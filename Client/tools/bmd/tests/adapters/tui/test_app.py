import hashlib
import shutil
from pathlib import Path

from textual.pilot import Pilot

from mubmd.adapters.storage import LocalFileStore
from mubmd.adapters.tui.app import MubmdApp
from mubmd.adapters.tui.widgets.bmd_tree import BmdTree, display_order
from mubmd.adapters.tui.widgets.diff_screen import DiffScreen
from mubmd.adapters.tui.widgets.record_editor import RecordEditor
from mubmd.adapters.tui.widgets.record_table_view import RecordTableView
from mubmd.adapters.tui.widgets.status_line import StatusLine
from mubmd.adapters.tui.widgets.title_bar import TitleBar
from mubmd.app.edits import set_value
from mubmd.core.ciphers.xor3 import xor3

SIZE = (100, 30)


def text_of(path: Path) -> Path:
    return path / "Local" / "Text.bmd"


async def open_text(app: MubmdApp, pilot: Pilot[None], folder: Path) -> RecordTableView:
    await app.open_path(text_of(folder))
    await pilot.pause()
    return app.query_one(RecordTableView)


async def edit(app: MubmdApp, pilot: Pilot[None], index: int, value: str) -> None:
    table = app.query_one(RecordTableView)
    table.move_cursor(row=table.get_row_index(str(index)))
    table.focus()
    await pilot.press("enter")
    app.query_one(RecordEditor).value = value
    await pilot.pause()
    await pilot.press("enter")
    await pilot.pause()


def records(path: Path) -> list[bytes]:
    plain = xor3(path.read_bytes())
    return [plain[start : start + 300] for start in range(0, 614_400, 300)]


def test_the_tree_lists_folders_before_files_without_case() -> None:
    root = Path("/Data")
    files = [root / "Gate.bmd", root / "local" / "text.bmd", root / "Local2" / "a.bmd", root / "local" / "Item.bmd"]
    assert display_order(root, files) == [
        root / "local" / "Item.bmd",
        root / "local" / "text.bmd",
        root / "Local2" / "a.bmd",
        root / "Gate.bmd",
    ]


