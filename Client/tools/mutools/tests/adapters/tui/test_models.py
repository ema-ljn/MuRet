import shutil
from pathlib import Path

from textual.pilot import Pilot

from mutools.adapters.storage import LocalFileStore
from mutools.adapters.tui.app import MutoolsApp
from mutools.adapters.tui.widgets.bmd_tree import BmdTree
from mutools.adapters.tui.widgets.field_list import FieldList
from mutools.adapters.tui.widgets.model_tree import ModelTree
from mutools.adapters.tui.widgets.record_editor import RecordEditor
from mutools.adapters.tui.widgets.record_table_view import RecordTableView
from mutools.adapters.tui.widgets.status_line import StatusLine
from tests.models import CODEC, model_file

SIZE = (120, 40)
SWORD_TEXTURE_NAME = slice(6772, 6804)


def helm(folder: Path) -> Path:
    return folder / "player" / "HelmMale01.bmd"


async def open_model(app: MutoolsApp, pilot: Pilot[None], path: Path) -> ModelTree:
    await app.open_path(path)
    await pilot.pause()
    return app.query_one(ModelTree)


async def open_fields(app: MutoolsApp, pilot: Pilot[None], part: str) -> FieldList:
    tree = app.query_one(ModelTree)
    tree.select(part)
    tree.focus()
    await pilot.pause()
    await pilot.press("enter")
    await pilot.pause()
    return app.query_one(FieldList)


async def open_field(app: MutoolsApp, pilot: Pilot[None], part: str, field: str) -> RecordEditor:
    fields = await open_fields(app, pilot, part)
    fields.move_cursor(row=fields.get_row_index(field))
    await pilot.press("enter")
    await pilot.pause()
    return app.query_one(RecordEditor)


async def write(app: MutoolsApp, pilot: Pilot[None], part: str, field: str, value: str) -> None:
    editor = await open_field(app, pilot, part, field)
    editor.value = value
    await pilot.pause()
    await pilot.press("enter")
    await pilot.pause()


async def save(pilot: Pilot[None]) -> None:
    await pilot.press("ctrl+s")
    await pilot.pause()
    await pilot.press("y")
    await pilot.pause()


def parent_of(tree: ModelTree, part: str) -> str | None:
    parent = tree.nodes[part].parent
    return None if parent is None else parent.data


async def test_opening_a_model_shows_its_tree(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, helm(model_copy))
        assert [str(node.label) for node in tree.root.children] == ["meshes (2)", "actions (1)", "bones (56)"]
        assert str(tree.border_title) == "player/HelmMale01.bmd ─ model · version 10 · 2 meshes · 56 bones · 1 action"
        assert tree.display
        assert not app.query_one(RecordTableView).display
        assert parent_of(tree, "/bones/3") == "/bones/2"


async def test_an_encrypted_model_says_so_in_its_title(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, model_copy / "skill" / "m_waves.bmd")
        assert "model · version 12 · K16 · " in str(tree.border_title)


async def test_a_table_opened_after_a_model_shows_its_rows(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, helm(model_copy))
        await app.open_path(model_copy / "Local" / "Text.bmd")
        await pilot.pause()
        table = app.query_one(RecordTableView)
        assert (table.display, tree.display, table.row_count) == (True, False, 2048)
        assert not app.query_one(FieldList).display


async def test_moving_through_the_tree_shows_the_parts_fields(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, helm(model_copy))
        tree.select("/meshes/1")
        await pilot.pause()
        fields = app.query_one(FieldList)
        assert str(fields.border_title) == "mesh 1"
        assert [str(key.value) for key in fields.rows][:2] == ["texture_name", "texture"]
        assert str(fields.get_cell("texture_name", "value")) == "hide.jpg"
        assert str(fields.get_cell("fact slot", "value")) == "hidden"


async def test_renaming_a_texture_saves_only_its_name(model_copy: Path, reference_data: Path) -> None:
    sword = model_copy / "Item" / "Sword01.bmd"
    before = (reference_data / "Item" / "Sword01.bmd").read_bytes()
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_model(app, pilot, sword)
        editor = await open_field(app, pilot, "/meshes/0", "texture_name")
        assert (str(editor.border_title), editor.value) == ("/meshes/0/texture_name", "sword02.jpg")
        editor.value = "etcring.jpg"
        await pilot.pause()
        assert str(editor.border_subtitle) == "11/31 bytes"
        await pilot.press("enter")
        await pilot.pause()
        await save(pilot)
    after = sword.read_bytes()
    assert after[SWORD_TEXTURE_NAME] == b"etcring.jpg".ljust(32, b"\0")
    assert after[: SWORD_TEXTURE_NAME.start] == before[: SWORD_TEXTURE_NAME.start]
    assert after[SWORD_TEXTURE_NAME.stop :] == before[SWORD_TEXTURE_NAME.stop :]


