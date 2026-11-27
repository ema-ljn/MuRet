import json
from typing import Final

from mubmd.core.json_types import JsonValue

LINE_WIDTH: Final = 120
INDENT: Final = 2
_SHORTEST_ITEM: Final = len("0, ")


def _leaf_text(value: JsonValue) -> str:
    return json.dumps(value, ensure_ascii=False)


def _members(value: list[JsonValue] | dict[str, JsonValue]) -> list[tuple[str, JsonValue]]:
    if isinstance(value, dict):
        return [(f"{_leaf_text(key)}: ", item) for key, item in value.items()]
    return [("", item) for item in value]


def _brackets(value: list[JsonValue] | dict[str, JsonValue]) -> tuple[str, str]:
    return ("{", "}") if isinstance(value, dict) else ("[", "]")


def _one_line(value: JsonValue, room: int) -> str | None:
    if not isinstance(value, list | dict):
        return _leaf_text(value)
    if _SHORTEST_ITEM * len(value) > room:
        return None
    parts: list[str] = []
    left = room
    for prefix, item in _members(value):
        text = _one_line(item, left)
        if text is None or len(prefix) + len(text) > left:
            return None
        parts.append(prefix + text)
        left -= len(parts[-1])
    opening, closing = _brackets(value)
    line = opening + ", ".join(parts) + closing
    return line if len(line) <= room else None


def _write(value: JsonValue, indent: int, prefix: str, comma: str, lines: list[str]) -> None:
    margin = " " * indent
    line = _one_line(value, LINE_WIDTH - indent - len(prefix) - len(comma))
    if line is not None:
        lines.append(f"{margin}{prefix}{line}{comma}")
        return
    assert isinstance(value, list | dict)
    opening, closing = _brackets(value)
    members = _members(value)
    lines.append(f"{margin}{prefix}{opening}")
    for number, (name, item) in enumerate(members, start=1):
        _write(item, indent + INDENT, name, "," if number < len(members) else "", lines)
    lines.append(f"{margin}{closing}{comma}")


def view_text(view: JsonValue) -> str:
    lines: list[str] = []
    _write(view, 0, "", "", lines)
    lines.append("")
    return "\n".join(lines)
