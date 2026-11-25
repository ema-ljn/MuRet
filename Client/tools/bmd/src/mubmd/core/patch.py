import copy
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal

from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.pointer import format_pointer, list_index, parse_pointer, resolve


@dataclass(frozen=True, slots=True)
class PatchOp:
    op: Literal["test", "replace"]
    path: str
    value: JsonValue


@dataclass(frozen=True, slots=True)
class Change:
    pointer: str
    old: JsonValue
    new: JsonValue


def _invalid(message: str, pointer: str) -> MubmdError:
    return MubmdError(ErrorCode.INVALID_VALUE, message, pointer=pointer)


def parse_patch(raw: JsonValue) -> tuple[PatchOp, ...]:
    if not isinstance(raw, list):
        raise _invalid("A patch is a JSON array of operations; put a single operation inside [ ]", "")
    ops: list[PatchOp] = []
    for number, item in enumerate(raw):
        if not isinstance(item, dict):
            raise _invalid(f"Operation {number} is not an object", f"/{number}")
        op = item.get("op")
        if op not in ("test", "replace"):
            raise _invalid(f"Operation {number}: only 'test' and 'replace' are supported, got {op!r}", f"/{number}/op")
        path = item.get("path")
        if not isinstance(path, str):
            raise _invalid(f"Operation {number} needs a 'path' string", f"/{number}/path")
        parse_pointer(path)
        if "value" not in item:
            raise _invalid(f"Operation {number} needs a 'value'", f"/{number}/value")
        ops.append(PatchOp("test" if op == "test" else "replace", path, item["value"]))
    return tuple(ops)


def _replace(document: JsonObject, path: str, value: JsonValue) -> None:
    tokens = parse_pointer(path)
    if not tokens:
        raise _invalid("replace needs a path inside the document", path)
    parent = resolve(document, format_pointer(tokens[:-1]))
    last = tokens[-1]
    if isinstance(parent, dict) and last in parent:
        parent[last] = copy.deepcopy(value)
        return
    if isinstance(parent, list):
        index = list_index(last, len(parent))
        if index is not None:
            parent[index] = copy.deepcopy(value)
            return
    resolve(document, path)
    raise MubmdError(ErrorCode.POINTER_NOT_FOUND, f"Nothing at {path}", pointer=path)


def apply_patch(view: JsonObject, ops: Sequence[PatchOp]) -> JsonObject:
    document = copy.deepcopy(view)
    for op in ops:
        if op.op == "test":
            current = resolve(document, op.path)
            if current != op.value:
                raise MubmdError(
                    ErrorCode.TEST_FAILED,
                    f"test failed at {op.path}: expected {op.value!r}, found {current!r}",
                    suggestion=f"mubmd get <file> {op.path}",
                    pointer=op.path,
                )
        else:
            _replace(document, op.path, op.value)
    return document


def _diff(old: JsonValue, new: JsonValue, tokens: list[str | int], out: list[Change]) -> None:
    if isinstance(old, dict) and isinstance(new, dict):
        for key in [*old, *(key for key in new if key not in old)]:
            _diff(old.get(key), new.get(key), [*tokens, key], out)
    elif isinstance(old, list) and isinstance(new, list) and len(old) == len(new):
        for index, (old_item, new_item) in enumerate(zip(old, new, strict=True)):
            _diff(old_item, new_item, [*tokens, index], out)
    elif old != new:
        out.append(Change(format_pointer(tokens), old, new))


def diff_views(old: JsonValue, new: JsonValue) -> tuple[Change, ...]:
    out: list[Change] = []
    _diff(old, new, [], out)
    return tuple(out)
