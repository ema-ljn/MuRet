import json
from pathlib import Path

import pytest

from tests.adapters.cli.conftest import Cli

MOVE_RECORD_SIZE = 0x50
MOVE_TRAILING_SIZE = 640

SKILL_RECORD_SIZE = 0x3C
SKILL_RECORDS_END = 256 * SKILL_RECORD_SIZE


def test_editing_a_skill_updates_its_checksum(cli: Cli, working_copy: Path, reference_data: Path) -> None:
    target = working_copy / "Local" / "skill.bmd"
    source = (reference_data / "Local" / "skill.bmd").read_bytes()
    result = cli("set", target, "/records/1/mana", "45")
    assert result.code == 0, result.err
    assert cli("get", target, "/records/1/mana").json["data"]["value"] == 45
    assert cli("validate", target).json["data"]["files"][0]["findings"] == []
    data = target.read_bytes()
    assert data[:SKILL_RECORD_SIZE] == source[:SKILL_RECORD_SIZE]
    assert data[SKILL_RECORD_SIZE : 2 * SKILL_RECORD_SIZE] != source[SKILL_RECORD_SIZE : 2 * SKILL_RECORD_SIZE]
    assert data[2 * SKILL_RECORD_SIZE : SKILL_RECORDS_END] == source[2 * SKILL_RECORD_SIZE : SKILL_RECORDS_END]
    assert data[SKILL_RECORDS_END:] != source[SKILL_RECORDS_END:]
    assert len(data) == len(source)


def test_roundtrip_reads_every_file(cli: Cli, reference_data: Path) -> None:
    result = cli("roundtrip", reference_data)
    assert result.code == 0, result.out
    assert result.json["data"] == {"checked": 1991, "identical": 1991, "differing": [], "skipped": 3}


def test_the_reference_has_only_its_known_findings(cli: Cli, reference_data: Path) -> None:
    data = cli("validate", reference_data).json["data"]
    found = {
        Path(entry["path"]).relative_to(reference_data).as_posix(): [
            (finding["code"], finding["pointer"]) for finding in entry["findings"]
        ]
        for entry in data["files"]
    }
    assert (data["checked"], data["skipped"]) == (1991, 3)
    assert found == {
        "Local/Text.bmd": [("TEXT_FILLS_RECORD", "/texts/7")],
        "Local/Texttest.bmd": [("TEXT_FILLS_RECORD", "/texts/7"), ("TEXT_FILLS_RECORD", "/texts/714")],
        "Local/TKOU.bmd": [("TEXT_HAS_TAIL", "/texts/1829")],
        "Local/item.bmd": [("CHECKSUM_MISMATCH", "/stored_checksum")],
        "Local/n380.bmd": [("CHECKSUM_MISMATCH", "/stored_checksum")],
        "Item/waterring.bmd": [("TEXTURE_NOT_FOUND", "/meshes/1/texture_name")],
        "NPC/BloodCastle01.bmd": [
            ("PARENT_AFTER_CHILD", "/bones/43/parent"),
            ("PARENT_AFTER_CHILD", "/bones/44/parent"),
        ],
        "object35/Object36.bmd": [
            ("OUT_OF_RANGE", "/meshes/1/vertices/0/bone"),
            ("OUT_OF_RANGE", "/meshes/1/normals/0/bone"),
        ],
    }


def test_adding_a_move_requirement_grows_the_file(cli: Cli, working_copy: Path, tmp_path: Path) -> None:
    target = working_copy / "Local" / "movereq.bmd"
    source = target.read_bytes()
    view = tmp_path / "movereq.json"
    assert cli("dump", target, "--output", view).code == 0
    records = json.loads(view.read_text(encoding="utf-8"))["records"]
    assert len(records) == 28
    new = {"number": 29, "name": "Kanturu", "search_name": "Kanturu", "level": 160, "zen": 9000, "gate": 138}
    result = cli("apply", target, json.dumps([{"op": "replace", "path": "/records", "value": [*records, new]}]))
    assert result.code == 0, result.err
    data = target.read_bytes()
    assert data[:4] == bytes((0x1D, 0, 0, 0))
    assert len(data) == len(source) + MOVE_RECORD_SIZE
    assert data[4 : 4 + 28 * MOVE_RECORD_SIZE] == source[4 : 4 + 28 * MOVE_RECORD_SIZE]
    assert data[-MOVE_TRAILING_SIZE:] == source[-MOVE_TRAILING_SIZE:]
    assert cli("get", target, "/records/28").json["data"]["value"] == new


@pytest.mark.parametrize(
    ("name", "pointer", "raw"),
    [
        ("Gate.bmd", "/records/1/level", "300"),
        ("Gate.bmd", "/records/1/level", "true"),
        ("Local/slide.bmd", "/records/0/speed", "NaN"),
        ("Local/slide.bmd", "/records/0/speed", "Infinity"),
    ],
)
def test_a_value_its_field_cannot_hold_is_bad_usage_and_writes_nothing(
    cli: Cli, working_copy: Path, name: str, pointer: str, raw: str
) -> None:
    target = working_copy / name
    before = target.read_bytes()
    result = cli("set", target, pointer, raw)
    assert result.code == 2
    assert (result.error["code"], result.error["pointer"]) == ("INVALID_VALUE", pointer)
    assert target.read_bytes() == before
    assert not target.with_name(f"{target.name}.orig").exists()


def test_find_takes_a_field(cli: Cli, working_copy: Path) -> None:
    target = working_copy / "Local" / "movereq.bmd"
    items = cli("find", target, "Arena", "--field", "name").json["data"]["items"]
    assert [item["pointer"] for item in items] == ["/records/0/name"]
