from pathlib import Path

from textual.pilot import Pilot

from mutools.adapters.storage import LocalFileStore
from mutools.adapters.tui.app import MutoolsApp
from mutools.adapters.tui.widgets.bmd_tree import BmdTree
from mutools.adapters.tui.widgets.diff_screen import DiffScreen
from mutools.adapters.tui.widgets.field_list import FieldList
from mutools.adapters.tui.widgets.record_editor import RecordEditor
from mutools.adapters.tui.widgets.record_table_view import RecordTableView
from mutools.adapters.tui.widgets.status_line import StatusLine
from mutools.core.ciphers.xor3 import xor3
from mutools.core.patch import Change

SIZE = (120, 40)
DIALOG_RECORD_SIZE = 0x400
FIRST_ANSWER = 0x180
ANSWER_SIZE = 0x40
GATE_RECORD_SIZE = 9


async def open_table(app: MutoolsApp, pilot: Pilot[None], path: Path) -> RecordTableView:
    await app.open_path(path)
    await pilot.pause()
    return app.query_one(RecordTableView)


async def open_fields(app: MutoolsApp, pilot: Pilot[None], index: int) -> FieldList:
    table = app.query_one(RecordTableView)
    table.move_cursor(row=table.get_row_index(str(index)))
    table.focus()
    await pilot.press("enter")
    await pilot.pause()
    return app.query_one(FieldList)


async def open_field(app: MutoolsApp, pilot: Pilot[None], index: int, field: str) -> RecordEditor:
    fields = await open_fields(app, pilot, index)
    fields.move_cursor(row=fields.get_row_index(field))
    await pilot.press("enter")
    await pilot.pause()
    return app.query_one(RecordEditor)


async def save(pilot: Pilot[None]) -> None:
    await pilot.press("ctrl+s")
    await pilot.pause()
    await pilot.press("y")
    await pilot.pause()


