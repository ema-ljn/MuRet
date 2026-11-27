import os
from collections.abc import Iterable
from pathlib import Path
from typing import Any, ClassVar

from rich.text import Text
from textual.app import App, ComposeResult, SystemCommand
from textual.binding import Binding, BindingType
from textual.containers import Horizontal, Vertical
from textual.screen import Screen
from textual.widgets import DataTable, Footer, Input

from mutools.adapters.storage import WORKSPACE_SUGGESTION
from mutools.adapters.tui.theme import LORENCIA, LORENCIA_LIGHT
from mutools.adapters.tui.widgets.bmd_tree import BmdTree
from mutools.adapters.tui.widgets.diff_screen import DiffScreen
from mutools.adapters.tui.widgets.field_list import FACT_KEY, FieldList
from mutools.adapters.tui.widgets.model_tree import ModelTree
from mutools.adapters.tui.widgets.record_editor import FIELD_HINT, RECORD_HINT, RecordEditor
from mutools.adapters.tui.widgets.record_table_view import RecordTableView, RowLayout
from mutools.adapters.tui.widgets.status_line import StatusLine
from mutools.adapters.tui.widgets.title_bar import TitleBar
from mutools.app.documents import format_name_of
from mutools.app.model_parts import counted, is_model, model_parts, part_facts, part_fields, part_of, part_title
from mutools.app.rows import (
    ROW_POINTER_TOKENS,
    RowFilter,
    edit_text,
    findings_by_row,
    label_title,
    row_pointer,
    row_shown,
    row_text,
    rows_of,
)
from mutools.app.session import EditSession, Measure
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.findings import Finding
from mutools.core.json_types import JsonValue
from mutools.core.leaves import is_leaf
from mutools.core.pointer import parse_pointer
from mutools.core.ports import FileStore, Summary

USED_BAR_CELLS = 9
READ_ONLY_HINT = f"read-only reference folder: make a working copy with `{WORKSPACE_SUGGESTION}`"
STALE_HINT = "the file changed on disk: press r to read it again and keep your edits"
RAW_BYTES_HINT = "is kept as raw bytes: change it with mutools set or mutools apply"
CLI_EDITS_HINT = "the rest of a model is edited with mutools set or mutools apply"
TABLES_ONLY_HINT = "search and filter work in tables"
NO_CIPHER = "none"


def absolute(path: Path) -> Path:
    return Path(os.path.abspath(path))


def shown_path(path: Path) -> str:
    try:
        return f"~/{path.relative_to(Path.home()).as_posix()}"
    except ValueError:
        return str(path)


def used_bar(used: int, records: int) -> str:
    filled = round(USED_BAR_CELLS * used / records) if records else 0
    return "▰" * filled + "▱" * (USED_BAR_CELLS - filled)


def described(format_name: str, summary: Summary) -> str:
    if summary.records is None or summary.used_records is None:
        cipher = "" if summary.cipher == NO_CIPHER else f" · {summary.cipher}"
        counts = (
            counted(summary.meshes or 0, "mesh", "meshes"),
            counted(summary.bones or 0, "bone"),
            counted(summary.actions or 0, "action"),
        )
        return f"{format_name} · version {summary.version}{cipher} · {' · '.join(counts)}"
    used = f"{summary.used_records}/{summary.records} used {used_bar(summary.used_records, summary.records)}"
    return f"{format_name} · {summary.cipher} · {used}"


def warned(warnings: tuple[Finding, ...]) -> str:
    return "".join(f" · ⚠ {warning.code} {warning.pointer}" for warning in warnings)


