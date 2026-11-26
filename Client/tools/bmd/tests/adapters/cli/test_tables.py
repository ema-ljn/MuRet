from pathlib import Path

from tests.adapters.cli.conftest import Cli

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