async def test_opening_a_dialog_table_lists_its_records(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        assert table.row_count == 200
        assert "dialog · XOR3 per record · 75/200 used" in str(table.border_title)
        table.focus()
        await pilot.press("f")
        await pilot.pause()
        assert table.row_count == 75
        assert str(table.border_subtitle) == "used · 75 rows"


async def test_the_filter_comes_back_to_every_row(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        table.focus()
        counts = []
        for _ in range(4):
            await pilot.press("f")
            await pilot.pause()
            counts.append((table.row_count, str(table.border_subtitle or "")))
        assert counts == [(75, "used · 75 rows"), (0, "changed · 0 rows"), (0, "findings · 0 rows"), (200, "")]


async def test_the_changed_filter_keeps_the_edited_rows(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        editor = await open_field(app, pilot, 53, "answers/0")
        editor.value = "Conte de novo."
        await pilot.press("enter")
        table.focus()
        await pilot.press("f", "f")
        await pilot.pause()
        assert [row.key.value for row in table.ordered_rows] == ["53"]


async def test_the_findings_filter_keeps_the_rows_with_findings(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "Text.bmd")
        table.focus()
        await pilot.press("f", "f", "f")
        await pilot.pause()
        assert [row.key.value for row in table.ordered_rows] == ["7"]


async def test_a_record_shows_its_fields(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        app.select_index(53)
        await pilot.pause()
        fields = app.query_one(FieldList)
        assert fields.display
        assert str(fields.border_title) == "record 53"
        assert str(fields.get_cell("answers/0", "value")) == "Please repeat the story."
        assert str(fields.get_cell("answer_count", "value")) == "3"
        assert str(fields.get_cell("next/3", "value")) == "-1"


async def test_a_text_table_has_no_field_list(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        await open_table(app, pilot, working_copy / "Local" / "Text.bmd")
        assert not app.query_one(FieldList).display


async def test_the_editor_says_what_to_select(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        editor = app.query_one(RecordEditor)
        await open_table(app, pilot, working_copy / "Local" / "Text.bmd")
        assert editor.placeholder == "select a record and press enter"
        await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        assert editor.placeholder == "select a field and press enter"


async def test_enter_goes_into_a_record_and_escape_comes_back(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        editor = await open_field(app, pilot, 53, "answers/0")
        fields = app.query_one(FieldList)
        assert editor.has_focus
        assert str(editor.border_title) == "/records/53/answers/0"
        assert editor.border_subtitle == "24/63 bytes"
        assert app.query_one(StatusLine).text == "/records/53/answers/0"
        await pilot.press("escape")
        await pilot.pause()
        assert fields.has_focus
        await pilot.press("escape")
        await pilot.pause()
        assert table.has_focus


async def test_moving_to_another_record_lets_go_of_the_editor(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        editor = await open_field(app, pilot, 53, "answers/0")
        await pilot.press("escape", "escape", "down")
        await pilot.pause()
        assert table.index_at_cursor() == 54
        assert editor.pointer is None
        assert editor.disabled


async def test_moving_to_another_record_keeps_a_typed_edit(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        editor = await open_field(app, pilot, 53, "answers/0")
        editor.value = "Conte de novo."
        table.focus()
        await pilot.press("down")
        await pilot.pause()
        assert app.session is not None
        assert app.session.changes == (Change("/records/53/answers/0", "Please repeat the story.", "Conte de novo."),)
        assert editor.pointer is None


async def test_an_edit_that_does_not_fit_holds_its_record(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Gate.bmd")
        editor = await open_field(app, pilot, 1, "level")
        editor.value = "abc"
        table.focus()
        await pilot.press("down")
        await pilot.pause()
        assert table.index_at_cursor() == 1
        assert editor.has_focus
        assert editor.pointer == "/records/1/level"
        assert app.session is not None
        assert not app.session.dirty


async def test_entering_another_field_keeps_a_typed_edit(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        editor = await open_field(app, pilot, 53, "answers/0")
        editor.value = "Conte de novo."
        fields = app.query_one(FieldList)
        fields.focus()
        fields.move_cursor(row=fields.get_row_index("answers/1"))
        await pilot.press("enter")
        await pilot.pause()
        assert editor.pointer == "/records/53/answers/1"
        assert app.session is not None
        assert app.session.changes == (Change("/records/53/answers/0", "Please repeat the story.", "Conte de novo."),)


async def test_editing_a_field_saves_only_that_field(working_copy: Path, reference_data: Path) -> None:
    target = working_copy / "Local" / "Dialog.bmd"
    before = (reference_data / "Local" / "Dialog.bmd").read_bytes()
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_table(app, pilot, target)
        editor = await open_field(app, pilot, 53, "answers/0")
        editor.value = "Conte de novo."
        await pilot.press("enter")
        await pilot.pause()
        assert app.query_one(FieldList).has_focus
        assert str(app.query_one(FieldList).get_cell("answers/0", "value")) == "Conte de novo."
        assert str(app.query_one(FieldList).get_cell("answers/0", "mark")) == "●"
        await save(pilot)
        assert app.query_one(StatusLine).text.startswith("saved 1 change ")
    after = target.read_bytes()
    start = 53 * DIALOG_RECORD_SIZE + FIRST_ANSWER
    end = start + ANSWER_SIZE
    record = xor3(after[53 * DIALOG_RECORD_SIZE : 54 * DIALOG_RECORD_SIZE])
    assert record[FIRST_ANSWER : FIRST_ANSWER + ANSWER_SIZE] == b"Conte de novo.".ljust(ANSWER_SIZE, b"\0")
    assert after[:start] == before[:start]
    assert after[end:] == before[end:]


async def test_a_number_field_takes_a_number(working_copy: Path) -> None:
    target = working_copy / "Gate.bmd"
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_table(app, pilot, target)
        editor = await open_field(app, pilot, 1, "level")
        assert editor.value == "20"
        editor.value = "25"
        await pilot.pause()
        assert not editor.has_class("-invalid")
        await pilot.press("enter")
        await save(pilot)
    assert xor3(target.read_bytes()[GATE_RECORD_SIZE : 2 * GATE_RECORD_SIZE])[8] == 25


async def test_a_number_field_refuses_letters(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_table(app, pilot, working_copy / "Gate.bmd")
        editor = await open_field(app, pilot, 1, "level")
        editor.value = "abc"
        await pilot.pause()
        assert editor.has_class("-invalid")
        await pilot.press("enter")
        await pilot.pause()
        assert editor.has_focus
        assert app.session is not None
        assert not app.session.dirty


async def test_a_text_with_a_tail_is_edited_as_its_text(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_table(app, pilot, working_copy / "Local" / "movereq.bmd")
        editor = await open_field(app, pilot, 10, "name")
        assert editor.value == "Atlans"
        editor.value = "Atlans 1"
        await pilot.press("enter", "ctrl+s")
        await pilot.pause()
        assert isinstance(app.screen, DiffScreen)
        assert app.screen.changes == (Change("/records/10/name", {"text": "Atlans", "tail": "d9cbb9"}, "Atlans 1"),)


async def test_a_text_with_a_tail_left_as_it_is_keeps_its_tail(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_table(app, pilot, working_copy / "Local" / "movereq.bmd")
        await open_field(app, pilot, 10, "name")
        await pilot.press("enter")
        await pilot.pause()
        assert app.session is not None
        assert not app.session.dirty
        assert app.session.value("/records/10/name") == {"text": "Atlans", "tail": "d9cbb9"}


async def test_raw_bytes_are_not_edited_in_the_console(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_table(app, pilot, working_copy / "Local" / "itemsetoption.bmd")
        editor = await open_field(app, pilot, 0, "name")
        assert editor.disabled
        assert app.query_one(FieldList).has_focus
        assert "mutools set" in app.query_one(StatusLine).text


async def test_search_looks_in_every_text_of_a_row(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "Dialog.bmd")
        table.focus()
        await pilot.press("slash", *"i will go find it")
        await pilot.pause()
        assert [row.key.value for row in table.ordered_rows] == ["53", "54", "77"]


async def test_search_finds_a_move_requirement_by_its_name(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "movereq.bmd")
        table.focus()
        await pilot.press("slash", *"Arena")
        await pilot.pause()
        assert [row.key.value for row in table.ordered_rows] == ["0"]


async def test_item_rows_keep_their_section(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Local" / "item.bmd")
        assert str(table.get_cell("7168", "section")) == "14"
        assert str(table.get_cell("0", "value")) == "Kris"


async def test_a_table_of_numbers_labels_its_rows_with_their_values(working_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_table(app, pilot, working_copy / "Gate.bmd")
        assert str(table.get_cell("1", "value")).startswith("flag 1 · map 0 · x1 121")


async def test_the_tree_dims_what_it_cannot_open(tmp_path: Path, working_copy: Path) -> None:
    folder = tmp_path / "Mixed"
    folder.mkdir()
    for name in ("Local/Text.bmd", "Gate.bmd"):
        (folder / Path(name).name).write_bytes((working_copy / name).read_bytes())
    (folder / "Player.bmd").write_bytes(b"a model")
    app = MutoolsApp(LocalFileStore(), folder)
    async with app.run_test(size=SIZE):
        tree = app.query_one(BmdTree)
        strips = [tree.render_line(y) for y in range(tree.last_line + 1)]
        lines = [strip.text for strip in strips]
        assert any("≡ Text.bmd" in line for line in lines)
        assert any("▤ Gate.bmd" in line for line in lines)
        dimmed = [segment.text for strip in strips for segment in strip if segment.style and segment.style.dim]
        assert dimmed == ["Player.bmd"]
