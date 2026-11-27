from random import Random

import pytest

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.findings import Finding, Severity
from mutools.core.formats.record_fields import (
    RecordField,
    describe,
    fields_bytes,
    fields_view,
    float_bytes,
    float_view,
    leaf_at,
    out_of_range,
    span,
    struct,
)
from mutools.core.json_types import JsonValue

NAME = RecordField("name", 32, "text")
ATLANS = b"Atlans\0\xd9\xcb\xb9".ljust(32, b"\0")


def refused(field: RecordField, value: JsonValue, prefix: str = "") -> MutoolsError:
    with pytest.raises(MutoolsError) as caught:
        fields_bytes((field,), {field.name: value}, "cp1252", prefix)
    return caught.value


def test_a_signed_field_reads_minus_one() -> None:
    fields = (RecordField("next", 4, "int"),)
    assert fields_view(fields, b"\xff\xff\xff\xff", "cp1252") == {"next": -1}
    assert fields_bytes(fields, {"next": -1}, "cp1252", "") == b"\xff\xff\xff\xff"


def test_a_list_field_is_a_json_list() -> None:
    fields = (RecordField("classes", 1, "uint", count=5),)
    assert span(fields[0]) == 5
    assert fields_view(fields, bytes((1, 0, 1, 0, 0)), "cp1252") == {"classes": [1, 0, 1, 0, 0]}
    assert fields_bytes(fields, {"classes": [1, 0, 1, 0, 0]}, "cp1252", "") == bytes((1, 0, 1, 0, 0))


def test_a_struct_list_names_the_place_of_a_bad_value() -> None:
    entries = struct("entries", (RecordField("kind", 1, "uint"),), count=2)
    error = refused(entries, [{"kind": 1}, {"kind": 256}], "/records/3")
    assert error.code is ErrorCode.INVALID_VALUE
    assert error.pointer == "/records/3/entries/1/kind"


def test_a_struct_reads_its_members() -> None:
    entries = struct("entries", (RecordField("kind", 1, "uint"), RecordField("dialogs", 2, "int", count=2)), count=2)
    data = bytes((1, 0x34, 0, 0xFF, 0xFF, 2, 0, 0, 1, 0))
    view = fields_view((entries,), data, "cp1252")
    assert view == {"entries": [{"kind": 1, "dialogs": [52, -1]}, {"kind": 2, "dialogs": [0, 1]}]}
    assert fields_bytes((entries,), view, "cp1252", "") == data


def test_a_float_keeps_its_bits() -> None:
    fields = (RecordField("speed", 4, "f32"),)
    assert fields_view(fields, bytes.fromhex("00002040"), "cp1252") == {"speed": 2.5}
    assert fields_bytes(fields, {"speed": 2.5}, "cp1252", "") == bytes.fromhex("00002040")
    assert fields_view(fields, bytes.fromhex("0000c07f"), "cp1252") == {"speed": {"f32": "0000c07f"}}
    assert fields_bytes(fields, {"speed": {"f32": "0000c07f"}}, "cp1252", "") == bytes.fromhex("0000c07f")


@pytest.mark.parametrize(
    ("data", "number"),
    [
        ("cdcccc3d", "0.1"),
        ("00002040", "2.5"),
        ("00000080", "-0.0"),
        ("01000000", "1e-45"),
        ("ffff7f7f", "3.4028235e+38"),
    ],
)
def test_a_float_prints_as_the_shortest_number_that_keeps_its_bits(data: str, number: str) -> None:
    view = float_view(bytes.fromhex(data))
    assert repr(view) == number
    assert float_bytes(view, "") == bytes.fromhex(data)


def test_no_finite_float_changes_a_bit() -> None:
    random = Random(4)
    for _ in range(20_000):
        data = random.randbytes(4)
        view = float_view(data)
        if isinstance(view, float):
            assert float_bytes(view, "") == data
        else:
            assert view == {"f32": data.hex()}


@pytest.mark.parametrize(
    ("field", "value"),
    [
        (RecordField("level", 1, "uint"), True),
        (RecordField("level", 1, "uint"), 1.5),
        (RecordField("level", 1, "uint"), "7"),
        (RecordField("level", 1, "uint"), 256),
        (RecordField("level", 1, "uint"), -1),
        (RecordField("level", 2, "int"), 32768),
        (RecordField("level", 2, "int"), -32769),
        (RecordField("level", 4, "f32"), float("nan")),
        (RecordField("level", 4, "f32"), float("inf")),
        (RecordField("level", 4, "f32"), 1e39),
        (RecordField("level", 4, "f32"), True),
        (RecordField("level", 4, "f32"), {"f32": "00"}),
    ],
)
def test_a_number_field_refuses_what_json_lets_through(field: RecordField, value: JsonValue) -> None:
    error = refused(field, value, "/records/0")
    assert error.code is ErrorCode.INVALID_VALUE
    assert error.pointer == "/records/0/level"


def test_a_text_with_bytes_after_its_zero_keeps_them() -> None:
    view = fields_view((NAME,), ATLANS, "cp1252")
    assert view == {"name": {"text": "Atlans", "tail": "d9cbb9"}}
    assert fields_bytes((NAME,), view, "cp1252", "") == ATLANS


