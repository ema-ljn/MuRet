from pathlib import Path

import pytest

from mutools.app.edits import set_value
from mutools.app.session import EditSession
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.patch import Change
from tests.app.conftest import DIALOG, GATE, MOVE_REQUIREMENTS, REFERENCE_TEXT, TEXT, WORK
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
    with pytest.raises(MutoolsError) as caught:
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
    with pytest.raises(MutoolsError) as caught:
        session.save()
    assert caught.value.code is ErrorCode.REFERENCE_READ_ONLY


def test_measure_counts_bytes_in_the_file_encoding(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    measure = session.measure("/texts/500", "El usuario salió del juego")
    assert (measure.size, measure.limit, measure.error) == (26, 300, None)
    bad = session.measure("/texts/500", "😀")
    assert bad.error is not None
    assert bad.error.code is ErrorCode.NOT_ENCODABLE


def test_item_name_measure_reserves_one_byte_for_the_terminator(reference_data: Path) -> None:
    item = WORK / "Local" / "item.bmd"
    store = FakeStore({item: (reference_data / "Local" / "item.bmd").read_bytes()})
    session = EditSession.open(store, item)
    measure = session.measure("/records/7190/name", "é" * 29)
    assert (measure.size, measure.limit, measure.error) == (29, 29, None)
    too_long = session.measure("/records/7190/name", "é" * 30)
    assert (too_long.size, too_long.limit) == (30, 29)
    assert too_long.error is not None
    assert too_long.error.code is ErrorCode.TEXT_TOO_LONG


def test_reload_reads_the_file_again_and_keeps_the_edits(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    session.set("/texts/500", "Te fuiste")
    set_value(store, TEXT, "/texts/100", "Zen!", dry_run=False)
    with pytest.raises(MutoolsError) as caught:
        session.save()
    assert caught.value.code is ErrorCode.STALE_FILE
    session.reload()
    assert session.value("/texts/100") == "Zen!"
    assert session.changes == (Change("/texts/500", "El usuario salió del juego", "Te fuiste"),)
    assert session.save().changed


def test_the_warnings_of_a_session_are_those_of_its_changes(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    session.set("/texts/577", "Aumenta %s%% de Daño")
    assert [(warning.code, warning.pointer) for warning in session.warnings] == [("PRINTF_CHANGED", "/texts/577")]


def test_measure_warns_of_a_text_that_fills_its_record(store: FakeStore) -> None:
    measure = EditSession.open(store, TEXT).measure("/texts/100", "a" * 300)
    assert measure.error is None
    assert [warning.code for warning in measure.warnings] == ["TEXT_FILLS_RECORD"]


def test_measure_of_a_number_has_no_byte_limit(tables: FakeStore) -> None:
    measure = EditSession.open(tables, GATE).measure("/records/1/level", 25)
    assert (measure.size, measure.limit, measure.error, measure.warnings) == (None, None, None, ())


def test_measure_of_a_number_its_field_cannot_hold_is_an_error(tables: FakeStore) -> None:
    measure = EditSession.open(tables, GATE).measure("/records/1/level", 300)
    assert measure.error is not None
    assert measure.error.code is ErrorCode.INVALID_VALUE


def test_measure_of_a_text_field_gives_its_bytes_and_its_room(tables: FakeStore) -> None:
    measure = EditSession.open(tables, MOVE_REQUIREMENTS).measure("/records/10/name", "Atlans 1")
    assert (measure.size, measure.limit, measure.error) == (8, 31, None)


def test_measure_reports_a_value_the_client_cannot_hold(tables: FakeStore) -> None:
    measure = EditSession.open(tables, DIALOG).measure("/records/53/answer_count", 11)
    assert measure.error is None
    assert [(warning.code, warning.pointer) for warning in measure.warnings] == [
        ("OUT_OF_RANGE", "/records/53/answer_count")
    ]


def test_parse_reads_a_number_as_json_and_a_text_as_written(tables: FakeStore) -> None:
    assert EditSession.open(tables, GATE).parse("/records/1/level", "25") == 25
    assert EditSession.open(tables, MOVE_REQUIREMENTS).parse("/records/10/name", "25") == "25"


def test_parse_of_a_bad_number_is_an_error(tables: FakeStore) -> None:
    with pytest.raises(MutoolsError) as caught:
        EditSession.open(tables, GATE).parse("/records/1/level", "abc")
    assert caught.value.code is ErrorCode.INVALID_VALUE
    assert caught.value.pointer == "/records/1/level"


def test_findings_lists_every_finding_of_the_view(tables: FakeStore) -> None:
    session = EditSession.open(tables, DIALOG)
    assert not session.findings
    session.set("/records/53/answer_count", 11)
    assert [(finding.code, finding.pointer) for finding in session.findings] == [
        ("OUT_OF_RANGE", "/records/53/answer_count")
    ]


def test_findings_include_those_the_file_already_had(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    assert [(finding.code, finding.pointer) for finding in session.findings] == [("TEXT_FILLS_RECORD", "/texts/7")]
    assert session.warnings == ()


def test_a_changed_view_is_one_step_of_the_history(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    texts = session.view["texts"]
    assert isinstance(texts, list)

    assert session.change({**session.view, "texts": ["Te fuiste", *texts[1:]]})
    assert session.value("/texts/0") == "Te fuiste"
    assert session.undo()
    assert not session.dirty


def test_a_view_with_no_difference_is_no_change(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)

    assert not session.change(dict(session.view))
    assert not session.undo()


def test_a_changed_view_the_file_cannot_hold_is_refused(store: FakeStore) -> None:
    session = EditSession.open(store, TEXT)
    texts = session.view["texts"]
    assert isinstance(texts, list)

    with pytest.raises(MutoolsError) as caught:
        session.change({**session.view, "texts": ["😀", *texts[1:]]})
    assert caught.value.code is ErrorCode.NOT_ENCODABLE
    assert not session.dirty
