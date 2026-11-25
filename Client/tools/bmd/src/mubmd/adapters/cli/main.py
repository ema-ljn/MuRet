import dataclasses
import io
import sys
from collections.abc import Sequence
from typing import Any, Final

import typer
from rich.console import Console
from typer._click.exceptions import UsageError

from mubmd.adapters.cli import human
from mubmd.adapters.cli.commands import BriefData, Outcome, app, version_outcome
from mubmd.adapters.cli.describe import BRIEF, command_names, describe
from mubmd.adapters.cli.envelope import failure, success, to_json_value, to_line
from mubmd.adapters.cli.exit_codes import EXIT_OK, exit_code_for
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding
from mubmd.core.json_types import JsonObject, JsonValue

HUMAN: Final = "--human"
ECHOED_ARGUMENT_LIMIT: Final = 200


def _shortened(argument: str) -> str:
    return argument if len(argument) <= ECHOED_ARGUMENT_LIMIT else argument[:ECHOED_ARGUMENT_LIMIT] + "…"


def _keep_fields(data: JsonValue, fields: tuple[str, ...], item_type: Any) -> JsonValue:
    known = [field.name for field in dataclasses.fields(item_type)]
    unknown = [name for name in fields if name not in known]
    if unknown:
        raise MubmdError(
            ErrorCode.USAGE,
            f"Unknown field {', '.join(unknown)}; the fields are {', '.join(known)}",
            suggestion=f"--fields {','.join(known)}",
        )
    if not isinstance(data, dict):
        return data
    items = data.get("items")
    rows = items if isinstance(items, list) else [data]
    kept = [{key: row[key] for key in fields if key in row} if isinstance(row, dict) else row for row in rows]
    return {**data, "items": kept} if isinstance(items, list) else kept[0]


def _command_for_help(head: list[str]) -> str | None:
    words = [arg for arg in head if not arg.startswith("-")]
    for size in range(len(words), 0, -1):
        name = " ".join(words[:size])
        if any(known == name or known.startswith(f"{name} ") for known in command_names()):
            return name
    return None


def _run(args: list[str]) -> Outcome:
    result = typer.main.get_command(app).main(args, prog_name="mubmd", standalone_mode=False)
    assert isinstance(result, Outcome)
    return result


class Output:
    def __init__(self, human_output: bool) -> None:
        self.human = human_output

    def success(self, outcome: Outcome) -> int:
        data = to_json_value(outcome.data, outcome.data_type)
        if outcome.fields is not None:
            data = _keep_fields(data, outcome.fields, outcome.item_type)
        warnings = [to_json_value(warning, Finding) for warning in outcome.warnings]
        if self.human:
            human.render_success(Console(file=sys.stdout), outcome.command, data, warnings)
            return outcome.exit_code
        for line in outcome.lines:
            sys.stdout.write(to_line(line) + "\n")
        sys.stdout.write(to_line(success(outcome.command, data, warnings)) + "\n")
        return outcome.exit_code

    def help(self, command: str | None) -> int:
        description = describe(command)
        if self.human:
            human.render_help(Console(file=sys.stdout), description)
        else:
            sys.stdout.write(to_line(success("help", description, [])) + "\n")
        return EXIT_OK

    def failure(self, error: MubmdError, argv: Sequence[str]) -> int:
        received: JsonObject = {"argv": [_shortened(arg) for arg in argv]}
        envelope = failure(error, received)
        if self.human:
            body = envelope["error"]
            assert isinstance(body, dict)
            human.render_error(Console(file=sys.stderr), body)
        else:
            sys.stderr.write(to_line(envelope) + "\n")
        return exit_code_for(error.code)


def _usage(error: UsageError) -> MubmdError:
    path = error.ctx.command_path if error.ctx is not None else "mubmd"
    return MubmdError(ErrorCode.USAGE, error.format_message(), suggestion=f"{path} --help")


def _dispatch(output: Output, head: list[str], tail: list[str]) -> int:
    if "--version" in head:
        return output.success(version_outcome())
    if "--brief" in head:
        return output.success(Outcome("brief", BriefData(BRIEF), BriefData))
    if "--help" in head or not (head or tail):
        return output.help(_command_for_help(head))
    return output.success(_run(head + tail))


def main(argv: Sequence[str] | None = None) -> int:
    args = list(sys.argv[1:] if argv is None else argv)
    split = args.index("--") if "--" in args else len(args)
    head, tail = [arg for arg in args[:split] if arg != HUMAN], args[split:]
    output = Output(HUMAN in args[:split])
    try:
        return _dispatch(output, head, tail)
    except UsageError as error:
        return output.failure(_usage(error), args)
    except MubmdError as error:
        return output.failure(error, args)
    except OSError as error:
        return output.failure(MubmdError(ErrorCode.IO_ERROR, str(error)), args)
    except Exception as error:
        return output.failure(MubmdError(ErrorCode.INTERNAL_ERROR, f"{type(error).__name__}: {error}"), args)


def run() -> None:
    for stream in (sys.stdout, sys.stderr):
        if isinstance(stream, io.TextIOWrapper):
            stream.reconfigure(encoding="utf-8")
    sys.exit(main())
