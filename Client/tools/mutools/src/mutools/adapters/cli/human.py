import json
from typing import Any

from rich.console import Console
from rich.table import Table
from rich.text import Text

from mutools.core.json_types import JsonObject, JsonValue


def _cell(value: JsonValue) -> Text:
    return Text(value if isinstance(value, str) else json.dumps(value, ensure_ascii=False))


def _line(*parts: str | tuple[str, str]) -> Text:
    return Text.assemble(*parts)


def _rows_table(rows: list[Any]) -> Table:
    table = Table(header_style="bold yellow")
    columns = list(rows[0]) if rows and isinstance(rows[0], dict) else ["value"]
    for column in columns:
        table.add_column(column)
    for row in rows:
        values = [row.get(column) for column in columns] if isinstance(row, dict) else [row]
        table.add_row(*(_cell(value) for value in values))
    return table


def _object_table(data: JsonObject) -> Table:
    table = Table(show_header=False)
    table.add_column(style="bold yellow")
    table.add_column()
    for key, value in data.items():
        table.add_row(Text(key), _cell(value))
    return table


def render_success(console: Console, command: str, data: JsonValue, warnings: list[JsonValue]) -> None:
    console.print(_line((command, "bold")))
    if isinstance(data, dict):
        lists = {key: value for key, value in data.items() if isinstance(value, list) and value}
        scalars: JsonObject = {k: v for k, v in data.items() if k not in lists and v is not None}
        console.print(_object_table(scalars))
        for key, value in lists.items():
            console.print(_line((key, "bold")))
            console.print(_rows_table(value))
    else:
        console.print(_cell(data))
    for warning in warnings:
        if isinstance(warning, dict):
            detail = f" {warning.get('code')} {warning.get('pointer')}: {warning.get('message')}"
            console.print(_line(("warning", "yellow"), detail))


def render_error(console: Console, error: JsonObject) -> None:
    console.print(_line((str(error["code"]), "bold red"), f" {error['message']}"))
    if error.get("pointer"):
        console.print(_line(f"  at {error['pointer']}"))
    if error.get("suggestion"):
        console.print(_line(f"  try: {error['suggestion']}"))


def render_help(console: Console, description: JsonObject) -> None:
    title = f" {description['version']}: {description['description']}"
    console.print(_line((str(description["name"]), "bold yellow"), title))
    commands = description["commands"]
    assert isinstance(commands, list)
    for command in commands:
        assert isinstance(command, dict)
        params = command["params"]
        assert isinstance(params, list)
        usage = " ".join(str(param["name"]) for param in params if isinstance(param, dict))
        console.print(_line("\n", (f"mutools {command['name']}", "bold"), f" {usage}\n  {command['help']}"))
        for param in params:
            if isinstance(param, dict):
                console.print(_line(f"    {param['name']:<12} {param['help']}"))
