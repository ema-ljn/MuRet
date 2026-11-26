import pytest

from mubmd.app.rows import RowFilter, findings_by_row, row_label, row_matches, row_shown, row_text, rows_key
from mubmd.core.findings import Finding, Severity
from mubmd.core.json_types import JsonObject, JsonValue

DIALOG: JsonObject = {"text": "Hola", "answer_count": 1, "next": [52], "answers": ["Please repeat the story.", ""]}
GATE: JsonObject = {"flag": 1, "map": 0, "x1": 121, "y1": 232}
TAILED: JsonObject = {"text": "Atlans", "tail": "d9cbb9"}
BLANK: JsonObject = {"kind": 0, "text": "", "raw_0x21": {"hex": "0000"}}
NO_SET: JsonObject = {"sets": [255, 255, 255], "raw_0x03": {"hex": "ff"}}
COUNT = Finding("OUT_OF_RANGE", Severity.ERROR, "/records/3/answer_count", "11 is too many")
FILLED = Finding("TEXT_FILLS_RECORD", Severity.WARNING, "/texts/7", "no 0")
CHECKSUM = Finding("CHECKSUM_MISMATCH", Severity.WARNING, "/stored_checksum", "differs")


def test_the_rows_of_a_view_are_its_texts_or_its_records() -> None:
    assert rows_key({"format": "text", "texts": []}) == "texts"
    assert rows_key({"format": "gate", "records": []}) == "records"


def test_the_label_of_a_record_is_its_first_text() -> None:
    assert row_label(DIALOG) == "Hola"
    assert row_label({"number": 1, "name": "", "search_name": "Arena"}) == "Arena"


def test_the_label_of_a_record_without_texts_lists_its_values() -> None:
    assert row_label(GATE) == "flag 1 · map 0 · x1 121 · y1 232"
    assert row_label({"sets": [3, 0, 0], "raw_0x03": {"hex": "00"}}) == "sets/0 3 · sets/1 0 · sets/2 0"


def test_the_label_of_a_text_with_a_tail_is_its_text() -> None:
    assert row_label(TAILED) == "Atlans"
    assert row_label({"number": 11, "name": TAILED}) == "Atlans"


@pytest.mark.parametrize("value", [BLANK, "", {"hex": "00ff"}])
def test_a_row_with_nothing_to_read_has_no_label(value: JsonValue) -> None:
    assert row_label(value) == ""


def test_a_row_matches_any_of_its_texts_without_case() -> None:
    assert row_matches(DIALOG, "STORY")
    assert row_matches("El usuario salió", "usuario")
    assert row_matches({"name": TAILED}, "atl")
    assert not row_matches(DIALOG, "52")
    assert not row_matches({"name": TAILED}, "d9cbb9")
    assert not row_matches({"hex": "abcd"}, "abcd")


def test_the_text_of_a_row_is_searched_without_case() -> None:
    assert "please repeat the story." in row_text(DIALOG)
    assert "hola" in row_text(DIALOG)


def test_every_row_is_shown_without_a_filter() -> None:
    assert row_shown("/records/3", BLANK, BLANK, (), RowFilter.ALL)


def test_the_used_filter_hides_a_blank_row() -> None:
    assert not row_shown("/records/3", BLANK, BLANK, (), RowFilter.USED)
    assert row_shown("/records/0", NO_SET, NO_SET, (), RowFilter.USED)
    assert row_shown("/records/10", {"name": {"text": "", "tail": "65"}}, None, (), RowFilter.USED)
    assert not row_shown("/texts/0", "", "", (), RowFilter.USED)


def test_the_changed_filter_keeps_what_differs_from_the_file() -> None:
    assert row_shown("/texts/5", "nuevo", "viejo", (), RowFilter.CHANGED)
    assert not row_shown("/texts/5", "igual", "igual", (), RowFilter.CHANGED)


def test_the_findings_filter_keeps_a_row_with_a_finding_at_or_under_it() -> None:
    findings = (COUNT, FILLED, CHECKSUM)
    assert row_shown("/records/3", DIALOG, DIALOG, findings, RowFilter.FINDINGS)
    assert row_shown("/texts/7", "x", "x", findings, RowFilter.FINDINGS)
    assert not row_shown("/records/30", DIALOG, DIALOG, findings, RowFilter.FINDINGS)
    assert not row_shown("/texts/70", "x", "x", findings, RowFilter.FINDINGS)


def test_findings_are_grouped_by_their_row() -> None:
    assert findings_by_row((COUNT, FILLED, CHECKSUM)) == {"/records/3": (COUNT,), "/texts/7": (FILLED,)}
