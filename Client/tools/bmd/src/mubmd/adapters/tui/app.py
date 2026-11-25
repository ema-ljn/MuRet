import os
from collections.abc import Iterable
from pathlib import Path
from typing import Any, ClassVar

from rich.text import Text
from textual.app import App, ComposeResult, SystemCommand
from textual.binding import Binding, BindingType
from textual.containers import Horizontal, Vertical
from textual.screen import Screen
from textual.widgets import Footer, Input

from mubmd.adapters.storage import WORKSPACE_SUGGESTION
from mubmd.adapters.tui.theme import LORENCIA, LORENCIA_LIGHT
from mubmd.adapters.tui.widgets.bmd_tree import BmdTree
from mubmd.adapters.tui.widgets.diff_screen import DiffScreen
from mubmd.adapters.tui.widgets.record_editor import RecordEditor
from mubmd.adapters.tui.widgets.record_table_view import RecordTableView
from mubmd.adapters.tui.widgets.status_line import StatusLine
from mubmd.adapters.tui.widgets.title_bar import TitleBar
from mubmd.app.session import EditSession
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding
from mubmd.core.json_types import JsonValue
from mubmd.core.ports import FileStore

USED_BAR_CELLS = 9
READ_ONLY_HINT = f"read-only reference folder: make a working copy with `{WORKSPACE_SUGGESTION}`"
STALE_HINT = "the file changed on disk: press r to read it again and keep your edits"


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


def row_values(view: dict[str, JsonValue]) -> list[JsonValue]:
    if view.get("format") == "item":
        records = view.get("records")
        if not isinstance(records, list):
            return []
        return [record.get("name") if isinstance(record, dict) else None for record in records]
    texts = view.get("texts")
    return texts if isinstance(texts, list) else []


def record_pointer(session: EditSession, index: int) -> str:
    return f"/records/{index}/name" if session.format_name == "item" else f"/texts/{index}"


def warned(warnings: tuple[Finding, ...]) -> str:
    return "".join(f" · ⚠ {warning.code} {warning.pointer}" for warning in warnings)


