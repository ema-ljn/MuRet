import json
from pathlib import Path

import pytest

from tests.adapters.cli.conftest import Cli

RECORD_SIZE = 0x54
NAME_SIZE = 0x1E
TABLE_SIZE = 8192 * RECORD_SIZE


def test_an_item_name_is_editable_without_touching_other_fields(cli: Cli, tmp_path: Path, reference_data: Path) -> None:
    source = (reference_data / "Local" / "item.bmd").read_bytes()
    target = tmp_path / "item" / "item.bmd"
    target.parent.mkdir()
    target.write_bytes(source)
    pointer = "/records/0/name"
    operations = [
        {"op": "test", "path": pointer, "value": "Kris"},
        {"op": "replace", "path": pointer, "value": "Training Kris"},
    ]
    result = cli("apply", target, json.dumps(operations))
    assert result.code == 0, result.err
    assert result.json["data"]["total"] == 1
    data = target.read_bytes()
    assert data[:NAME_SIZE] != source[:NAME_SIZE]
    assert data[NAME_SIZE:TABLE_SIZE] == source[NAME_SIZE:TABLE_SIZE]
    assert data[TABLE_SIZE + 4 :] == source[TABLE_SIZE + 4 :]
    assert cli("validate", target).json["data"]["files"][0]["findings"] == []
    assert cli("find", target, "Training Kris").json["data"]["items"][0]["pointer"] == pointer


def test_dump_and_build_preserve_the_original_item_checksum_and_trailing_bytes(
    cli: Cli, tmp_path: Path, reference_data: Path
) -> None:
    original = reference_data / "Local" / "item.bmd"
    view = tmp_path / "item.json"
    target = tmp_path / "item.bmd"
    assert cli("dump", original, "--output", view).code == 0
    result = cli("build", view, "--output", target)
    assert result.code == 0, result.err
    assert target.read_bytes() == original.read_bytes()


def test_item_models_with_the_same_name_are_not_item_tables(cli: Cli, reference_data: Path) -> None:
    result = cli("info", reference_data / "Item" / "item.bmd")
    assert result.error["code"] == "UNKNOWN_FORMAT"


def test_a_relative_item_model_is_not_an_item_table(
    cli: Cli, reference_data: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.chdir(reference_data / "Item")
    result = cli("info", "item.bmd")
    assert result.error["code"] == "UNKNOWN_FORMAT"


def test_itemtest_matches_its_stored_checksum(cli: Cli, reference_data: Path) -> None:
    result = cli("validate", reference_data / "Local" / "itemtest.bmd")
    assert result.code == 0, result.out
    assert result.json["data"]["files"][0]["findings"] == []


@pytest.mark.parametrize("footer", [b"", b"\x11\x22\x33"])
def test_an_item_export_with_an_incomplete_checksum_roundtrips(
    cli: Cli, tmp_path: Path, reference_data: Path, footer: bytes
) -> None:
    original = (reference_data / "Local" / "item.bmd").read_bytes()[:TABLE_SIZE] + footer
    file = tmp_path / "item.bmd"
    view = tmp_path / "item.json"
    target = tmp_path / "n380.bmd"
    file.write_bytes(original)
    assert cli("dump", file, "--output", view).code == 0
    result = cli("build", view, "--output", target)
    assert result.code == 0, result.err
    assert target.read_bytes() == original

    edited = cli("set", target, "/records/0/name", "Training Kris")
    assert edited.code == 0, edited.err
    assert target.read_bytes()[TABLE_SIZE + 4 :] == footer
    assert cli("validate", target).code == 0


@pytest.mark.parametrize("key", ["stored_checksum", "trailing", "records_sha256"])
def test_an_item_export_cannot_silently_lose_metadata(cli: Cli, tmp_path: Path, reference_data: Path, key: str) -> None:
    original = reference_data / "Local" / "item.bmd"
    view = tmp_path / "item.json"
    assert cli("dump", original, "--output", view).code == 0
    document = json.loads(view.read_text(encoding="utf-8"))
    del document[key]
    view.write_text(json.dumps(document), encoding="utf-8")
    target = tmp_path / "item.bmd"

    result = cli("build", view, "--output", target)

    assert result.error["code"] == "INVALID_VALUE"
    assert result.error["pointer"] == f"/{key}"
    assert not target.exists()


def test_editing_an_item_name_requires_space_for_its_terminator(cli: Cli, tmp_path: Path, reference_data: Path) -> None:
    target = tmp_path / "item.bmd"
    original = (reference_data / "Local" / "item.bmd").read_bytes()
    target.write_bytes(original)
    result = cli("set", target, "/records/0/name", "A" * NAME_SIZE)
    assert result.error["code"] == "TEXT_TOO_LONG"
    assert target.read_bytes() == original
