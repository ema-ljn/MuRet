import shutil
from pathlib import Path

import jsonschema
import pytest

from mutools.adapters.cli.describe import describe
from tests.adapters.cli.conftest import Cli


@pytest.fixture
def data(working_copy: Path, reference_data: Path, cli: Cli, tmp_path: Path) -> Path:
    player = working_copy / "player" / "Player.bmd"
    player.parent.mkdir()
    shutil.copyfile(reference_data / "player" / "Player.bmd", player)
    action = tmp_path / "action.json"
    assert cli("action", "export", player, "194", "--output", action).code == 0
    assert cli("action", "import", player, action).json["data"]["action"] == 205
    return working_copy


def output_schema(name: str) -> object:
    entries = describe(name)["commands"]
    assert isinstance(entries, list)
    (entry,) = entries
    assert isinstance(entry, dict)
    return entry["output_schema"]


def test_an_emote_added_listed_and_removed(cli: Cli, data: Path) -> None:
    added = cli("emote", "add", data, "205", "--trigger", "wave", "--trigger", "/wave", "--once")
    listed = cli("emote", "list", data)
    removed = cli("emote", "remove", data, "205")

    for run, name in ((added, "emote add"), (listed, "emote list"), (removed, "emote remove")):
        assert (run.code, run.err, run.json["command"]) == (0, "", name)
        jsonschema.validate(run.json["data"], output_schema(name))
    assert listed.json["data"]["emotes"] == [
        {"action": 205, "elf_action": 205, "keys_per_step": 0.32, "plays": "once", "triggers": ["wave", "/wave"]}
    ]
    assert (data / "MuRet" / "Emotes.txt").read_bytes() == b"MuRet emotes 1\n"


def test_a_taken_trigger_is_a_warning_and_still_written(cli: Cli, data: Path) -> None:
    run = cli("emote", "add", data, "205", "--trigger", "Thanks", "--keys-per-step", "0.5", "--elf-action", "204")

    assert run.code == 0
    assert [(warning["code"], warning["pointer"]) for warning in run.json["warnings"]] == [
        ("TRIGGER_TAKEN", "/triggers/0")
    ]
    assert (data / "MuRet" / "Emotes.txt").read_bytes() == b"MuRet emotes 1\n205\t204\t0.5\tloop\tThanks\n"


def test_an_action_player_bmd_does_not_have_says_how_to_add_it(cli: Cli, data: Path) -> None:
    run = cli("emote", "add", data, "206", "--trigger", "wave")

    assert run.code == 2
    assert (run.error["code"], run.error["pointer"]) == ("INVALID_VALUE", "/action")
    assert run.error["suggestion"].startswith(f"mutools action import {data}/player/Player.bmd")


def test_a_trigger_is_required(cli: Cli, data: Path) -> None:
    assert cli("emote", "add", data, "205").code == 2
