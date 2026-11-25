import re
from collections.abc import Sequence

from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.json_types import JsonValue

_LIST_INDEX = re.compile(r"0|[1-9][0-9]*")


def parse_pointer(text: str) -> tuple[str, ...]:
    if text == "":
        return ()
    if not text.startswith("/"):
        raise MubmdError(
            ErrorCode.POINTER_NOT_FOUND,
            f"A JSON Pointer is empty or starts with '/', got {text!r}",
            suggestion=f"/{text}",
            pointer=text,
        )
    return tuple(token.replace("~1", "/").replace("~0", "~") for token in text[1:].split("/"))


def format_pointer(tokens: Sequence[str | int]) -> str:
    return "".join("/" + str(token).replace("~", "~0").replace("/", "~1") for token in tokens)


def list_index(token: str, length: int) -> int | None:
    if not _LIST_INDEX.fullmatch(token):
        return None
    index = int(token)
    return index if index < length else None


def child(container: JsonValue, token: str, pointer: str) -> JsonValue:
    if isinstance(container, dict) and token in container:
        return container[token]
    if isinstance(container, list):
        index = list_index(token, len(container))
        if index is not None:
            return container[index]
        raise MubmdError(
            ErrorCode.POINTER_NOT_FOUND,
            f"No item {token!r} at {pointer}: the list has indices 0 to {len(container) - 1}",
            pointer=pointer,
        )
    raise MubmdError(ErrorCode.POINTER_NOT_FOUND, f"Nothing at {pointer}", pointer=pointer)


def resolve(value: JsonValue, pointer: str) -> JsonValue:
    current = value
    for token in parse_pointer(pointer):
        current = child(current, token, pointer)
    return current
