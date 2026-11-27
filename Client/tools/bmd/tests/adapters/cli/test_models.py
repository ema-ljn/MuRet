import json
import struct
from pathlib import Path

import pytest

from tests.adapters.cli.conftest import Cli

TEXTURE_NAME = "/meshes/0/texture_name"
SWORD_TEXTURE_NAME = slice(6772, 6804)


def codes(warnings: list[dict[str, str]]) -> list[str]:
    return [warning["code"] for warning in warnings]


def test_info_counts_a_models_parts(cli: Cli, model_copy: Path) -> None:
    data = cli("info", model_copy / "Item" / "Sword01.bmd").json["data"]
    del data["path"]
    assert data == {
        "format": "model",
        "encoding": "cp949",
        "read_by_client": None,
        "read_only": False,
        "size": 6866,
        "cipher": "none",
        "records": None,
        "used_records": None,
        "version": 10,
        "meshes": 1,
        "bones": 1,
        "actions": 1,
        "findings": 0,
    }


def test_info_of_a_table_has_no_model_parts(cli: Cli, model_copy: Path) -> None:
    data = cli("info", model_copy / "Gate.bmd").json["data"]
    assert (data["records"], data["read_by_client"]) == (256, True)
    assert (data["version"], data["meshes"], data["bones"], data["actions"]) == (None, None, None, None)


def test_info_of_the_biggest_model(cli: Cli, reference_data: Path) -> None:
    data = cli("info", reference_data / "player" / "Player.bmd").json["data"]
    assert (data["cipher"], data["version"], data["size"]) == ("K16", 12, 2_414_277)
    assert (data["meshes"], data["bones"], data["actions"], data["read_only"]) == (0, 60, 205, True)


def test_human_info_shows_a_models_parts(cli: Cli, model_copy: Path) -> None:
    out = cli("--human", "info", model_copy / "Item" / "Sword01.bmd").out
    assert "meshes" in out
    assert "records" not in out


def test_set_renames_a_texture(cli: Cli, model_copy: Path, reference_data: Path) -> None:
    target = model_copy / "Item" / "Sword01.bmd"
    before = (reference_data / "Item" / "Sword01.bmd").read_bytes()
    result = cli("set", target, TEXTURE_NAME, "etcring.jpg")
    assert (result.code, result.json["warnings"]) == (0, [])
    after = target.read_bytes()
    assert after[SWORD_TEXTURE_NAME] == b"etcring.jpg".ljust(32, b"\0")
    assert after[: SWORD_TEXTURE_NAME.start] == before[: SWORD_TEXTURE_NAME.start]
    assert after[SWORD_TEXTURE_NAME.stop :] == before[SWORD_TEXTURE_NAME.stop :]


def test_an_encrypted_model_stays_encrypted(cli: Cli, model_copy: Path, reference_data: Path) -> None:
    target = model_copy / "skill" / "m_waves.bmd"
    assert cli("set", target, "/bones/0/name", "Root").code == 0
    data = target.read_bytes()
    assert (len(data), data[:4]) == (3030, b"BMD\x0c")
    changes = cli("diff", reference_data / "skill" / "m_waves.bmd", target).json["data"]["changes"]
    assert changes == [{"pointer": "/bones/0/name", "old": {"text": "Box01", "tail": "30310031"}, "new": "Root"}]


def test_set_refuses_the_version(cli: Cli, model_copy: Path) -> None:
    target = model_copy / "Item" / "Sword01.bmd"
    before = target.read_bytes()
    result = cli("set", target, "/version", "12")
    assert result.code == 2
    assert (result.error["code"], result.error["pointer"]) == ("INVALID_VALUE", "/version")
    assert target.read_bytes() == before


def test_build_changes_the_version(cli: Cli, model_copy: Path, tmp_path: Path) -> None:
    target = model_copy / "Item" / "Sword01.bmd"
    plain = target.read_bytes()
    dumped = tmp_path / "sword.json"
    assert cli("dump", target, "--output", dumped).code == 0
    view = json.loads(dumped.read_text(encoding="utf-8"))
    for version in (12, 10):
        view["version"] = version
        source = tmp_path / f"sword{version}.json"
        source.write_text(json.dumps(view), encoding="utf-8")
        assert cli("build", source, "--output", target).code == 0
        if version == 12:
            encrypted = target.read_bytes()
            assert (len(encrypted), encrypted[:4], struct.unpack_from("<I", encrypted, 4)) == (
                6870,
                b"BMD\x0c",
                (6862,),
            )
    assert target.read_bytes() == plain


