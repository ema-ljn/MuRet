import pytest

from mubmd.app.session import EditSession
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.patch import Change
from tests.app.conftest import REFERENCE_TEXT, TEXT
from tests.app.fake_store import FakeStore


def test_a_new_session_is_clean(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    assert not session.dirty
    assert session.value("/texts/500") == "El usuario salió del juego"
    summary = session.summary
    assert (summary.cipher, summary.records, summary.used_records) == ("XOR3", 2048, 1645)
    assert session.format_name == "text"
    assert session.original == session.view
    assert not session.read_only


def test_set_records_a_change(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    session.set("/texts/500", "Te fuiste")
    assert session.dirty
    assert session.changes == (Change("/texts/500", "El usuario salió del juego", "Te fuiste"),)
    assert store.writes == 0


def test_an_invalid_value_is_refused(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    with pytest.raises(MubmdError) as caught:
        session.set("/texts/500", "😀")
    assert caught.value.code is ErrorCode.NOT_ENCODABLE
    assert not session.dirty


def test_undo_and_redo_walk_the_history(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    session.set("/texts/500", "uno")
    session.set("/texts/500", "dos")
    assert session.undo()
    assert session.value("/texts/500") == "uno"
    assert session.undo()
    assert session.value("/texts/500") == "El usuario salió del juego"
    assert not session.undo()
    assert session.redo()
    assert session.value("/texts/500") == "uno"
    session.set("/texts/100", "Zen!")
    assert not session.redo()


def test_save_writes_and_starts_clean(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    session.set("/texts/500", "Te fuiste")
    result = session.save()
    assert result.changed
    assert store.writes == 1
    assert not session.dirty
    assert not session.undo()


def test_the_reference_session_is_read_only(store: FakeStore) -> None:
    session = EditSession.open(store, REFERENCE_TEXT)
    assert session.read_only
    session.set("/texts/500", "x")
    with pytest.raises(MubmdError) as caught:
        session.save()
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY


def test_measure_counts_bytes_in_the_file_encoding(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    measure = session.measure("/texts/500", "El usuario salió del juego")
    assert (measure.size, measure.limit, measure.error) == (26, 300, None)
    bad = session.measure("/texts/500", "😀")
    assert bad.error is not None
    assert bad.error.code is ErrorCode.NOT_ENCODABLE
