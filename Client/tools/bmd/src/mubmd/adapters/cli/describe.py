import inspect
import types
import typing
from collections.abc import Callable, Iterator
from pathlib import Path
from typing import Any, Final

import typer
from typer.models import ArgumentInfo, OptionInfo

from mubmd import __version__
from mubmd.adapters.cli.commands import EXAMPLES, OUTPUT_TYPES, app
from mubmd.adapters.cli.envelope import output_schema
from mubmd.adapters.cli.exit_codes import EXIT_CODE_MEANINGS
from mubmd.core.errors import ErrorCode
from mubmd.core.json_types import JsonObject, JsonValue

BRIEF: Final = (
    "mubmd reads, checks and edits MU Online's .bmd files byte-exact (texts and tables). Every command "
    "prints one JSON object on stdout, {ok: true, schema, command, data, warnings}, or one on stderr when it "
    "fails, {ok: false, schema, error: {code, message, suggestion, input, pointer}}, and exits 0, 1 (findings or "
    "differences), 2 (usage), 20 (not found), 30 (precondition) or 70 (a bug in the tool). Places inside a file "
    "are JSON Pointers (/texts/500, /records/1/mana). The reference folder Client/assets/Data is read-only: run "
    "`mubmd workspace init ~/mu/Data` once and edit the copy with `find`, `get`, then `set` or `apply` (a JSON "
    "Patch of test and replace). A write is verified before the file is replaced, and its result is what was "
    "written. `mubmd <command> --help` describes one command as JSON."
)

DESCRIPTION: Final = "Read, check and edit MU Online's .bmd files byte-exact, from a CLI built for agents."

GLOBAL_OPTIONS: Final[list[JsonValue]] = [
    {"name": "--human", "help": "Rich tables and colors instead of JSON"},
    {"name": "--brief", "help": "One paragraph on how to use the tool"},
    {"name": "--help", "help": "This description, or one command's with mubmd <command> --help"},
    {"name": "--version", "help": "The tool's version"},
]

_TYPE_NAMES: Final[dict[Any, str]] = {Path: "path", int: "integer", str: "string", bool: "boolean"}


def _type_name(annotation: Any) -> str:
    if isinstance(annotation, types.UnionType):
        (annotation,) = (arg for arg in typing.get_args(annotation) if arg is not type(None))
    return _TYPE_NAMES[annotation]


def _option_names(info: OptionInfo) -> list[str]:
    names = [str(name) for name in info.param_decls or ()]
    if isinstance(info.default, str) and info.default.startswith("-"):
        names.insert(0, info.default)
    return names


def _param(name: str, parameter: inspect.Parameter, annotation: Any) -> JsonObject:
    base, info = typing.get_args(annotation)[:2]
    has_default = parameter.default is not inspect.Parameter.empty
    default = parameter.default if has_default else None
    if isinstance(info, ArgumentInfo):
        kind, shown = "argument", (info.metavar or name).lower()
    else:
        assert isinstance(info, OptionInfo)
        kind, shown = "option", _option_names(info)[0]
    return {
        "name": shown,
        "kind": kind,
        "type": _type_name(base),
        "required": not has_default,
        "default": str(default) if isinstance(default, Path) else default,
        "choices": None,
        "help": info.help,
    }


def _params(callback: Callable[..., Any]) -> list[JsonValue]:
    hints = typing.get_type_hints(callback, include_extras=True)
    signature = inspect.signature(callback)
    return [_param(name, parameter, hints[name]) for name, parameter in signature.parameters.items()]


def _commands(typer_app: typer.Typer, prefix: str) -> Iterator[tuple[str, str, Callable[..., Any]]]:
    for command in typer_app.registered_commands:
        assert command.callback is not None
        yield f"{prefix}{command.name}", str(command.help), command.callback
    for group in typer_app.registered_groups:
        assert group.typer_instance is not None
        yield from _commands(group.typer_instance, f"{prefix}{group.name} ")


def _entry(name: str, help_text: str, callback: Callable[..., Any]) -> JsonObject:
    return {
        "name": name,
        "help": help_text,
        "params": _params(callback),
        "example": EXAMPLES[name],
        "output_schema": output_schema(OUTPUT_TYPES[name]),
    }


def command_names() -> list[str]:
    return [name for name, _, _ in _commands(app, "")]


def describe(command: str | None) -> JsonObject:
    entries: list[JsonValue] = [
        _entry(name, help_text, callback)
        for name, help_text, callback in _commands(app, "")
        if command is None or name == command or name.startswith(f"{command} ")
    ]
    return {
        "name": "mubmd",
        "version": __version__,
        "description": DESCRIPTION,
        "brief": BRIEF,
        "global_options": GLOBAL_OPTIONS,
        "commands": entries,
        "exit_codes": {str(code): meaning for code, meaning in EXIT_CODE_MEANINGS.items()},
        "error_codes": [code.value for code in ErrorCode],
    }
