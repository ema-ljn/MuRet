from collections.abc import Iterator
from pathlib import Path

import pytest

from mutools.core.catalog import all_formats, identify
from mutools.core.formats.record_fields import RecordField, span
from mutools.core.formats.record_table_format import RecordTableFormat
from mutools.core.json_types import JsonObject, JsonValue
from mutools.core.pointer import resolve
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
        ("Local/slide.bmd", 1, 1),
        ("Local/movereq.bmd", 28, 28),
        ("Local/movereqtest.bmd", 30, 30),
        ("Local/TKOUREQ.bmd", 30, 30),
        ("Local/MonsterSkill.bmd", 8, 8),
        ("Local/Dialog.bmd", 200, 75),
        ("Local/quest.bmd", 200, 4),
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
ARENA: JsonObject = {"number": 1, "name": "Arena", "search_name": "Arena", "level": 50, "zen": 2000, "gate": 200}
DIALOG_53: JsonObject = {
    "answer_count": 3,
    "next": [52, 54, 53, -1, 0, 0, 0, 0, 0, 0],
    "actions": [-1, 1, 2, 0, 0, 0, 0, 0, 0, 0],
}
QUEST_0_NAME = " Find the 'Scroll of Emperor'!"
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
        ("Local/movereq.bmd", 0, ARENA),
        ("Local/movereq.bmd", 10, {"name": {"text": "Atlans", "tail": "d9cbb9"}}),
        ("Local/movereq.bmd", 27, {"name": "Crywolf Fortress2", "level": 300, "zen": 10000}),
        ("Local/movereqtest.bmd", 0, {"name": "竞技场"}),
        ("Local/MonsterSkill.bmd", 0, {"type": 304, "skills": [4, 1, -1, -1, -1]}),
        ("Local/Dialog.bmd", 0, {"text": {"text": "Aew! Precisa de alguma coisa?", "tail": "643f"}}),
        ("Local/Dialog.bmd", 53, DIALOG_53),
        ("Local/quest.bmd", 0, {"entry_count": 2, "condition_count": 3, "npc_type": 235, "name": QUEST_0_NAME}),
        ("Local/questtest.bmd", 0, {"name": "'제왕의 서'를 찾아라!"}),
        ("Local/slide.bmd", 0, {"period": 60, "speed": 2.5}),
    ],
)
def test_known_records_read_as_the_research_says(
    reference_data: Path, name: str, index: int, expected: JsonObject
) -> None:
    record = records_of(reference_data, name)[index]
    assert isinstance(record, dict)
    assert {key: record[key] for key in expected} == expected


@pytest.mark.parametrize(
    ("name", "pointer", "expected"),
    [
        ("Local/Dialog.bmd", "/53/answers/0", "Please repeat the story."),
        ("Local/Dialog.bmd", "/53/answers/1", "I will go find it."),
        ("Local/Dialog.bmd", "/53/answers/2", "I don't care."),
        ("Local/quest.bmd", "/0/entries/0/item_group", 14),
        ("Local/quest.bmd", "/0/entries/0/item_index", 23),
        ("Local/quest.bmd", "/0/entries/0/dialogs", [52, 54, 55, 56]),
        ("Local/quest.bmd", "/0/conditions/0/link", 1),
        ("Local/quest.bmd", "/0/conditions/0/fail_dialog", 73),
        ("Local/slide.bmd", "/0/groups/0/max_level", 15),
        ("Local/slide.bmd", "/0/groups/0/count", 28),
        ("Local/slide.bmd", "/0/groups/0/texts/0", "Precisa esta no level 15 para entrar em Devias"),
        ("Local/slide.bmd", "/0/groups/3/count", -1),
    ],
)
def test_known_values_inside_lists_and_structs(
    reference_data: Path, name: str, pointer: str, expected: JsonValue
) -> None:
    assert resolve(records_of(reference_data, name), pointer) == expected


@pytest.mark.parametrize(("name", "size"), [("movereq.bmd", 640), ("movereqtest.bmd", 480), ("TKOUREQ.bmd", 480)])
def test_a_counted_file_keeps_the_bytes_after_its_records(reference_data: Path, name: str, size: int) -> None:
    data = (reference_data / "Local" / name).read_bytes()
    found = identify(name, data)
    assert len(found.format.codec.decode(data, found.file).trailing) == size


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