async def test_selecting_a_file_in_the_tree_opens_it(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        tree = app.query_one(BmdTree)
        tree.focus()
        await pilot.press("down", "enter", "down", "enter")
        await pilot.pause()
        assert app.query_one(RecordTableView).row_count == 2048
        assert "Local/Text.bmd" in str(app.query_one(RecordTableView).border_title)


async def test_opening_a_text_table(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_text(app, pilot, working_copy)
        assert table.row_count == 2048
        assert "text · XOR3 · 1645/2048 used" in str(table.border_title)
        assert "working copy" in app.query_one(TitleBar).text


async def test_a_file_given_as_root_opens_at_once(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), text_of(working_copy))
    async with app.run_test(size=SIZE) as pilot:
        await pilot.pause()
        assert app.query_one(RecordTableView).row_count == 2048


async def test_search_filters_and_enter_edits(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_text(app, pilot, working_copy)
        table.focus()
        await pilot.press("slash", *"usuario")
        await pilot.pause()
        assert 0 < table.row_count < 2048
        table.move_cursor(row=table.get_row_index("500"))
        table.focus()
        await pilot.press("enter")
        await pilot.pause()
        editor = app.query_one(RecordEditor)
        assert editor.value == "El usuario salió del juego"
        assert editor.border_subtitle == "26/300 bytes"
        assert app.query_one(StatusLine).text.startswith("/texts/500")


async def test_an_emoji_marks_the_editor_and_nothing_is_saved(working_copy: Path) -> None:
    before = text_of(working_copy).read_bytes()
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_text(app, pilot, working_copy)
        table.move_cursor(row=500)
        table.focus()
        await pilot.press("enter")
        editor = app.query_one(RecordEditor)
        editor.value = "Hola 😀"
        await pilot.pause()
        assert editor.has_class("-invalid")
        await pilot.press("ctrl+s")
        await pilot.pause()
        assert not isinstance(app.screen, DiffScreen)
        assert "NOT_ENCODABLE" in app.query_one(StatusLine).text
    assert text_of(working_copy).read_bytes() == before


async def test_undo_and_redo_restore_the_texts(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_text(app, pilot, working_copy)
        await edit(app, pilot, 500, "uno")
        await edit(app, pilot, 500, "dos")
        assert "2 unsaved" not in app.query_one(TitleBar).text
        assert "1 unsaved" in app.query_one(TitleBar).text
        table.focus()
        await pilot.press("ctrl+z")
        await pilot.pause()
        assert table.value_at(500) == "uno"
        await pilot.press("ctrl+z")
        await pilot.pause()
        assert table.value_at(500) == "El usuario salió del juego"
        await pilot.press("ctrl+y")
        await pilot.pause()
        assert table.value_at(500) == "uno"


async def test_save_shows_the_diff_and_writes_after_confirming(working_copy: Path) -> None:
    before = records(text_of(working_copy))
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_text(app, pilot, working_copy)
        await edit(app, pilot, 500, "Te fuiste")
        await pilot.press("ctrl+s")
        await pilot.pause()
        assert isinstance(app.screen, DiffScreen)
        assert len(app.screen.changes) == 1
        await pilot.press("y")
        await pilot.pause()
        assert "working copy" in app.query_one(TitleBar).text
        assert "unsaved" not in app.query_one(TitleBar).text
    after = records(text_of(working_copy))
    assert [index for index in range(2048) if before[index] != after[index]] == [500]
    assert after[500].startswith(b"Te fuiste\0")


async def test_a_file_changed_on_disk_is_not_overwritten(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_text(app, pilot, working_copy)
        other = text_of(working_copy).read_bytes()[:-1] + b"\x01"
        text_of(working_copy).write_bytes(other)
        await edit(app, pilot, 500, "Te fuiste")
        await pilot.press("ctrl+s")
        await pilot.pause()
        await pilot.press("y")
        await pilot.pause()
        assert "STALE_FILE" in app.query_one(StatusLine).text
    assert text_of(working_copy).read_bytes() == other


async def test_the_reference_is_read_only_and_points_to_a_working_copy(tmp_path: Path, reference_data: Path) -> None:
    data = tmp_path / "Data"
    (data / "Local").mkdir(parents=True)
    shutil.copyfile(text_of(reference_data), text_of(data))
    digest = hashlib.sha256(text_of(data).read_bytes()).hexdigest()
    (tmp_path / "Data.sha256").write_text(f"{digest}  ./Local/Text.bmd\n")
    app = MubmdApp(LocalFileStore(), data)
    async with app.run_test(size=SIZE) as pilot:
        await open_text(app, pilot, data)
        assert "read-only" in app.query_one(TitleBar).text
        await pilot.press("ctrl+s")
        await pilot.pause()
        assert not isinstance(app.screen, DiffScreen)
        assert "mubmd workspace init" in app.query_one(StatusLine).text


async def test_a_read_only_file_is_not_edited(tmp_path: Path, reference_data: Path) -> None:
    reference = tmp_path / "ref" / "Data"
    (reference / "Local").mkdir(parents=True)
    shutil.copyfile(text_of(reference_data), text_of(reference))
    (tmp_path / "ref" / "Data.sha256").write_text("")
    work = tmp_path / "work" / "Data"
    (work / "Local").mkdir(parents=True)
    text_of(work).symlink_to(text_of(reference))
    app = MubmdApp(LocalFileStore(), work)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_text(app, pilot, work)
        assert "Local/Text.bmd" in str(table.border_title)
        table.move_cursor(row=500)
        table.focus()
        await pilot.press("enter")
        await pilot.pause()
        assert app.query_one(RecordEditor).disabled
        assert "mubmd workspace init" in app.query_one(StatusLine).text


async def test_the_tree_marks_the_open_file_and_its_unsaved_changes(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_text(app, pilot, working_copy)
        tree = app.query_one(BmdTree)

        def line_of(name: str) -> str:
            lines = [tree.render_line(y).text for y in range(tree.last_line + 1)]
            return next(line for line in lines if name in line)

        assert "◆ Text.bmd" in line_of("Text.bmd")
        assert "◆" not in line_of("TKOU.bmd")
        assert "●" not in line_of("Text.bmd")
        await edit(app, pilot, 500, "Te fuiste")
        assert "●" in line_of("Text.bmd")
        assert "●" not in line_of("TKOU.bmd")


async def test_the_editor_warns_of_a_text_that_fills_its_record(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_text(app, pilot, working_copy)
        table.move_cursor(row=100)
        table.focus()
        await pilot.press("enter")
        editor = app.query_one(RecordEditor)
        editor.value = "a" * 300
        await pilot.pause()
        assert not editor.has_class("-invalid")
        assert "TEXT_FILLS_RECORD" in str(editor.border_subtitle)


async def test_the_save_screen_shows_the_warnings_of_the_changes(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_text(app, pilot, working_copy)
        await edit(app, pilot, 577, "Aumenta %s%% de Daño")
        await pilot.press("ctrl+s")
        await pilot.pause()
        assert isinstance(app.screen, DiffScreen)
        assert [warning.code for warning in app.screen.warnings] == ["PRINTF_CHANGED"]
        await pilot.press("y")
        await pilot.pause()
        assert "PRINTF_CHANGED" in app.query_one(StatusLine).text


async def test_saving_keeps_the_cursor_and_says_what_was_saved(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_text(app, pilot, working_copy)
        await edit(app, pilot, 1500, "Te fuiste")
        await pilot.press("ctrl+s")
        await pilot.pause()
        await pilot.press("y")
        await pilot.pause()
        assert table.index_at_cursor() == 1500
        assert app.query_one(StatusLine).text.startswith("saved 1 change")


async def test_a_stale_file_is_read_again_keeping_the_edits(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_text(app, pilot, working_copy)
        set_value(LocalFileStore(), text_of(working_copy), "/texts/100", "Zen!", dry_run=False)
        await edit(app, pilot, 500, "Te fuiste")
        await pilot.press("ctrl+s")
        await pilot.pause()
        await pilot.press("y")
        await pilot.pause()
        assert "STALE_FILE" in app.query_one(StatusLine).text
        table.focus()
        await pilot.press("r")
        await pilot.pause()
        assert table.value_at(100) == "Zen!"
        await pilot.press("ctrl+s")
        await pilot.pause()
        await pilot.press("y")
        await pilot.pause()
    after = records(text_of(working_copy))
    assert after[100].startswith(b"Zen!\0")
    assert after[500].startswith(b"Te fuiste\0")


async def test_quit_asks_again_after_a_new_edit(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        table = await open_text(app, pilot, working_copy)
        await edit(app, pilot, 500, "uno")
        table.focus()
        await pilot.press("q")
        await pilot.pause()
        await edit(app, pilot, 501, "dos")
        table.focus()
        await pilot.press("q")
        await pilot.pause()
        assert "unsaved changes" in app.query_one(StatusLine).text


async def test_square_brackets_in_a_path_are_shown_as_they_are(tmp_path: Path, working_copy: Path) -> None:
    folder = tmp_path / "Data [b]"
    shutil.copytree(working_copy, folder)
    app = MubmdApp(LocalFileStore(), folder)
    async with app.run_test(size=(200, 30)) as pilot:
        await open_text(app, pilot, folder)
        assert "Data [b]" in app.query_one(TitleBar).render_line(0).text
        assert app.query_one(BmdTree).border_title == "Data \\[b]"
        await edit(app, pilot, 500, "Te fuiste")
        await pilot.press("ctrl+s")
        await pilot.pause()
        assert isinstance(app.screen, DiffScreen)
        await pilot.press("y")
        await pilot.pause()
        assert "Data [b]" in app.query_one(StatusLine).render_line(0).text


async def test_undo_shows_the_text_again_in_the_editor(working_copy: Path) -> None:
    app = MubmdApp(LocalFileStore(), working_copy)
    async with app.run_test(size=SIZE) as pilot:
        await open_text(app, pilot, working_copy)
        await edit(app, pilot, 500, "Te fuiste")
        await pilot.press("ctrl+z")
        await pilot.pause()
        assert app.query_one(RecordEditor).value == "El usuario salió del juego"
