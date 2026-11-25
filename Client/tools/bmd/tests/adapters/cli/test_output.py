import json
from pathlib import Path

import jsonschema
import pytest

from mubmd.adapters.cli import commands as cli_commands
from mubmd.adapters.cli.describe import describe
from mubmd.adapters.tui.app import MubmdApp
from tests.adapters.cli.conftest import Cli


def commands(text: Path, reference: Path, tmp_path: Path) -> dict[str, list[str | Path]]:
    copy = text.with_name("Texttest.bmd")
    patch = tmp_path / "patch.json"
    patch.write_text(json.dumps([{"op": "replace", "path": "/texts/500", "value": "Te fuiste"}]))
    dumped = tmp_path / "text.json"
    dumped.write_text(json.dumps({"format_version": 1, "format": "text", "encoding": "cp1252", "texts": [""] * 2048}))
    return {
        "formats": ["formats"],
        "schema": ["schema", "text"],
        "workspace init": ["workspace", "init", tmp_path / "work", "--from", reference],
        "info": ["info", text],
        "find": ["find", text, "usuario"],
        "get": ["get", text, "/texts/500"],
        "set": ["set", text, "/texts/500", "Te fuiste", "--dry-run"],
        "apply": ["apply", text, patch, "--dry-run"],
        "validate": ["validate", text.parent],
        "diff": ["diff", text, copy],
        "dump": ["dump", text, "--output", tmp_path / "out.json"],
        "build": ["build", dumped, "--output", copy, "--dry-run"],
        "roundtrip": ["roundtrip", text.parent],
    }


COMMANDS = ["formats", "schema", "workspace init", "info", "find", "get", "set", "apply", "validate", "diff", "dump"]
COMMANDS += ["build", "roundtrip"]


@pytest.mark.parametrize("name", COMMANDS)
def test_stdout_is_one_envelope_that_matches_the_schema(
    cli: Cli, text: Path, reference: Path, tmp_path: Path, name: str
) -> None:
    run = cli(*commands(text, reference, tmp_path)[name])
    assert run.code in (0, 1)
    assert run.err == ""
    assert run.out.endswith("\n")
    assert run.out.count("\n") == 1
    envelope = run.json
    assert envelope.keys() == {"ok", "schema", "command", "data", "warnings"}
    assert (envelope["ok"], envelope["schema"], envelope["command"]) == (True, 1, name)
    entries = describe(name)["commands"]
    assert isinstance(entries, list)
    (entry,) = entries
    assert isinstance(entry, dict)
    jsonschema.validate(envelope["data"], entry["output_schema"])


def test_edits_carry_their_warnings_in_the_envelope(cli: Cli, text: Path) -> None:
    run = cli("set", text, "/texts/577", "Aumenta %s%% de Daño")
    assert [warning["code"] for warning in run.json["warnings"]] == ["PRINTF_CHANGED"]
    assert "warnings" not in run.json["data"]


def test_set_writes_the_file(cli: Cli, text: Path) -> None:
    assert cli("set", text, "/texts/500", "Te fuiste").json["data"]["changed"]
    assert cli("get", text, "/texts/500").json["data"]["value"] == "Te fuiste"


def test_a_value_that_looks_like_a_flag_goes_after_two_dashes(cli: Cli, text: Path) -> None:
    assert cli("set", text, "/texts/500", "--", "--- Evento ---").code == 0
    assert cli("get", text, "/texts/500").json["data"]["value"] == "--- Evento ---"


def test_apply_takes_the_patch_inline(cli: Cli, text: Path) -> None:
    patch = json.dumps(
        [{"op": "test", "path": "/texts/100", "value": "Zen"}, {"op": "replace", "path": "/texts/100", "value": "Zen!"}]
    )
    assert cli("apply", text, patch).json["data"]["changes"] == [{"pointer": "/texts/100", "old": "Zen", "new": "Zen!"}]


def test_exit_codes_report_findings_and_differences(cli: Cli, text: Path) -> None:
    assert cli("validate", text).code == 1
    assert cli("diff", text, text).code == 0
    cli("set", text.with_name("Texttest.bmd"), "/texts/1", "x")
    assert cli("diff", text, text.with_name("Texttest.bmd")).code == 1


def test_ndjson_streams_one_finding_per_line(cli: Cli, text: Path) -> None:
    run = cli("validate", text.parent, "--ndjson")
    lines = [json.loads(line) for line in run.out.splitlines()]
    first = next(line for line in lines[:-1] if line["path"] == str(text))
    assert first == {
        "path": str(text),
        "code": "TEXT_FILLS_RECORD",
        "severity": "warning",
        "pointer": "/texts/7",
        "message": first["message"],
    }
    assert lines[-1]["command"] == "validate"
    assert lines[-1]["data"] == {"files": [], "checked": 3, "skipped": 0}


def test_human_output_is_not_json(cli: Cli, text: Path) -> None:
    run = cli("--human", "info", text)
    assert run.code == 0
    assert "1645" in run.out
    with pytest.raises(json.JSONDecodeError):
        json.loads(run.out)


def test_human_output_shows_square_brackets_as_they_are(cli: Cli, text: Path) -> None:
    cli("set", text, "/texts/100", "Zen [/b] [red]rojo[/red]")
    run = cli("--human", "get", text, "/texts/100")
    assert run.code == 0
    assert "Zen [/b] [red]rojo[/red]" in run.out


def test_version_is_json(cli: Cli) -> None:
    assert cli("--version").json["data"] == {"version": "0.1.0"}


def test_tui_opens_the_console_ui(cli: Cli, working_copy: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    opened: list[Path] = []

    def run(app: MubmdApp) -> None:
        opened.append(app.folder)

    monkeypatch.setattr(MubmdApp, "run", run)
    monkeypatch.setattr(cli_commands, "has_terminal", lambda: True)
    envelope = cli("tui", working_copy).json
    assert envelope["command"] == "tui"
    assert opened == [working_copy.resolve()]
