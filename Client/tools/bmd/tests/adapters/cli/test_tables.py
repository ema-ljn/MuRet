import json
from pathlib import Path

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


def test_roundtrip_reads_every_table(cli: Cli, reference_data: Path) -> None:
    result = cli("roundtrip", reference_data)
    assert result.code == 0, result.out
    assert result.json["data"] == {"checked": 29, "identical": 29, "differing": [], "skipped": 1965}


def test_the_reference_has_only_its_known_findings(cli: Cli, reference_data: Path) -> None:
    result = cli("validate", reference_data)
    found = {
        Path(entry["path"]).name: [finding["code"] for finding in entry["findings"]]
        for entry in result.json["data"]["files"]
    }
    assert found == {
        "Text.bmd": ["TEXT_FILLS_RECORD"],
        "Texttest.bmd": ["TEXT_FILLS_RECORD", "TEXT_FILLS_RECORD"],
        "TKOU.bmd": ["TEXT_HAS_TAIL"],
        "item.bmd": ["CHECKSUM_MISMATCH"],
        "n380.bmd": ["CHECKSUM_MISMATCH"],
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