def test_build_writes_a_model_under_a_new_name(cli: Cli, model_copy: Path, tmp_path: Path) -> None:
    source = model_copy / "Item" / "Sword01.bmd"
    dumped = tmp_path / "sword.json"
    cli("dump", source, "--output", dumped)
    result = cli("build", dumped, "--output", model_copy / "Item" / "Sword99.bmd")
    assert result.code == 0, result.err
    assert (model_copy / "Item" / "Sword99.bmd").read_bytes() == source.read_bytes()


def test_validate_names_a_texture_nobody_has(cli: Cli, model_copy: Path) -> None:
    result = cli("validate", model_copy / "Item" / "waterring.bmd")
    (finding,) = result.json["data"]["files"][0]["findings"]
    assert result.code == 1
    assert (finding["code"], finding["severity"], finding["pointer"]) == (
        "TEXTURE_NOT_FOUND",
        "warning",
        "/meshes/1/texture_name",
    )
    assert "waterring.OZJ" in finding["message"]


def test_a_texture_in_a_folder_beside_is_no_finding(cli: Cli, model_copy: Path) -> None:
    result = cli("validate", model_copy / "player" / "Helper03.bmd")
    assert (result.code, result.json["data"]["files"][0]["findings"]) == (0, [])


@pytest.mark.parametrize(
    ("name", "code", "mention"),
    [
        ("GroundEff.jpg", "TEXTURE_ELSEWHERE", "skill"),
        ("nothing.jpg", "TEXTURE_NOT_FOUND", "nothing.OZJ"),
        ("etcring_R.tga", "TEXTURE_ROLE_CHANGED", "etcring_R.OZT"),
    ],
)
def test_a_rename_warns_and_is_written(cli: Cli, model_copy: Path, name: str, code: str, mention: str) -> None:
    target = model_copy / "Item" / "Sword01.bmd"
    result = cli("set", target, TEXTURE_NAME, name)
    assert result.code == 0, result.err
    (warning,) = (warning for warning in result.json["warnings"] if warning["code"] == code)
    assert warning["pointer"] == TEXTURE_NAME
    assert mention in warning["message"]
    assert cli("get", target, TEXTURE_NAME).json["data"]["value"] == name


def test_find_reads_a_models_names(cli: Cli, model_copy: Path) -> None:
    target = model_copy / "player" / "HelmMale01.bmd"
    bones = cli("find", target, "Thigh").json["data"]["items"]
    assert "/bones/3/name" in [item["pointer"] for item in bones]
    textures = cli("find", target, "hide", "--field", "texture_name").json["data"]["items"]
    assert [item["pointer"] for item in textures] == ["/meshes/1/texture_name"]


def test_formats_lists_the_model(cli: Cli) -> None:
    formats = cli("formats").json["data"]["formats"]
    assert formats[-1] == {"name": "model", "size": None, "files": [], "matches": "content"}
    assert formats[0]["name"] == "text"
    assert formats[0]["matches"] == "name"


def test_schema_describes_a_model(cli: Cli) -> None:
    fields = cli("schema", "model").json["data"]["fields"]
    assert "/meshes/<N>/texture_name" in [field["pointer"] for field in fields]


def test_a_corrupt_model_is_an_error_and_a_finding(cli: Cli, model_copy: Path) -> None:
    target = model_copy / "Item" / "Sword01.bmd"
    target.write_bytes(target.read_bytes()[:100])
    result = cli("info", target)
    assert (result.code, result.error["code"]) == (30, "CORRUPT_FILE")
    folder = cli("validate", model_copy / "Item")
    found = {Path(entry["path"]).name: codes(entry["findings"]) for entry in folder.json["data"]["files"]}
    assert found == {"Sword01.bmd": ["CORRUPT_FILE"], "waterring.bmd": ["TEXTURE_NOT_FOUND"]}
    assert folder.json["data"]["checked"] == 2


def test_the_help_names_a_models_pointer(cli: Cli) -> None:
    (command,) = cli("get", "--help").json["data"]["commands"]
    (pointer,) = (param for param in command["params"] if param["name"] == "pointer")
    assert "/meshes/0/texture_name" in pointer["help"]


def test_the_help_says_where_a_model_is_built(cli: Cli) -> None:
    (command,) = cli("build", "--help").json["data"]["commands"]
    assert "a model's view to any .bmd name" in command["help"]
