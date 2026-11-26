from collections.abc import Iterator
from pathlib import Path

import pytest

from mubmd.core.catalog import all_formats, identify
from mubmd.core.formats.record_fields import RecordField, span
from mubmd.core.formats.record_table_format import RecordTableFormat
from mubmd.core.json_types import JsonObject, JsonValue
from tests.conftest import TABLE_FILES, TEXT_FILES

RECORD_TABLES = tuple(name for name in TABLE_FILES if name not in TEXT_FILES)
HYPERION_TAIL = b"'Dark Stone'\0\0peror'!\0n".hex()


def records_of(reference_data: Path, name: str) -> list[JsonValue]:
    data = (reference_data / name).read_bytes()
    found = identify(name, data)
    records = found.format.codec.to_view(found.format.codec.decode(data, found.file))["records"]
    assert isinstance(records, list)
    return records


def test_every_file_of_the_catalog_is_in_the_reference(reference_data: Path) -> None:
    catalog = sorted(file.name for spec in all_formats() for file in spec.files)
    assert sorted(Path(name).name for name in TABLE_FILES) == catalog
    assert all((reference_data / name).is_file() for name in TABLE_FILES)


@pytest.mark.parametrize("name", RECORD_TABLES)
def test_a_table_round_trips(reference_data: Path, name: str) -> None:
    data = (reference_data / name).read_bytes()
    found = identify(name, data)
    codec = found.format.codec
    document = codec.decode(data, found.file)
    assert codec.encode(document) == data
    assert codec.from_view(codec.to_view(document)) == document


@pytest.mark.parametrize(
    ("name", "records", "used"),
    [
        ("Local/skill.bmd", 256, 73),
        ("Gate.bmd", 256, 125),
        ("Local/Filter.bmd", 1000, 0),
        ("Local/FilterName.bmd", 1000, 217),
        ("Local/itemsettype.bmd", 8192, 8192),
        ("Local/itemsetoption.bmd", 64, 64),
        ("Local/credit.bmd", 400, 244),
    ],
)
def test_a_table_has_the_records_the_research_counts(reference_data: Path, name: str, records: int, used: int) -> None:
    data = (reference_data / name).read_bytes()
    found = identify(name, data)
    summary = found.format.codec.summarize(found.format.codec.decode(data, found.file))
    assert (summary.records, summary.used_records) == (records, used)


@pytest.mark.parametrize(
    "name",
    [
        "Local/skill.bmd",
        "Local/Skilltest.bmd",
        "Local/Filter.bmd",
        "Local/FilterName.bmd",
        "Local/itemsettype.bmd",
        "Local/itemsettypetest.bmd",
        "Local/itemsetoption.bmd",
        "Local/itemsetoptiontest.bmd",
    ],
)
def test_the_checksums_the_client_checks_match(reference_data: Path, name: str) -> None:
    data = (reference_data / name).read_bytes()
    found = identify(name, data)
    assert found.format.codec.check(found.format.codec.decode(data, found.file), None) == ()


KRIS: JsonObject = {
    "name": "Kris",
    "two_handed": 0,
    "level": 6,
    "equip_slot": 0,
    "width": 1,
    "height": 2,
    "damage_min": 6,
    "damage_max": 11,
    "attack_speed": 50,
    "durability": 20,
    "required_strength": 40,
    "required_agility": 40,
    "price": 0,
    "stat_kind": 1,
    "classes": [1, 1, 1, 1, 1],
    "resistances": [0, 0, 0, 0, 0, 0, 0],
}
POISON: JsonObject = {
    "name": "Poison",
    "level": 30,
    "damage": 12,
    "mana": 42,
    "stamina": 0,
    "distance": 6,
    "delay": 0,
    "energy": 100,
    "element": 1,
    "evolutions": [0, 0, 0],
    "classes": [1, 0, 0, 1, 0],
}
GATE_1: JsonObject = {
    "flag": 1,
    "map": 0,
    "x1": 121,
    "y1": 232,
    "x2": 123,
    "y2": 233,
    "target": 2,
    "direction": 0,
    "level": 20,
}


@pytest.mark.parametrize(
    ("name", "index", "expected"),
    [
        ("Local/item.bmd", 0, KRIS),
        ("Local/skill.bmd", 1, POISON),
        ("Local/Skilltest.bmd", 1, {"name": "포이즌"}),
        ("Gate.bmd", 1, GATE_1),
        ("Local/FilterName.bmd", 0, {"word": "ADMIN"}),
        ("Local/FilterName.bmd", 6, {"word": {"text": "", "tail": "65627a656e"}}),
        ("Local/itemsettype.bmd", 0, {"sets": [255, 255, 255], "raw_0x03": {"hex": "ff"}}),
        ("Local/itemsettype.bmd", 4096, {"sets": [3, 0, 0], "raw_0x03": {"hex": "00"}}),
        ("Local/itemsetoption.bmd", 0, {"name": {"hex": "ff" * 32}}),
        (
            "Local/itemsetoption.bmd",
            3,
            {
                "name": {"text": "Hyperion", "tail": HYPERION_TAIL},
                "full_set_pieces": 4,
                "full_options": [19, 12, 255, 255, 255],
            },
        ),
        ("Local/credit.bmd", 0, {"kind": 1, "text": "STAFF"}),
        ("Local/credit.bmd", 244, {"kind": 0, "text": ""}),
    ],
)
def test_known_records_read_as_the_research_says(
    reference_data: Path, name: str, index: int, expected: JsonObject
) -> None:
    record = records_of(reference_data, name)[index]
    assert isinstance(record, dict)
    assert {key: record[key] for key in expected} == expected


def raw_fields(fields: tuple[RecordField, ...]) -> Iterator[tuple[str, int]]:
    offset = 0
    for field in fields:
        if field.kind == "bytes":
            yield field.name, offset
        yield from raw_fields(field.fields)
        offset += span(field)


def test_raw_fields_are_named_after_their_offset() -> None:
    tables = [spec.codec for spec in all_formats() if isinstance(spec.codec, RecordTableFormat)]
    names = [(name, offset) for codec in tables for name, offset in raw_fields(codec.schema.fields)]
    assert names
    assert [name for name, _ in names] == [f"raw_0x{offset:02x}" for _, offset in names]
