import json
from pathlib import Path

import pytest

from mubmd.adapters.storage import LocalFileStore
from mubmd.app.edits import apply_operations, set_value
from mubmd.app.queries import get
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Severity
from mubmd.core.json_types import JsonValue
from mubmd.core.patch import Change, PatchOp
from tests.app.conftest import DIALOG, GATE, MOVE_REQUIREMENTS, REFERENCE_TEXT, SKILL, SLIDE, TEXT
from tests.app.fake_store import FakeStore


def test_set_writes_once_and_is_idempotent(store: FakeStore) -> None:
    first = set_value(store, TEXT, "/texts/500", "Te fuiste", dry_run=False)
    second = set_value(store, TEXT, "/texts/500", "Te fuiste", dry_run=False)
    assert first.changed
    assert first.changes == (Change("/texts/500", "El usuario salió del juego", "Te fuiste"),)
    assert not second.changed
    assert store.writes == 1
    assert get(store, TEXT, "/texts/500").value == "Te fuiste"


def test_dry_run_writes_nothing(store: FakeStore, text_bytes: bytes) -> None:
    result = set_value(store, TEXT, "/texts/500", "Te fuiste", dry_run=True)
    assert result.dry_run
    assert result.changes == (Change("/texts/500", "El usuario salió del juego", "Te fuiste"),)
    assert store.files[TEXT] == text_bytes
    assert store.writes == 0


