import pytest

from mutools.core.ciphers.xor3 import xor3
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.record_fields import RecordField
from mutools.core.formats.record_table_format import RecordTableFormat, RecordTableSchema
from mutools.core.json_types import JsonValue
from mutools.core.ports import CatalogFile, ValueCheck

FIELDS = (RecordField("name", 4, "text"), RecordField("value", 4, "int", highest=10))
FILE = CatalogFile("pairs.bmd", "cp1252", read_by_client=True)
RECORD_SIZE = 8
TRAILING = bytes(range(1, 6))


def table(count: int | None, checksum_key: int | None = None, section_size: int | None = None) -> RecordTableFormat:
    return RecordTableFormat(
        RecordTableSchema(
            name="pairs",
            count=count,
            record_size=RECORD_SIZE,
            fields=FIELDS,
            checksum_key=checksum_key,
            reference="the research",
            section_size=section_size,
        )
    )


FIXED = table(2)
COUNTED = table(None)


def record(name: bytes, value: int) -> bytes:
    return xor3(name.ljust(4, b"\0") + value.to_bytes(4, "little", signed=True))


def counted_file(count: int, records: int) -> bytes:
    body = b"".join(record(b"abc", index) for index in range(records))
    return count.to_bytes(4, "little", signed=True) + body


def test_a_counted_table_reads_as_many_records_as_its_count() -> None:
    data = counted_file(2, 3)
    document = COUNTED.decode(data, FILE)
    assert len(document.records) == 2
    assert document.trailing == data[-RECORD_SIZE:]
    assert COUNTED.encode(document) == data


def test_a_counted_table_writes_the_length_of_its_list() -> None:
    data = counted_file(2, 2) + TRAILING
    view = COUNTED.to_view(COUNTED.decode(data, FILE))
    records = view["records"]
    assert isinstance(records, list)
    records.append({"name": "new", "value": 7})
    encoded = COUNTED.encode(COUNTED.from_view(view))
    assert encoded[:4] == bytes((3, 0, 0, 0))
    assert encoded == encoded[:4] + data[4:-5] + record(b"new", 7) + TRAILING


@pytest.mark.parametrize("data", [counted_file(-1, 2), counted_file(3, 2), counted_file(0x7FFFFFFF, 2)])
def test_a_count_the_file_cannot_hold_is_corrupt(data: bytes) -> None:
    with pytest.raises(MutoolsError) as caught:
        COUNTED.decode(data, FILE)
    assert caught.value.code is ErrorCode.CORRUPT_FILE
    count = int.from_bytes(data[:4], "little", signed=True)
    assert str(count) in caught.value.message
    assert "holds 2" in caught.value.message


def test_a_counted_table_needs_its_count() -> None:
    with pytest.raises(MutoolsError) as caught:
        COUNTED.decode(b"\x01\x00", FILE)
    assert caught.value.code is ErrorCode.CORRUPT_FILE


def test_a_table_without_a_checksum_has_none_in_its_view() -> None:
    data = record(b"abc", 1) + record(b"de", 2) + TRAILING
    document = FIXED.decode(data, FILE)
    view = FIXED.to_view(document)
    assert list(view) == ["format_version", "format", "encoding", "records", "trailing"]
    assert view["records"] == [{"name": "abc", "value": 1}, {"name": "de", "value": 2}]
    assert view["trailing"] == TRAILING.hex()
    assert FIXED.encode(FIXED.from_view(view)) == data


def test_a_fixed_table_refuses_another_number_of_records() -> None:
    view = FIXED.to_view(FIXED.decode(record(b"abc", 1) + record(b"de", 2), FILE))
    records = view["records"]
    assert isinstance(records, list)
    records.append({"name": "new", "value": 7})
    with pytest.raises(MutoolsError) as caught:
        FIXED.from_view(view)
    assert caught.value.code is ErrorCode.INVALID_VALUE
    assert caught.value.pointer == "/records"


def test_the_file_size_counts_the_checksum_word() -> None:
    assert FIXED.file_size == 2 * RECORD_SIZE
    assert table(2, checksum_key=0xE2F1).file_size == 2 * RECORD_SIZE + 4
    assert COUNTED.file_size is None


def test_check_reports_a_value_the_client_cannot_hold() -> None:
    document = FIXED.decode(record(b"abc", 10) + record(b"de", 11), FILE)
    findings = FIXED.check(document, None)
    assert [(finding.code, finding.severity, finding.pointer) for finding in findings] == [
        ("OUT_OF_RANGE", "error", "/records/1/value")
    ]


def test_check_keeps_the_checksum_finding_beside_the_range_one() -> None:
    codec = table(2, checksum_key=0xE2F1)
    document = codec.decode(record(b"abc", 10) + record(b"de", 11) + bytes(4), FILE)
    assert [finding.code for finding in codec.check(document, None)] == ["CHECKSUM_MISMATCH", "OUT_OF_RANGE"]


DOCUMENT = FIXED.decode(record(b"abc", 1) + record(b"de", 2), FILE)


def test_check_value_refuses_what_set_would_refuse() -> None:
    with pytest.raises(MutoolsError) as caught:
        FIXED.check_value("/records/1/name", "abcd", DOCUMENT)
    assert caught.value.code is ErrorCode.TEXT_TOO_LONG
    assert caught.value.pointer == "/records/1/name"


def test_check_value_gives_a_text_the_bytes_it_may_hold() -> None:
    assert FIXED.check_value("/records/1/name", "abc", DOCUMENT) == ValueCheck(3, ())


def test_check_value_reports_a_value_the_client_cannot_hold() -> None:
    result = FIXED.check_value("/records/1/value", 11, DOCUMENT)
    assert result.limit is None
    assert [(finding.code, finding.pointer) for finding in result.warnings] == [("OUT_OF_RANGE", "/records/1/value")]
    assert FIXED.check_value("/records/1/value", 10, DOCUMENT) == ValueCheck(None, ())


@pytest.mark.parametrize(
    ("pointer", "value"),
    [
        ("/records/9/name", "abc"),
        ("/records/1/nope", 1),
        ("/records/1", {"name": "abc", "value": 1}),
        ("/records", []),
        ("/encoding", "cp949"),
        ("/trailing", ""),
        ("", {}),
    ],
)
def test_check_value_needs_a_leaf_of_a_record_the_table_has(pointer: str, value: JsonValue) -> None:
    with pytest.raises(MutoolsError) as caught:
        FIXED.check_value(pointer, value, DOCUMENT)
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND
    assert caught.value.pointer == pointer


def test_check_value_counts_the_records_of_a_counted_table() -> None:
    document = COUNTED.decode(counted_file(3, 3), FILE)
    assert COUNTED.check_value("/records/2/name", "abc", document) == ValueCheck(3, ())
    with pytest.raises(MutoolsError) as caught:
        COUNTED.check_value("/records/3/name", "abc", document)
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND


def test_the_fields_of_a_fixed_table_name_its_range_of_records() -> None:
    assert [info.pointer for info in FIXED.describe_fields()] == ["/records/<0..1>/name", "/records/<0..1>/value"]


def test_the_fields_of_a_counted_table_name_no_range() -> None:
    assert [info.pointer for info in COUNTED.describe_fields()] == ["/records/<n>/name", "/records/<n>/value"]


def test_the_summary_carries_the_section_size() -> None:
    document = FIXED.decode(record(b"abc", 1) + record(b"", 0), FILE)
    summary = table(2, section_size=512).summarize(document)
    assert (summary.records, summary.used_records, summary.section_size) == (2, 1, 512)
    assert FIXED.summarize(document).section_size is None