class MubmdApp(App[None]):
    CSS_PATH = "mubmd.tcss"
    TITLE = "mubmd"
    BINDINGS: ClassVar[list[BindingType]] = [
        Binding("slash", "search", "search"),
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
        self.quit_armed = False

    def compose(self) -> ComposeResult:
        yield TitleBar()
        with Horizontal(id="body"):
            tree = BmdTree(self.folder, self.store.list_bmd(self.folder))
            tree.border_title = Text(self.folder.name)
            yield tree
            with Vertical(id="document"):
                yield Input(placeholder="search texts", id="search")
                yield RecordTableView()
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
        names = "item names" if self.session is not None and self.session.format_name == "item" else "texts"
        yield SystemCommand(f"Search {names}", "Filter the rows by a search term", self.action_search)
        yield SystemCommand("Undo", "Undo the last edit", self.action_undo)
        yield SystemCommand("Redo", "Redo the last undone edit", self.action_redo)
        yield SystemCommand("Save", "Show the changes and write them", self.action_save)
        yield SystemCommand("Diff", "Show the unsaved changes", self.action_diff)
        yield SystemCommand("Read again", "Read the file again and keep the unsaved edits", self.action_reload)
        yield SystemCommand("Light or dark", "Switch between Lorencia and Lorencia light", self.action_toggle_theme)

    def status(self, text: str, *, error: bool = False) -> None:
        self.query_one(StatusLine).show(text, error=error)

    def failed(self, error: MubmdError) -> None:
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
            summary = self.session.summary
            self.query_one(RecordTableView).border_title = Text(
                f"{self.label_of(self.session.path)} ─ {self.session.format_name} · {summary.cipher} · "
                f"{summary.used_records}/{summary.records} used {used_bar(summary.used_records, summary.records)}"
            )

    def show_rows(self, query: str = "") -> None:
        if self.session is None:
            return
        theme = self.current_theme
        table = self.query_one(RecordTableView)
        table.set_colors(theme.accent or "yellow", theme.error or "red")
        table.show(
            row_values(self.session.view),
            row_values(self.session.original),
            self.session.encoding,
            query,
            item_names=self.session.format_name == "item",
        )

    async def open_path(self, path: Path) -> None:
        path = absolute(path)
        try:
            session = self.sessions.get(path) or EditSession.open(self.store, path)
        except MubmdError as error:
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
        search.placeholder = "search item names" if session.format_name == "item" else "search texts"
        self.sessions[path] = session
        self.session = session
        self.query_one(BmdTree).reveal(path)
        self.show_rows(search.value)
        self.refresh_title()
        self.status(f"{shown_path(path)} opened")

    def on_bmd_tree_file_selected(self, event: BmdTree.FileSelected) -> None:
        self.run_worker(self.open_path(event.path))

    def on_data_table_row_highlighted(self, event: RecordTableView.RowHighlighted) -> None:
        if self.session is not None and event.row_key.value is not None:
            self.status(record_pointer(self.session, int(event.row_key.value)))

    def on_data_table_row_selected(self, event: RecordTableView.RowSelected) -> None:
        if event.row_key.value is not None:
            self.edit_index(int(event.row_key.value), focus=True)

    def edit_index(self, index: int, *, focus: bool) -> None:
        if self.session is None:
            return
        if self.session.read_only:
            self.status(READ_ONLY_HINT, error=True)
            return
        pointer = record_pointer(self.session, index)
        value = self.session.value(pointer)
        if not isinstance(value, str):
            self.query_one(RecordEditor).reset()
            self.status(f"{pointer} is a hex record: change it with mubmd set or mubmd apply", error=True)
            return
        editor = self.query_one(RecordEditor)
        editor.load(pointer, index, value)
        editor.show_measure(self.session.measure(pointer, value))
        self.status(pointer)
        if focus:
            editor.focus()

    def select_index(self, index: int) -> None:
        table = self.query_one(RecordTableView)
        table.move_cursor(row=table.get_row_index(str(index)))
        self.edit_index(index, focus=False)

    def on_input_changed(self, event: Input.Changed) -> None:
        if event.input.id == "search":
            self.show_rows(event.value)
        elif isinstance(event.input, RecordEditor) and self.session is not None and event.input.pointer is not None:
            event.input.show_measure(self.session.measure(event.input.pointer, event.value))

    def on_input_submitted(self, event: Input.Submitted) -> None:
        if isinstance(event.input, RecordEditor):
            if self.commit_editor():
                self.query_one(RecordTableView).focus()
        else:
            self.query_one(RecordTableView).focus()

    def commit_editor(self) -> bool:
        editor = self.query_one(RecordEditor)
        if self.session is None or editor.pointer is None or not editor.pending:
            return True
        try:
            self.session.set(editor.pointer, editor.value)
        except MubmdError as error:
            self.failed(error)
            return False
        editor.loaded = editor.value
        self.after_edit()
        return True

    def after_edit(self) -> None:
        self.quit_armed = False
        if self.session is not None:
            table = self.query_one(RecordTableView)
            table.update_rows(row_values(self.session.view), row_values(self.session.original))
            editor = self.query_one(RecordEditor)
            value = None if editor.pointer is None else self.session.value(editor.pointer)
            if isinstance(value, str) and not editor.has_focus:
                editor.show_value(value)
        self.refresh_title()

    def action_search(self) -> None:
        search = self.query_one("#search", Input)
        search.add_class("-visible")
        search.focus()

    def action_cancel(self) -> None:
        search = self.query_one("#search", Input)
        editor = self.query_one(RecordEditor)
        if search.has_focus:
            search.remove_class("-visible")
            search.value = ""
        elif editor.has_focus:
            editor.revert()
        self.query_one(RecordTableView).focus()

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
        except MubmdError as error:
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
        except MubmdError as error:
            self.failed(error)
            return
        self.after_edit()
        count = len(result.changes)
        saved = f"saved {count} change{'s' if count != 1 else ''} to {shown_path(result.path)}"
        self.status(f"{saved}{warned(result.warnings)}")

    def action_toggle_theme(self) -> None:
        self.theme = LORENCIA_LIGHT.name if self.theme == LORENCIA.name else LORENCIA.name
        self.show_rows(self.query_one("#search", Input).value)

    async def action_quit(self) -> None:
        if any(session.dirty for session in self.sessions.values()) and not self.quit_armed:
            self.quit_armed = True
            self.status("unsaved changes: ctrl+s to save, or q again to quit without saving", error=True)
            return
        self.exit()