def test_a_string_over_a_text_with_a_tail_drops_the_tail() -> None:
    assert fields_bytes((NAME,), {"name": "Atlans 1"}, "cp1252", "") == b"Atlans 1".ljust(32, b"\0")


def test_a_text_without_a_zero_stays_hex() -> None:
    view = fields_view((NAME,), b"\xff" * 32, "cp1252")
    assert view == {"name": {"hex": "ff" * 32}}
    assert fields_bytes((NAME,), view, "cp1252", "") == b"\xff" * 32


def test_a_text_its_encoding_cannot_read_stays_hex() -> None:
    data = b"\x81\0".ljust(32, b"\0")
    assert fields_view((NAME,), data, "cp1252") == {"name": {"hex": data.hex()}}


def test_a_text_and_tail_that_do_not_fit_are_refused() -> None:
    error = refused(NAME, {"text": "a" * 30, "tail": "0102"}, "/records/5")
    assert error.code is ErrorCode.TEXT_TOO_LONG
    assert error.pointer == "/records/5/name"


def test_a_string_needs_room_for_its_zero() -> None:
    assert refused(NAME, "a" * 32).code is ErrorCode.TEXT_TOO_LONG
    assert fields_bytes((NAME,), {"name": "a" * 31}, "cp1252", "") == b"a" * 31 + b"\0"


@pytest.mark.parametrize("value", [{"kind": 1}, {"kind": 1, "item": 2, "extra": 3}, [1, 2], None])
def test_a_struct_refuses_missing_and_extra_fields(value: JsonValue) -> None:
    entry = struct("entry", (RecordField("kind", 1, "uint"), RecordField("item", 1, "uint")))
    error = refused(entry, value, "/records/2")
    assert error.code is ErrorCode.INVALID_VALUE
    assert error.pointer == "/records/2/entry"


@pytest.mark.parametrize("value", [[1, 0, 1, 0], [1, 0, 1, 0, 0, 0], 1, {"0": 1}])
def test_a_list_refuses_another_length(value: JsonValue) -> None:
    error = refused(RecordField("classes", 1, "uint", count=5), value, "/records/2")
    assert error.code is ErrorCode.INVALID_VALUE
    assert error.pointer == "/records/2/classes"


def test_a_hex_field_takes_exactly_its_bytes() -> None:
    raw = RecordField("raw_0x1f", 2, "bytes")
    assert fields_view((raw,), b"\x01\x02", "cp1252") == {"raw_0x1f": {"hex": "0102"}}
    assert refused(raw, {"hex": "01"}).code is ErrorCode.INVALID_VALUE
    assert refused(raw, "0102").code is ErrorCode.INVALID_VALUE


def quest_fields() -> tuple[RecordField, ...]:
    entry = (RecordField("raw_0x00", 12, "bytes"), RecordField("dialogs", 2, "int", count=4))
    return (RecordField("head", 0x26, "bytes"), struct("entries", entry, count=16))


def test_describe_gives_each_leaf_its_pointer_and_offset() -> None:
    infos = describe(quest_fields(), "/records/<0..199>", "the research")
    dialogs = infos[-1]
    assert dialogs.pointer == "/records/<0..199>/entries/<0..15>/dialogs/<0..3>"
    assert dialogs.offset == 0x32
    assert dialogs.type == "int"
    assert dialogs.reference == "the research"
    assert [info.offset for info in infos] == [0, 0x26, 0x32]


def test_leaf_at_finds_a_leaf_inside_lists_and_structs() -> None:
    leaf = leaf_at(quest_fields(), ("entries", "15", "dialogs", "3"), "/records/0/entries/15/dialogs/3")
    assert (leaf.name, leaf.size, leaf.kind) == ("dialogs", 2, "int")


@pytest.mark.parametrize(
    "tokens",
    [("nope",), ("entries",), ("entries", "16", "dialogs", "0"), ("entries", "0"), ("entries", "0", "dialogs"), ()],
)
def test_leaf_at_refuses_what_is_not_a_leaf(tokens: tuple[str, ...]) -> None:
    with pytest.raises(MutoolsError) as caught:
        leaf_at(quest_fields(), tokens, "/the/pointer")
    assert caught.value.code is ErrorCode.POINTER_NOT_FOUND
    assert caught.value.pointer == "/the/pointer"


def test_out_of_range_reports_the_pointer() -> None:
    fields = (RecordField("answer_count", 4, "int", highest=10),)
    (finding,) = out_of_range(fields, {"answer_count": 11}, "/records/0")
    assert finding == Finding("OUT_OF_RANGE", Severity.ERROR, "/records/0/answer_count", finding.message)
    assert "11" in finding.message
    assert list(out_of_range(fields, {"answer_count": 10}, "/records/0")) == []


def test_out_of_range_looks_inside_lists_of_structs() -> None:
    group = struct("groups", (RecordField("count", 4, "int", highest=32),), count=2)
    findings = list(out_of_range((group,), {"groups": [{"count": -1}, {"count": 33}]}, "/records/0"))
    assert [finding.pointer for finding in findings] == ["/records/0/groups/1/count"]


def test_a_lowest_bound_is_checked_too() -> None:
    fields = (RecordField("type", 4, "int", lowest=0, highest=0xFFFF),)
    assert [finding.pointer for finding in out_of_range(fields, {"type": -1}, "/records/7")] == ["/records/7/type"]
