from pathlib import Path

import jsonschema
import pytest

from mutools.adapters.cli import commands
from mutools.adapters.cli.describe import describe
from tests.adapters.cli.conftest import Cli

BLOOD_CASTLE = Path("NPC") / "BloodCastle01.bmd"
BLOOD_CASTLE_MESHES = 4


@pytest.fixture
def model(model_copy: Path) -> Path:
    return model_copy / BLOOD_CASTLE


@pytest.fixture
def exported(cli: Cli, model: Path, tmp_path: Path) -> Path:
    output = tmp_path / "gate.glb"
    assert cli("gltf", "export", model, "--output", output).code == 0
    return output


def output_schema(name: str) -> object:
    entries = describe(name)["commands"]
    assert isinstance(entries, list)
    (entry,) = entries
    assert isinstance(entry, dict)
    return entry["output_schema"]


def test_an_export_answers_with_what_the_glb_holds(cli: Cli, model: Path, tmp_path: Path) -> None:
    run = cli("gltf", "export", model, "--output", tmp_path / "gate.glb")

    data = run.json["data"]
    jsonschema.validate(data, output_schema("gltf export"))
    assert (data["animations"], data["meshes"], data["size"]) == (1, 4, (tmp_path / "gate.glb").stat().st_size)


def test_a_texture_that_cannot_be_read_is_listed(cli: Cli, model: Path, tmp_path: Path) -> None:
    untextured = cli("gltf", "export", model, "--output", tmp_path / "gate.glb").json["data"]["untextured"]

    assert [found["mesh"] for found in untextured] == list(range(BLOOD_CASTLE_MESHES))


def test_an_import_answers_with_the_animation_it_took(cli: Cli, model: Path, exported: Path) -> None:
    run = cli("gltf", "import", model, exported, "--dry-run")

    data = run.json["data"]
    jsonschema.validate(data, output_schema("gltf import"))
    assert (data["animation"], data["keys"], data["action"], data["actions"]) == ("000 Action 0", 12, 1, 2)


def test_an_action_imported_over_itself_leaves_the_file_as_it_was(cli: Cli, model: Path, exported: Path) -> None:
    before = model.read_bytes()

    run = cli("gltf", "import", model, exported, "--at", "0")

    assert (run.code, run.json["data"]["changed"], model.read_bytes()) == (0, False, before)


def test_an_export_without_the_gltf_extra_says_how_to_add_it(
    cli: Cli, model: Path, tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def missing() -> None:
        raise ModuleNotFoundError("No module named 'pygltflib'", name="pygltflib")

    monkeypatch.setattr(commands, "load_gltf_writer", missing)

    run = cli("gltf", "export", model, "--output", tmp_path / "gate.glb")

    assert (run.code, run.error["code"], run.error["suggestion"]) == (2, "USAGE", "pip install 'mutools[gltf]'")