def test_the_reference_is_never_written(store: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        set_value(store, REFERENCE_TEXT, "/texts/500", "Te fuiste", dry_run=False)
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY


def test_a_dry_run_on_the_reference_fails_as_the_real_run_would(store: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        set_value(store, REFERENCE_TEXT, "/texts/500", "Te fuiste", dry_run=True)
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY


def test_an_emoji_is_refused_and_nothing_is_written(store: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        set_value(store, TEXT, "/texts/500", "Hola 😀", dry_run=False)
    assert caught.value.code is ErrorCode.NOT_ENCODABLE
    assert caught.value.pointer == "/texts/500"
    assert store.writes == 0


def test_a_failing_test_operation_writes_nothing(store: FakeStore) -> None:
    ops = (PatchOp("test", "/texts/500", "otro"), PatchOp("replace", "/texts/500", "Te fuiste"))
    with pytest.raises(MubmdError) as caught:
        apply_operations(store, TEXT, ops, dry_run=False)
    assert caught.value.code is ErrorCode.TEST_FAILED
    assert store.writes == 0


def test_apply_changes_several_texts_at_once(store: FakeStore) -> None:
    ops = (
        PatchOp("test", "/texts/500", "El usuario salió del juego"),
        PatchOp("replace", "/texts/500", "The user left the game"),
        PatchOp("replace", "/texts/100", "Zen!"),
    )
    result = apply_operations(store, TEXT, ops, dry_run=False)
    assert [change.pointer for change in result.changes] == ["/texts/100", "/texts/500"]
    assert store.writes == 1


def test_changing_a_printf_specifier_warns_and_still_writes(store: FakeStore) -> None:
    result = set_value(store, TEXT, "/texts/577", "Aumenta %s%% de Daño", dry_run=False)
    assert [(warning.code, warning.pointer) for warning in result.warnings] == [("PRINTF_CHANGED", "/texts/577")]
    assert store.writes == 1


def test_a_value_for_a_non_string_target_is_json(store: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        set_value(store, TEXT, "/texts", "not json", dry_run=True)
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_a_stale_file_is_not_overwritten(store: FakeStore, monkeypatch: pytest.MonkeyPatch) -> None:
    original_read = store.read

    def read_then_change(path: Path) -> bytes:
        data = original_read(path)
        store.files[path] = data[:-1] + b"\x01"
        return data

    monkeypatch.setattr(store, "read", read_then_change)
    with pytest.raises(MubmdError) as caught:
        set_value(store, TEXT, "/texts/500", "Te fuiste", dry_run=False)
    assert caught.value.code is ErrorCode.STALE_FILE


def test_an_encoding_override_reads_the_file_differently(store: FakeStore) -> None:
    value = get(store, TEXT, "/encoding", encoding="latin-1").value
    assert json.dumps(value) == '"latin-1"'


def test_an_unknown_encoding_is_invalid(store: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        get(store, TEXT, "/texts/1", encoding="klingon")
    assert caught.value.code is ErrorCode.INVALID_VALUE


def test_the_header_of_a_view_is_not_editable(store: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        set_value(store, TEXT, "/encoding", "CP1252", dry_run=False)
    assert (caught.value.code, caught.value.pointer) == (ErrorCode.INVALID_VALUE, "/encoding")
    assert store.writes == 0


def test_a_change_that_writes_the_same_bytes_is_no_change(store: FakeStore) -> None:
    same: JsonValue = {"hex": b"Zen".ljust(300, b"\0").hex().upper()}
    result = apply_operations(store, TEXT, (PatchOp("replace", "/texts/100", same),), dry_run=False)
    assert not result.changed
    assert store.writes == 0


def test_an_emoji_is_refused_in_the_chinese_table(working_copy: Path) -> None:
    chinese = working_copy / "Local" / "TKOU.bmd"
    before = chinese.read_bytes()
    with pytest.raises(MubmdError) as caught:
        set_value(LocalFileStore(), chinese, "/texts/500", "😀", dry_run=False)
    assert (caught.value.code, caught.value.pointer) == (ErrorCode.NOT_ENCODABLE, "/texts/500")
    assert chinese.read_bytes() == before


MOVE_RECORD_SIZE = 0x50
ATLANS_NAME = 4 + 10 * MOVE_RECORD_SIZE + 4
ATLANS_NAME_END = ATLANS_NAME + 32


def test_set_takes_a_text_with_a_tail_as_written(tables: FakeStore) -> None:
    before = tables.files[MOVE_REQUIREMENTS]
    result = set_value(tables, MOVE_REQUIREMENTS, "/records/10/name", "Atlans 1", dry_run=False)
    assert result.changes == (Change("/records/10/name", {"text": "Atlans", "tail": "d9cbb9"}, "Atlans 1"),)
    after = tables.files[MOVE_REQUIREMENTS]
    assert after[:ATLANS_NAME] == before[:ATLANS_NAME]
    assert after[ATLANS_NAME_END:] == before[ATLANS_NAME_END:]
    assert get(tables, MOVE_REQUIREMENTS, "/records/10/name").value == "Atlans 1"


def test_set_parses_a_number(tables: FakeStore) -> None:
    result = set_value(tables, GATE, "/records/1/level", "25", dry_run=False)
    assert result.changes == (Change("/records/1/level", 20, 25),)


@pytest.mark.parametrize(
    ("path", "pointer", "raw"),
    [
        (GATE, "/records/1/level", "300"),
        (GATE, "/records/1/level", "true"),
        (GATE, "/records/1/level", "2.5"),
        (GATE, "/records/1/level", '"25"'),
        (GATE, "/records/1/level", "veinte"),
        (SLIDE, "/records/0/speed", "NaN"),
        (SLIDE, "/records/0/speed", "Infinity"),
        (SLIDE, "/records/0/speed", "1e39"),
    ],
)
def test_set_refuses_a_value_its_field_cannot_hold(tables: FakeStore, path: Path, pointer: str, raw: str) -> None:
    with pytest.raises(MubmdError) as caught:
        set_value(tables, path, pointer, raw, dry_run=False)
    assert caught.value.code is ErrorCode.INVALID_VALUE
    assert caught.value.pointer == pointer
    assert tables.writes == 0


@pytest.mark.parametrize("raw", ["true", "1.0"])
def test_set_refuses_a_wrong_type_that_equals_the_current_value(tables: FakeStore, raw: str) -> None:
    with pytest.raises(MubmdError) as caught:
        set_value(tables, GATE, "/records/1/flag", raw, dry_run=False)
    assert caught.value.code is ErrorCode.INVALID_VALUE
    assert caught.value.pointer == "/records/1/flag"


def test_set_replaces_a_whole_list(tables: FakeStore) -> None:
    result = set_value(tables, SKILL, "/records/1/classes", "[1, 1, 0, 1, 0]", dry_run=False)
    assert result.changes == (Change("/records/1/classes/1", 0, 1),)


@pytest.mark.parametrize(
    ("pointer", "value"),
    [
        ("/records/1", {"flag": 1}),
        ("/records/1", [1, 0, 121, 232, 123, 233, 2, 0, 20]),
        ("/records/1", None),
        ("/records", []),
    ],
)
def test_a_whole_record_must_keep_its_fields(tables: FakeStore, pointer: str, value: JsonValue) -> None:
    with pytest.raises(MubmdError) as caught:
        apply_operations(tables, GATE, (PatchOp("replace", pointer, value),), dry_run=False)
    assert caught.value.code is ErrorCode.INVALID_VALUE
    assert caught.value.pointer == pointer
    assert tables.writes == 0


def test_a_list_must_keep_its_length(tables: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        apply_operations(tables, SKILL, (PatchOp("replace", "/records/1/classes", [1, 0]),), dry_run=False)
    assert caught.value.code is ErrorCode.INVALID_VALUE
    assert caught.value.pointer == "/records/1/classes"
    assert tables.writes == 0


def test_a_field_the_record_does_not_have_is_not_found(tables: FakeStore) -> None:
    with pytest.raises(MubmdError) as caught:
        apply_operations(tables, SKILL, (PatchOp("replace", "/records/1/price", 5),), dry_run=False)
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND
    assert tables.writes == 0


def test_removing_a_move_requirement_shrinks_the_file(tables: FakeStore) -> None:
    before = tables.files[MOVE_REQUIREMENTS]
    records = get(tables, MOVE_REQUIREMENTS, "/records", limit=100).value
    assert isinstance(records, list)
    result = apply_operations(tables, MOVE_REQUIREMENTS, (PatchOp("replace", "/records", records[:-1]),), dry_run=False)
    assert result.changed
    after = tables.files[MOVE_REQUIREMENTS]
    kept = 4 + 27 * MOVE_RECORD_SIZE
    assert after[:4] == bytes((27, 0, 0, 0))
    assert after[4:kept] == before[4:kept]
    assert after[kept:] == before[kept + MOVE_RECORD_SIZE :]


def test_a_value_the_client_cannot_hold_is_written_with_its_finding(tables: FakeStore) -> None:
    result = set_value(tables, DIALOG, "/records/53/answer_count", "11", dry_run=False)
    assert result.changed
    assert [(warning.code, warning.severity, warning.pointer) for warning in result.warnings] == [
        ("OUT_OF_RANGE", Severity.ERROR, "/records/53/answer_count")
    ]
