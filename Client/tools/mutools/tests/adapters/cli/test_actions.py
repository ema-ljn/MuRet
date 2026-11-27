from pathlib import Path

import jsonschema
import pytest

from mutools.adapters.cli.describe import describe
from tests.adapters.cli.conftest import Cli

BLOOD_CASTLE = Path("NPC") / "BloodCastle01.bmd"
SWORD = Path("Item") / "Sword01.bmd"


@pytest.fixture
def model(model_copy: Path) -> Path:
    return model_copy / BLOOD_CASTLE


@pytest.fixture
def action(cli: Cli, model: Path, tmp_path: Path) -> Path:
    output = tmp_path / "stand.json"
    assert cli("action", "export", model, "0", "--output", output).code == 0
    return output


def output_schema(name: str) -> object:
    entries = describe(name)["commands"]
    assert isinstance(entries, list)
    (entry,) = entries
    assert isinstance(entry, dict)
    return entry["output_schema"]


@pytest.mark.parametrize(
    ("name", "argv"),
    [
        ("action export", ["export", "{model}", "0", "--output", "{tmp}/out.json"]),
        ("action import", ["import", "{model}", "{action}", "--dry-run"]),
        ("action remove", ["remove", "{model}", "0", "--dry-run"]),
    ],
)
def test_stdout_is_one_envelope_that_matches_the_schema(
    cli: Cli, model: Path, action: Path, tmp_path: Path, name: str, argv: list[str]
) -> None:
    values = {"model": model, "action": action, "tmp": tmp_path}
    run = cli("action", *(word.format(**values) for word in argv))

    assert (run.code, run.err, run.out.count("\n")) == (0, "", 1)
    assert run.json["command"] == name
    jsonschema.validate(run.json["data"], output_schema(name))


def test_an_action_added_and_removed_leaves_the_same_bytes(cli: Cli, model: Path, action: Path) -> None:
    before = model.read_bytes()

    added = cli("action", "import", model, action, "--limit", "1").json["data"]
    removed = cli("action", "remove", model, str(added["action"])).json["data"]

    assert (added["action"], added["actions"], len(added["changes"])) == (1, 2, 1)
    assert added["total"] > 1
    assert (removed["action"], removed["actions"]) == (1, 1)
    assert model.read_bytes() == before


def test_an_action_that_is_not_there_is_not_found(cli: Cli, model: Path, tmp_path: Path) -> None:
    run = cli("action", "export", model, "1", "--output", tmp_path / "out.json")

    assert run.code == 20
    assert (run.error["code"], run.error["pointer"]) == ("POINTER_NOT_FOUND", "/actions/1")
    assert run.error["suggestion"] == f"mutools info {model}"


def test_a_place_that_is_not_there_says_to_leave_it_out(cli: Cli, model: Path, action: Path) -> None:
    run = cli("action", "import", model, action, "--at", "1")

    assert run.code == 20
    assert "--at" in run.error["suggestion"]


def test_a_negative_action_is_bad_usage(cli: Cli, model: Path, tmp_path: Path) -> None:
    run = cli("action", "export", model, "--output", tmp_path / "out.json", "--", "-1")

    assert (run.code, run.error["code"]) == (2, "USAGE")


def test_an_action_for_another_skeleton_is_refused_at_its_bones(cli: Cli, model_copy: Path, action: Path) -> None:
    run = cli("action", "import", model_copy / SWORD, action)

    assert run.code == 2
    assert (run.error["code"], run.error["pointer"]) == ("INVALID_VALUE", "/bones")


def test_removing_an_action_that_is_not_the_last_asks_for_yes(cli: Cli, model: Path, action: Path) -> None:
    cli("action", "import", model, action)

    run = cli("action", "remove", model, "0")

    assert run.code == 2
    assert (run.error["code"], run.error["suggestion"]) == ("USAGE", "--yes")
    assert cli("action", "remove", model, "0", "--yes").code == 0


def test_the_reference_is_never_written(cli: Cli, reference_data: Path, action: Path) -> None:
    run = cli("action", "import", reference_data / BLOOD_CASTLE, action)

    assert (run.code, run.error["code"]) == (30, "REFERENCE_READ_ONLY")
