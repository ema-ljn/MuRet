import shutil
from pathlib import Path

import pytest
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

SIZE = (100, 30)
JEWEL_OF_CREATION = 7190
JEWEL_NAME = "/records/7190/name"


@pytest.fixture
def item_file(working_copy: Path, reference_data: Path) -> Path:
    path = working_copy / "Local" / "item.bmd"
    shutil.copyfile(reference_data / "Local" / "item.bmd", path)
    return path


async def edit_name(app: MutoolsApp, pilot: Pilot[None]) -> RecordEditor:
    table = app.query_one(RecordTableView)
    table.move_cursor(row=table.get_row_index(str(JEWEL_OF_CREATION)))
    table.focus()
    await pilot.press("enter")
    await pilot.pause()
    fields = app.query_one(FieldList)
    fields.move_cursor(row=fields.get_row_index("name"))
    await pilot.press("enter")
    await pilot.pause()
    return app.query_one(RecordEditor)


async def test_item_names_are_searchable_by_name(item_file: Path) -> None:
    app = MutoolsApp(LocalFileStore(), item_file)
    async with app.run_test(size=SIZE) as pilot:
        table = app.query_one(RecordTableView)
        assert table.row_count == 8192
        assert "item · XOR3" in str(table.border_title)
        assert str(table.get_cell(str(JEWEL_OF_CREATION), "section")) == "14"
        table.focus()
        await pilot.press("slash", *"jewel of creation", "enter")
        await pilot.pause()
        assert table.row_count == 1
        await pilot.press("enter", "enter")
        await pilot.pause()
        assert app.query_one(RecordEditor).value == "Jewel of Creation"
        assert app.query_one(StatusLine).text == JEWEL_NAME


async def test_item_name_editor_rejects_a_name_without_room_for_nul(item_file: Path) -> None:
    app = MutoolsApp(LocalFileStore(), item_file)
    async with app.run_test(size=SIZE) as pilot:
        editor = await edit_name(app, pilot)
        assert "17/29 bytes" in str(editor.border_subtitle)
        editor.value = "é" * 29
        await pilot.pause()
        assert not editor.has_class("-invalid")
        editor.value += "é"
        await pilot.pause()
        assert editor.has_class("-invalid")
        await pilot.press("enter")
        assert editor.has_focus


async def test_item_name_edits_go_through_undo(item_file: Path) -> None:
    app = MutoolsApp(LocalFileStore(), item_file)
    async with app.run_test(size=SIZE) as pilot:
        editor = await edit_name(app, pilot)
        editor.value = "Joya de Creación"
        await pilot.press("enter", "ctrl+z")
        await pilot.pause()
        assert editor.value == "Jewel of Creation"
        await pilot.press("ctrl+y")
        await pilot.pause()
        assert editor.value == "Joya de Creación"


async def test_saving_an_item_edits_only_the_selected_name(item_file: Path) -> None:
    before = item_file.read_bytes()
    app = MutoolsApp(LocalFileStore(), item_file)
    async with app.run_test(size=SIZE) as pilot:
        editor = await edit_name(app, pilot)
        editor.value = "Joya de Creación"
        await pilot.press("enter", "ctrl+s")
        await pilot.pause()
        assert isinstance(app.screen, DiffScreen)
        assert [change.pointer for change in app.screen.changes] == [JEWEL_NAME]
        assert item_file.read_bytes() == before
        await pilot.press("y")
        await pilot.pause()
        assert app.query_one(StatusLine).text.startswith("saved 1 change ")
    after = item_file.read_bytes()
    name_start = JEWEL_OF_CREATION * 84
    name_end = name_start + 30
    payload_end = 8192 * 84
    assert after[:name_start] == before[:name_start]
    assert xor3(after[name_start:name_end]).split(b"\0", 1)[0] == b"Joya de Creaci\xf3n"
    assert after[name_end:payload_end] == before[name_end:payload_end]
    assert after[payload_end + 4 :] == before[payload_end + 4 :]


async def test_switching_from_an_item_to_text_keeps_the_pending_name_in_its_file(item_file: Path) -> None:
    app = MutoolsApp(LocalFileStore(), item_file)
    async with app.run_test(size=SIZE) as pilot:
        editor = await edit_name(app, pilot)
        assert editor.pointer == JEWEL_NAME
        editor.value = "Joya de Creación"
        await app.open_path(item_file.with_name("Text.bmd"))
        await pilot.pause()
        assert editor.disabled
        assert app.query_one(RecordTableView).row_count == 2048
        await app.open_path(item_file)
        editor = await edit_name(app, pilot)
        assert editor.value == "Joya de Creación"


async def test_invalid_item_name_keeps_the_tree_on_the_open_file(item_file: Path) -> None:
    app = MutoolsApp(LocalFileStore(), item_file)
    async with app.run_test(size=SIZE) as pilot:
        editor = await edit_name(app, pilot)
        editor.value = "é" * 30
        tree = app.query_one(BmdTree)
        other_file = item_file.with_name("Text.bmd")
        tree.focus()
        tree.move_cursor(tree.nodes[other_file])
        await pilot.press("enter")
        await pilot.pause()
        assert app.query_one(RecordTableView).row_count == 8192
        assert editor.pointer == JEWEL_NAME
        assert editor.value == "é" * 30
        assert tree.cursor_node is tree.nodes[item_file]
        assert editor.has_focus