class MutoolsApp(App[None]):
    CSS_PATH = "mutools.tcss"
    TITLE = "mutools"
    BINDINGS: ClassVar[list[BindingType]] = [
        Binding("slash", "search", "search"),
        Binding("f", "filter", "filter"),
        Binding("escape", "cancel", "cancel", show=False),
        Binding("ctrl+z", "undo", "undo"),
        Binding("ctrl+y", "redo", "redo", show=False),
        Binding("ctrl+s", "save", "save"),
        Binding("d", "diff", "diff"),
        Binding("r", "reload", "read again", show=False),
        Binding("t", "toggle_theme", "light/dark", show=False),
        Binding("q", "quit", "quit"),
    ]

    def __init__(self, store: FileStore, root: Path) -> None:
        super().__init__()
        root = absolute(root.expanduser())
        self.store = store
        self.folder = root if root.is_dir() else root.parent
        self.start_file = None if root.is_dir() else root
        self.sessions: dict[Path, EditSession] = {}
        self.session: EditSession | None = None
        self.row_filter = RowFilter.ALL
        self.quit_armed = False
        self._row_texts: tuple[list[JsonValue], list[str]] | None = None

    def compose(self) -> ComposeResult:
        yield TitleBar()
        with Horizontal(id="body"):
            files = self.store.list_bmd(self.folder)
            tree = BmdTree(self.folder, {file: format_name_of(self.store, file) for file in files})
            tree.border_title = Text(self.folder.name)
            yield tree
            with Vertical(id="document"):
                yield Input(placeholder="search texts", id="search")
                yield RecordTableView()
                yield ModelTree()
                yield FieldList()
                yield RecordEditor()
        yield StatusLine()
        yield Footer()

    async def on_mount(self) -> None:
        self.register_theme(LORENCIA)
        self.register_theme(LORENCIA_LIGHT)
        self.theme = LORENCIA.name
        self.query_one(RecordTableView).border_title = "open a .bmd from the tree"
        self.refresh_title()
        if self.start_file is not None:
            await self.open_path(self.start_file)

    def get_system_commands(self, screen: Screen[Any]) -> Iterable[SystemCommand]:
        yield from super().get_system_commands(screen)
        yield SystemCommand("Search texts", "Keep the rows with a text that holds a search term", self.action_search)
        yield SystemCommand(
            "Filter rows", "Show every row, the used ones, the changed ones or those with findings", self.action_filter
        )
        yield SystemCommand("Undo", "Undo the last edit", self.action_undo)
        yield SystemCommand("Redo", "Redo the last undone edit", self.action_redo)
        yield SystemCommand("Save", "Show the changes and write them", self.action_save)
        yield SystemCommand("Diff", "Show the unsaved changes", self.action_diff)
        yield SystemCommand("Read again", "Read the file again and keep the unsaved edits", self.action_reload)
        yield SystemCommand("Light or dark", "Switch between Lorencia and Lorencia light", self.action_toggle_theme)

    def status(self, text: str, *, error: bool = False) -> None:
        self.query_one(StatusLine).show(text, error=error)

    def failed(self, error: MutoolsError) -> None:
        message = STALE_HINT if error.code is ErrorCode.STALE_FILE else error.message
        self.status(f"{error.code}: {message}", error=True)

    def label_of(self, path: Path) -> str:
        try:
            return path.relative_to(self.folder).as_posix()
        except ValueError:
            return path.name

    def refresh_title(self) -> None:
        unsaved = sum(len(session.changes) for session in self.sessions.values())
        self.query_one(TitleBar).show(shown_path(self.folder), self.store.is_reference(self.folder), unsaved)
        self.query_one(BmdTree).mark_unsaved({path for path, session in self.sessions.items() if session.dirty})
        if self.session is not None:
            described_file = described(self.session.format_name, self.session.summary)
            self.main_view().border_title = Text(f"{self.label_of(self.session.path)} ─ {described_file}")

    def model_open(self) -> bool:
        return self.session is not None and is_model(self.session.view)

    def main_view(self) -> RecordTableView | ModelTree:
        return self.query_one(ModelTree) if self.model_open() else self.query_one(RecordTableView)

    def row_texts(self, values: list[JsonValue]) -> list[str]:
        if self._row_texts is None or self._row_texts[0] is not values:
            self._row_texts = (values, [row_text(value) for value in values])
        return self._row_texts[1]

    def shown_rows(self, session: EditSession) -> list[int]:
        values, original = rows_of(session.view), rows_of(session.original)
        query = self.query_one("#search", Input).value.casefold()
        texts = self.row_texts(values) if query else None
        findings = findings_by_row(session.findings) if self.row_filter is RowFilter.FINDINGS else {}
        shown: list[int] = []
        for index, value in enumerate(values):
            pointer = row_pointer(session.view, index)
            before = original[index] if index < len(original) else None
            if texts is not None and query not in texts[index]:
                continue
            if row_shown(pointer, value, before, findings.get(pointer, ()), self.row_filter):
                shown.append(index)
        return shown

    def show_document(self) -> None:
        session = self.session
        if session is None:
            return
        model = is_model(session.view)
        self.query_one("#document").set_class(model, "-model")
        if model:
            self.show_parts(session)
        else:
            self.show_rows(session)

    def show_parts(self, session: EditSession) -> None:
        changed = {part_of(change.pointer) for change in session.changes}
        mark = self.current_theme.error or "red"
        self.query_one(ModelTree).show(session.path, model_parts(session.view), changed, mark)
        self.query_one(FieldList).add_class("-visible")
        self.query_one(RecordEditor).placeholder = FIELD_HINT
        self.show_fields()

    def show_rows(self, session: EditSession) -> None:
        theme = self.current_theme
        values = rows_of(session.view)
        leaf_rows = bool(values) and is_leaf(values[0])
        title = "text" if leaf_rows else label_title(session.fields)
        shown = self.shown_rows(session)
        table = self.query_one(RecordTableView)
        table.set_colors(theme.accent or "yellow", theme.error or "red")
        table.show(
            values,
            rows_of(session.original),
            session.encoding,
            shown,
            RowLayout(title, leaf_rows, session.summary.section_size),
        )
        filtered = self.row_filter is not RowFilter.ALL
        table.border_subtitle = f"{self.row_filter} · {len(shown)} rows" if filtered else None
        self.query_one(FieldList).set_class(not leaf_rows, "-visible")
        self.query_one(RecordEditor).placeholder = RECORD_HINT if leaf_rows else FIELD_HINT
        self.show_fields()

    def show_part_fields(self, session: EditSession) -> None:
        part = self.query_one(ModelTree).pointer
        fields, before = part_fields(session.view, part), part_fields(session.original, part)
        mark = self.current_theme.error or "red"
        self.query_one(FieldList).show(part, part_title(part), fields, before, mark, part_facts(session.view, part))

    def show_fields(self) -> None:
        fields = self.query_one(FieldList)
        if self.session is not None and self.model_open():
            self.show_part_fields(self.session)
            return
        index = self.query_one(RecordTableView).index_at_cursor()
        if self.session is None or index is None or not fields.has_class("-visible"):
            fields.empty()
            return
        values, original = rows_of(self.session.view), rows_of(self.session.original)
        before = original[index] if index < len(original) else None
        mark = self.current_theme.error or "red"
        fields.show(row_pointer(self.session.view, index), f"record {index}", values[index], before, mark)

    async def open_path(self, path: Path) -> None:
        path = absolute(path)
        try:
            session = self.sessions.get(path) or EditSession.open(self.store, path)
        except MutoolsError as error:
            self.failed(error)
            return
        search = self.query_one("#search", Input)
        previous = self.session
        if previous is not session:
            editor = self.query_one(RecordEditor)
            if previous is not None and not self.commit_editor():
                tree = self.query_one(BmdTree)
                tree.reveal(previous.path)
                tree.call_after_refresh(tree.move_cursor, tree.nodes.get(previous.path))
                editor.focus()
                return
            editor.reset()
            search.remove_class("-visible")
            search.value = ""
            self.row_filter = RowFilter.ALL
        self.sessions[path] = session
        self.session = session
        self.query_one(BmdTree).reveal(path)
        self.show_document()
        self.refresh_title()
        self.status(f"{shown_path(path)} opened")

    def on_bmd_tree_file_selected(self, event: BmdTree.FileSelected) -> None:
        self.run_worker(self.open_path(event.path))

    def on_data_table_row_highlighted(self, event: DataTable.RowHighlighted) -> None:
        if self.session is None or event.row_key.value is None or not event.data_table.is_attached:
            return
        if isinstance(event.data_table, FieldList):
            pointer = event.data_table.pointer_of(event.row_key.value)
            if pointer is not None and event.data_table.has_focus:
                self.status(pointer)
            return
        if self.model_open():
            return
        pointer = row_pointer(self.session.view, int(event.row_key.value))
        if self.leave_editor(pointer):
            self.status(pointer)
            self.show_fields()

    def leave_editor(self, row: str) -> bool:
        editor = self.query_one(RecordEditor)
        if editor.pointer is None or editor.pointer == row or editor.pointer.startswith(f"{row}/"):
            return True
        if self.commit_editor():
            editor.reset()
            return True
        table = self.query_one(RecordTableView)
        key = str(parse_pointer(editor.pointer)[ROW_POINTER_TOKENS - 1])
        if key in table.rows:
            table.move_cursor(row=table.get_row_index(key))
        editor.focus()
        return False

    def on_model_tree_part_highlighted(self, event: ModelTree.PartHighlighted) -> None:
        if self.session is not None and self.leave_part(event.pointer):
            if self.query_one(ModelTree).has_focus:
                self.status(event.pointer or "/")
            self.show_fields()

    def leave_part(self, part: str) -> bool:
        editor = self.query_one(RecordEditor)
        if editor.pointer is None or part_of(editor.pointer) == part:
            return True
        if self.commit_editor():
            editor.reset()
            return True
        self.query_one(ModelTree).select(part_of(editor.pointer))
        editor.focus()
        return False

    def on_model_tree_part_selected(self, event: ModelTree.PartSelected) -> None:
        if self.session is None:
            return
        if part_fields(self.session.view, event.pointer):
            self.query_one(FieldList).focus()
        elif not self.query_one(ModelTree).toggle(event.pointer):
            self.status(f"{part_title(event.pointer)} has nothing to edit here: {CLI_EDITS_HINT}")

    def on_data_table_row_selected(self, event: DataTable.RowSelected) -> None:
        fields = self.query_one(FieldList)
        if event.row_key.value is None:
            return
        if event.data_table is fields:
            pointer = fields.pointer_of(event.row_key.value)
            if pointer is None:
                self.status(f"{event.row_key.value.removeprefix(FACT_KEY)} is shown, not edited here: {CLI_EDITS_HINT}")
            else:
                self.edit_pointer(pointer, pointer, focus=True)
        elif fields.has_class("-visible"):
            fields.focus()
        else:
            self.edit_index(int(event.row_key.value), focus=True)

    def measure(self, session: EditSession, pointer: str, raw: str) -> Measure:
        try:
            return session.measure(pointer, session.parse(pointer, raw))
        except MutoolsError as error:
            return Measure(None, None, error)

    def edit_pointer(self, pointer: str, title: str, *, focus: bool) -> None:
        if self.session is None:
            return
        if self.session.read_only:
            self.status(READ_ONLY_HINT, error=True)
            return
        editor = self.query_one(RecordEditor)
        if not self.commit_editor():
            editor.focus()
            return
        raw = edit_text(self.session.value(pointer))
        if raw is None:
            editor.reset()
            self.status(f"{pointer} {RAW_BYTES_HINT}", error=True)
            return
        editor.load(pointer, title, raw)
        editor.show_measure(self.measure(self.session, pointer, raw))
        self.status(pointer)
        if focus:
            editor.focus()

    def edit_index(self, index: int, *, focus: bool) -> None:
        if self.session is not None:
            self.edit_pointer(row_pointer(self.session.view, index), f"record {index}", focus=focus)

    def select_index(self, index: int) -> None:
        table = self.query_one(RecordTableView)
        table.move_cursor(row=table.get_row_index(str(index)))
        if self.query_one(FieldList).has_class("-visible"):
            self.show_fields()
        else:
            self.edit_index(index, focus=False)

    def select_part(self, pointer: str) -> None:
        self.query_one(ModelTree).select(pointer)

    def editor_home(self) -> DataTable[Text]:
        fields = self.query_one(FieldList)
        return fields if fields.has_class("-visible") else self.query_one(RecordTableView)

    def on_input_changed(self, event: Input.Changed) -> None:
        if event.input.id == "search":
            self.show_document()
        elif isinstance(event.input, RecordEditor) and self.session is not None and event.input.pointer is not None:
            event.input.show_measure(self.measure(self.session, event.input.pointer, event.value))

    def on_input_submitted(self, event: Input.Submitted) -> None:
        if isinstance(event.input, RecordEditor):
            if self.commit_editor():
                self.editor_home().focus()
        else:
            self.query_one(RecordTableView).focus()

    def commit_editor(self) -> bool:
        editor = self.query_one(RecordEditor)
        if self.session is None or editor.pointer is None or not editor.pending:
            return True
        try:
            self.session.set(editor.pointer, self.session.parse(editor.pointer, editor.value))
        except MutoolsError as error:
            self.failed(error)
            return False
        editor.loaded = editor.value
        self.after_edit()
        return True

    def show_edited(self, session: EditSession) -> None:
        editor = self.query_one(RecordEditor)
        if editor.pointer is None or editor.has_focus:
            return
        try:
            raw = edit_text(session.value(editor.pointer))
        except MutoolsError:
            raw = None
        if raw is None:
            editor.reset()
        else:
            editor.show_value(raw)

    def after_edit(self) -> None:
        self.quit_armed = False
        if self.session is not None:
            table = self.query_one(RecordTableView)
            if self.model_open() or not table.update_rows(rows_of(self.session.view), rows_of(self.session.original)):
                self.show_document()
            else:
                self.show_fields()
            self.show_edited(self.session)
        self.refresh_title()

    def action_search(self) -> None:
        if self.model_open():
            self.status(TABLES_ONLY_HINT)
            return
        search = self.query_one("#search", Input)
        search.add_class("-visible")
        search.focus()

    def action_filter(self) -> None:
        if self.model_open():
            self.status(TABLES_ONLY_HINT)
        elif self.session is not None:
            self.row_filter = self.row_filter.following()
            self.show_document()
            self.status(f"rows: {self.row_filter}")

    def action_cancel(self) -> None:
        search = self.query_one("#search", Input)
        editor = self.query_one(RecordEditor)
        if search.has_focus:
            search.remove_class("-visible")
            search.value = ""
        elif editor.has_focus:
            editor.revert()
            self.editor_home().focus()
            return
        self.main_view().focus()

    def action_undo(self) -> None:
        if self.session is not None and self.session.undo():
            self.after_edit()

    def action_redo(self) -> None:
        if self.session is not None and self.session.redo():
            self.after_edit()

    def diff_screen(self, session: EditSession, *, confirm: bool) -> DiffScreen:
        return DiffScreen(session.changes, shown_path(session.path), confirm=confirm, warnings=session.warnings)

    def action_diff(self) -> None:
        if self.session is not None:
            self.push_screen(self.diff_screen(self.session, confirm=False))

    def action_reload(self) -> None:
        if self.session is None or not self.commit_editor():
            return
        try:
            self.session.reload()
        except MutoolsError as error:
            self.failed(error)
            return
        self.after_edit()
        kept = len(self.session.changes)
        self.status(f"{shown_path(self.session.path)} read again, {kept} unsaved change{'s' if kept != 1 else ''} kept")

    def action_save(self) -> None:
        if self.session is None or not self.commit_editor():
            return
        if self.session.read_only:
            self.status(READ_ONLY_HINT, error=True)
            return
        if not self.session.dirty:
            self.status("nothing to save")
            return
        self.push_screen(self.diff_screen(self.session, confirm=True), self.write_confirmed)

    def write_confirmed(self, confirmed: bool | None) -> None:
        if not confirmed or self.session is None:
            return
        try:
            result = self.session.save()
        except MutoolsError as error:
            self.failed(error)
            return
        self.after_edit()
        count = len(result.changes)
        saved = f"saved {count} change{'s' if count != 1 else ''} to {shown_path(result.path)}"
        self.status(f"{saved}{warned(result.warnings)}")

    def action_toggle_theme(self) -> None:
        self.theme = LORENCIA_LIGHT.name if self.theme == LORENCIA.name else LORENCIA.name
        self.show_document()

    async def action_quit(self) -> None:
        if any(session.dirty for session in self.sessions.values()) and not self.quit_armed:
            self.quit_armed = True
            self.status("unsaved changes: ctrl+s to save, or q again to quit without saving", error=True)
            return
        self.exit()
