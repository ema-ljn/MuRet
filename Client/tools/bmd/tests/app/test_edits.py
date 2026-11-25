import json
from pathlib import Path

import pytest

from mubmd.app.edits import apply_operations, set_value
from mubmd.app.queries import get
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.patch import Change, PatchOp
from tests.app.conftest import REFERENCE_TEXT, TEXT
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
