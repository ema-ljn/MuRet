from collections.abc import Callable
from pathlib import Path

import pytest

from mutools.app.actions import export_action, import_action
from mutools.app.emotes import EmoteEdit, add_emote, list_emotes, read_table, remove_emote
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.emote_table import AddedEmote
from tests.app.conftest import REFERENCE, TEXT, WORK
from tests.app.fake_store import FakeStore

PLAYER = WORK / "player" / "Player.bmd"
TABLE = WORK / "MuRet" / "Emotes.txt"
REFERENCE_PLAYER = REFERENCE / "player" / "Player.bmd"
WAVE = AddedEmote(205, 205, 0.32, "loop", ("wave", "/wave"))
BOW = AddedEmote(205, 205, 0.5, "once", ("bow",))
HEADER = b"MuRet emotes 1\n"


@pytest.fixture(scope="session")
def player_with_206_actions(reference_data: Path) -> bytes:
    store = FakeStore({PLAYER: (reference_data / "player" / "Player.bmd").read_bytes()})
    action = Path("/tmp/action.json")
    export_action(store, PLAYER, 194, action, dry_run=False)
    import_action(store, PLAYER, action, dry_run=False)
    return store.files[PLAYER]


@pytest.fixture
def data(player_with_206_actions: bytes, text_bytes: bytes) -> FakeStore:
    return FakeStore(
        {PLAYER: player_with_206_actions, TEXT: text_bytes, REFERENCE_PLAYER: player_with_206_actions},
        reference=REFERENCE,
    )


def add(store: FakeStore, emote: AddedEmote, *, replace_row: bool = False, dry_run: bool = False) -> EmoteEdit:
    return add_emote(store, WORK, emote, replace_row=replace_row, encoding="cp1252", dry_run=dry_run).result


def refused(call: Callable[..., object], *args: object, **options: object) -> MutoolsError:
    with pytest.raises(MutoolsError) as caught:
        call(*args, **options)
    return caught.value


def test_a_data_folder_without_the_table_has_no_emotes(data: FakeStore) -> None:
    table = read_table(data, WORK, "cp1252")

    assert (table.path, table.exists, table.emotes) == (TABLE, False, ())


def test_the_first_emote_writes_the_table_in_muret(data: FakeStore) -> None:
    edit = add(data, WAVE)

    assert edit == EmoteEdit(TABLE, 205, changed=True, dry_run=False, emotes=1)
    assert data.files[TABLE] == HEADER + b"205\t205\t0.32\tloop\twave\t/wave\n"


def test_the_table_is_found_ignoring_case(data: FakeStore) -> None:
    data.files[WORK / "muret" / "EMOTES.TXT"] = HEADER

    add(data, WAVE)

    assert (WORK / "muret" / "EMOTES.TXT") in data.files
    assert TABLE not in data.files


def test_an_emote_the_table_has_changes_nothing(data: FakeStore) -> None:
    add(data, WAVE)

    edit = add(data, WAVE)

    assert edit.changed is False
    assert data.writes == 1


def test_another_action_comes_after_the_rows_there(data: FakeStore) -> None:
    data.files[TABLE] = HEADER + b"206\t206\t1.0\tonce\tsit\n"

    add(data, WAVE)

    assert read_table(data, WORK, "cp1252").emotes[1] == WAVE


def test_changing_an_actions_emote_needs_replace(data: FakeStore) -> None:
    add(data, WAVE)

    error = refused(add, data, BOW)
    add(data, BOW, replace_row=True)

    assert (error.code, error.suggestion) == (ErrorCode.USAGE, "--replace")
    assert read_table(data, WORK, "cp1252").emotes == (BOW,)


@pytest.mark.parametrize(
    ("emote", "pointer"),
    [
        (AddedEmote(206, 205, 0.32, "loop", ("w",)), "/action"),
        (AddedEmote(205, 206, 0.32, "loop", ("w",)), "/elf_action"),
    ],
)
def test_an_action_player_bmd_does_not_have_is_invalid(data: FakeStore, emote: AddedEmote, pointer: str) -> None:
    error = refused(add, data, emote)

    assert (error.code, error.pointer) == (ErrorCode.INVALID_VALUE, pointer)
    assert error.suggestion is not None
    assert error.suggestion.startswith("mutools action import <file>/player/Player.bmd")
    assert data.writes == 0


def test_a_data_folder_without_player_bmd_is_not_found(data: FakeStore) -> None:
    del data.files[PLAYER]

    assert refused(add, data, WAVE).code is ErrorCode.FILE_NOT_FOUND


def test_a_folder_that_is_not_there_is_not_found(data: FakeStore) -> None:
    error = refused(read_table, data, Path("/nowhere"), "cp1252")

    assert error.code is ErrorCode.FILE_NOT_FOUND


def test_the_reference_folder_is_never_written(data: FakeStore) -> None:
    error = refused(add_emote, data, REFERENCE, WAVE, replace_row=False, encoding="cp1252", dry_run=True)

    assert error.code is ErrorCode.REFERENCE_READ_ONLY


def test_a_dry_run_writes_nothing(data: FakeStore) -> None:
    edit = add(data, WAVE, dry_run=True)

    assert (edit.changed, edit.dry_run) == (True, True)
    assert TABLE not in data.files


def test_an_encoding_that_does_not_exist_is_invalid(data: FakeStore) -> None:
    assert refused(read_table, data, WORK, "nope").code is ErrorCode.INVALID_VALUE


def test_a_trigger_that_holds_one_of_the_originals_is_a_warning(data: FakeStore) -> None:
    added = add_emote(
        data,
        WORK,
        AddedEmote(205, 205, 0.32, "loop", ("wave", "Thanks")),
        replace_row=False,
        encoding="cp1252",
        dry_run=False,
    )

    assert [(finding.code, finding.pointer) for finding in added.warnings] == [("TRIGGER_TAKEN", "/triggers/1")]
    assert "text 279 of Local/Text.bmd" in added.warnings[0].message


def test_the_list_warns_of_each_row_whose_trigger_is_taken(data: FakeStore) -> None:
    data.files[TABLE] = HEADER + b"205\t205\t0.32\tloop\twave\n206\t206\t0.32\tloop\tok\t^^\n"

    listed = list_emotes(data, WORK, "cp1252")

    assert len(listed.result.emotes) == 2
    assert [finding.pointer for finding in listed.warnings] == ["/emotes/1/triggers/1"]


def test_removing_an_emote_keeps_the_others(data: FakeStore) -> None:
    data.files[TABLE] = HEADER + b"205\t205\t0.32\tloop\twave\n206\t206\t1.0\tonce\tsit\n"

    edit = remove_emote(data, WORK, 205, encoding="cp1252", dry_run=False)

    assert (edit.changed, edit.emotes) == (True, 1)
    assert data.files[TABLE] == HEADER + b"206\t206\t1.0\tonce\tsit\n"


def test_removing_an_emote_that_is_not_there_names_the_ones_that_are(data: FakeStore) -> None:
    data.files[TABLE] = HEADER + b"206\t206\t1.0\tonce\tsit\n"

    error = refused(remove_emote, data, WORK, 205, encoding="cp1252", dry_run=False)

    assert (error.code, error.suggestion) == (ErrorCode.POINTER_NOT_FOUND, "mutools emote list <file>")
    assert error.message.endswith("the emotes are 206")