async def test_moving_a_bone_moves_it_in_the_tree(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, helm(model_copy))
        await write(app, pilot, "/bones/3", "parent", "0")
        assert parent_of(tree, "/bones/3") == "/bones/0"
        assert str(tree.nodes["/bones/3"].label) == "3● Bip01 L Thigh"
        assert tree.cursor_node is tree.nodes["/bones/3"]
        assert app.query_one(FieldList).has_focus
        await pilot.press("ctrl+z")
        await pilot.pause()
        assert parent_of(tree, "/bones/3") == "/bones/2"
        assert str(tree.nodes["/bones/3"].label) == "3  Bip01 L Thigh"


async def test_a_parent_after_the_bone_warns_and_is_taken(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_model(app, pilot, helm(model_copy))
        editor = await open_field(app, pilot, "/bones/1", "parent")
        editor.value = "5"
        await pilot.pause()
        assert str(editor.border_subtitle) == "number · ⚠ PARENT_AFTER_CHILD"
        await pilot.press("enter")
        await pilot.pause()
        assert app.session is not None
        assert app.session.value("/bones/1/parent") == 5


async def test_a_parent_the_model_lacks_is_refused(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_model(app, pilot, helm(model_copy))
        editor = await open_field(app, pilot, "/bones/1", "parent")
        editor.value = "40000"
        await pilot.pause()
        assert str(editor.border_subtitle).startswith("✗ ")
        await pilot.press("enter")
        await pilot.pause()
        assert app.session is not None
        assert (app.session.value("/bones/1/parent"), editor.has_focus) == (0, True)


async def test_a_dummy_bone_has_nothing_to_edit(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, helm(model_copy))
        await open_fields(app, pilot, "/bones/9")
        assert tree.has_focus
        assert "bone 9 has nothing to edit here" in app.query_one(StatusLine).text


async def test_enter_on_a_group_folds_it(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, helm(model_copy))
        await open_fields(app, pilot, "/bones")
        assert (tree.has_focus, tree.nodes["/bones"].is_expanded) == (True, False)
        await write(app, pilot, "/meshes/1", "texture", "0")
        assert not tree.nodes["/bones"].is_expanded


async def test_a_fact_is_shown_and_not_edited(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_model(app, pilot, helm(model_copy))
        fields = await open_fields(app, pilot, "/meshes/0")
        fields.move_cursor(row=fields.get_row_index("fact vertices"))
        await pilot.press("enter")
        await pilot.pause()
        assert fields.has_focus
        assert app.query_one(RecordEditor).pointer is None
        assert "vertices is shown, not edited here" in app.query_one(StatusLine).text


async def test_escape_goes_back_one_step(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, helm(model_copy))
        editor = await open_field(app, pilot, "/bones/3", "name")
        assert editor.has_focus
        await pilot.press("escape")
        await pilot.pause()
        assert app.query_one(FieldList).has_focus
        await pilot.press("escape")
        await pilot.pause()
        assert tree.has_focus


async def test_leaving_a_part_takes_the_edit_in_the_editor(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, helm(model_copy))
        editor = await open_field(app, pilot, "/bones/3", "name")
        editor.value = "Left thigh"
        await pilot.pause()
        tree.select("/bones/4")
        await pilot.pause()
        assert app.session is not None
        assert app.session.value("/bones/3/name") == "Left thigh"
        assert editor.pointer is None
        assert str(app.query_one(FieldList).border_title) == "bone 4"


async def test_search_and_filter_stay_for_tables(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = await open_model(app, pilot, helm(model_copy))
        tree.focus()
        for key in ("slash", "f"):
            await pilot.press(key)
            await pilot.pause()
            assert not app.query_one("#search").has_class("-visible")
            assert "work in tables" in app.query_one(StatusLine).text
            assert tree.has_focus


async def test_the_file_tree_marks_models(model_copy: Path) -> None:
    app = MutoolsApp(LocalFileStore(), model_copy)
    async with app.run_test(size=SIZE) as pilot:
        await pilot.pause()
        glyphs = app.query_one(BmdTree).glyphs
        names = ("Item/Sword01.bmd", "Local/Text.bmd", "Gate.bmd")
        assert [glyphs[model_copy / name] for name in names] == ["△", "≡", "▤"]


async def test_the_biggest_model_opens_and_saves(tmp_path: Path, reference_data: Path) -> None:
    player = tmp_path / "Data" / "player" / "Player.bmd"
    player.parent.mkdir(parents=True)
    shutil.copyfile(reference_data / "player" / "Player.bmd", player)
    app = MutoolsApp(LocalFileStore(), player)
    async with app.run_test(size=SIZE) as pilot:
        await pilot.pause()
        tree = app.query_one(ModelTree)
        assert len(tree.nodes["/actions"].children) == 205
        assert len([part for part in tree.nodes if part.startswith("/bones/")]) == 60
        await write(app, pilot, "/bones/0", "name", "Root")
        await save(pilot)
    model = CODEC.decode(player.read_bytes(), model_file("Player.bmd"))
    assert model.bones[0].name == b"Root".ljust(32, b"\0")
