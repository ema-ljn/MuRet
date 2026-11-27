from mutools.core.errors import ErrorCode
from tests.adapters.cli.conftest import Cli

COMMANDS = {
    "formats",
    "schema",
    "workspace init",
    "info",
    "find",
    "get",
    "set",
    "apply",
    "validate",
    "diff",
    "dump",
    "build",
    "roundtrip",
}


def test_no_arguments_describe_the_tool(cli: Cli) -> None:
    run = cli()
    assert run.code == 0
    data = run.json["data"]
    assert {command["name"] for command in data["commands"]} >= COMMANDS
    assert data["error_codes"] == [code.value for code in ErrorCode]
    assert set(data["exit_codes"]) == {"0", "1", "2", "20", "30", "70"}
    assert {option["name"] for option in data["global_options"]} == {"--human", "--brief", "--help", "--version"}


def test_help_is_the_same_description(cli: Cli) -> None:
    assert cli("--help").json == cli().json


def test_every_command_is_fully_described(cli: Cli) -> None:
    for command in cli().json["data"]["commands"]:
        assert command["help"]
        assert command["example"].startswith(f"mutools {command['name']}")
        assert "type" in command["output_schema"] or "$ref" in command["output_schema"]
        for param in command["params"]:
            assert param.keys() == {"name", "kind", "type", "required", "default", "choices", "help"}
            assert param["help"]


def test_help_of_one_command(cli: Cli) -> None:
    (command,) = cli("set", "--help").json["data"]["commands"]
    assert command["name"] == "set"
    assert [param["name"] for param in command["params"]] == ["file", "pointer", "value", "--dry-run", "--encoding"]
    file, *_, dry_run, _ = command["params"]
    assert (file["kind"], file["type"], file["required"]) == ("argument", "path", True)
    assert (dry_run["kind"], dry_run["type"], dry_run["default"]) == ("option", "boolean", False)


def test_help_of_a_subcommand(cli: Cli) -> None:
    (command,) = cli("workspace", "init", "--help").json["data"]["commands"]
    assert command["name"] == "workspace init"


def test_human_help_is_text(cli: Cli) -> None:
    run = cli("--human", "--help")
    assert "workspace" in run.out
    assert not run.out.lstrip().startswith("{")
