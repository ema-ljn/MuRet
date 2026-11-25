import json
from typing import Any

from rich.console import Console
from rich.table import Table

from mubmd.core.json_types import JsonObject, JsonValue


def _cell(value: JsonValue) -> str:
    return value if isinstance(value, str) else json.dumps(value, ensure_ascii=False)


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
        table.add_row(key, _cell(value))
    return table


def render_success(console: Console, command: str, data: JsonValue, warnings: list[JsonValue]) -> None:
    console.print(f"[bold]{command}[/]")
    if isinstance(data, dict):
        lists = {key: value for key, value in data.items() if isinstance(value, list) and value}
        console.print(_object_table({key: value for key, value in data.items() if key not in lists}))
        for key, value in lists.items():
            console.print(f"[bold]{key}[/]")
            console.print(_rows_table(value))
    else:
        console.print(_cell(data))
    for warning in warnings:
        if isinstance(warning, dict):
            console.print(
                f"[yellow]warning[/] {warning.get('code')} {warning.get('pointer')}: {warning.get('message')}"
            )


def render_error(console: Console, error: JsonObject) -> None:
    console.print(f"[bold red]{error['code']}[/] {error['message']}")
    if error.get("pointer"):
        console.print(f"  at {error['pointer']}")
    if error.get("suggestion"):
        console.print(f"  try: {error['suggestion']}")


def render_help(console: Console, description: JsonObject) -> None:
    console.print(f"[bold yellow]{description['name']}[/] {description['version']}: {description['description']}")
    commands = description["commands"]
    assert isinstance(commands, list)
    for command in commands:
        assert isinstance(command, dict)
        params = command["params"]
        assert isinstance(params, list)
        usage = " ".join(str(param["name"]) for param in params if isinstance(param, dict))
        console.print(f"\n[bold]mubmd {command['name']}[/] {usage}\n  {command['help']}")
        for param in params:
            if isinstance(param, dict):
                console.print(f"    {param['name']:<12} {param['help']}")
