from collections.abc import Iterator
from typing import Final

from mubmd.core.json_types import JsonValue

type Tokens = tuple[str | int, ...]

HEX_KEYS: Final = frozenset({"hex"})
TAILED_TEXT_KEYS: Final = frozenset({"text", "tail"})
LEAF_OBJECT_KEYS: Final = (HEX_KEYS, frozenset({"f32"}), TAILED_TEXT_KEYS)


def is_leaf(value: JsonValue) -> bool:
    if isinstance(value, dict):
        return value.keys() in LEAF_OBJECT_KEYS
    return not isinstance(value, list)


def leaf_text(value: JsonValue) -> str | None:
    if isinstance(value, dict) and value.keys() == TAILED_TEXT_KEYS:
        value = value["text"]
    return value if isinstance(value, str) else None


def leaves(value: JsonValue, tokens: Tokens = ()) -> Iterator[tuple[Tokens, JsonValue]]:
    if isinstance(value, list):
        for index, item in enumerate(value):
            yield from leaves(item, (*tokens, index))
    elif isinstance(value, dict) and not is_leaf(value):
        for key, item in value.items():
            yield from leaves(item, (*tokens, key))
    else:
        yield tokens, value


def is_blank(value: JsonValue) -> bool:
    if isinstance(value, list):
        return all(map(is_blank, value))
    if not isinstance(value, dict):
        return value in ("", 0)
    if value.keys() == HEX_KEYS:
        digits = value["hex"]
        return isinstance(digits, str) and not digits.strip("0")
    return not is_leaf(value) and all(map(is_blank, value.values()))
