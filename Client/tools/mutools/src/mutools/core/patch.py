import copy
import math
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Literal

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.json_types import JsonObject, JsonValue
from mutools.core.leaves import outline
from mutools.core.pointer import format_pointer, list_index, parse_pointer, resolve


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


def _invalid(message: str, pointer: str) -> MutoolsError:
    return MutoolsError(ErrorCode.INVALID_VALUE, message, pointer=pointer)


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


def _holds(parent: JsonValue, token: str) -> bool:
    if isinstance(parent, dict):
        return token in parent
    return isinstance(parent, list) and list_index(token, len(parent)) is not None


def _with(node: JsonValue, tokens: Sequence[str], value: JsonValue) -> JsonValue:
    if not tokens:
        return value
    token, rest = tokens[0], tokens[1:]
    if isinstance(node, dict):
        return {**node, token: _with(node[token], rest, value)}
    assert isinstance(node, list)
    items = list(node)
    items[int(token)] = _with(node[int(token)], rest, value)
    return items


def _replaced(document: JsonObject, path: str, value: JsonValue) -> JsonObject:
    tokens = parse_pointer(path)
    if not tokens:
        raise _invalid("replace needs a path inside the document", path)
    if not _holds(resolve(document, format_pointer(tokens[:-1])), tokens[-1]):
        resolve(document, path)
        raise MutoolsError(ErrorCode.POINTER_NOT_FOUND, f"Nothing at {path}", pointer=path)
    replaced = _with(document, tokens, copy.deepcopy(value))
    assert isinstance(replaced, dict)
    return replaced


def _zeros_of_two_signs(left: JsonValue, right: JsonValue) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return False
    if not isinstance(left, int | float) or not isinstance(right, int | float):
        return False
    return math.copysign(1.0, left) != math.copysign(1.0, right)


def same_json(left: JsonValue, right: JsonValue) -> bool:
    if isinstance(left, bool) or isinstance(right, bool):
        return left is right
    if isinstance(left, dict) and isinstance(right, dict):
        return left.keys() == right.keys() and all(same_json(left[key], right[key]) for key in left)
    if isinstance(left, list) and isinstance(right, list):
        return len(left) == len(right) and all(map(same_json, left, right))
    return left == right and not (left == 0 and _zeros_of_two_signs(left, right))


def apply_patch(view: JsonObject, ops: Sequence[PatchOp]) -> JsonObject:
    document = view
    for op in ops:
        if op.op == "test":
            current = resolve(document, op.path)
            if not same_json(current, op.value):
                raise MutoolsError(
                    ErrorCode.TEST_FAILED,
                    f"test failed at {op.path}: expected {op.value!r}, found {current!r}",
                    suggestion=f"mutools get <file> {op.path}",
                    pointer=op.path,
                )
        else:
            document = _replaced(document, op.path, op.value)
    return document


def _diff(old: JsonValue, new: JsonValue, tokens: list[str | int], out: list[Change]) -> None:
    if old is new:
        return
    if isinstance(old, dict) and isinstance(new, dict):
        for key in [*old, *(key for key in new if key not in old)]:
            _diff(old.get(key), new.get(key), [*tokens, key], out)
    elif isinstance(old, list) and isinstance(new, list) and len(old) == len(new):
        for index, (old_item, new_item) in enumerate(zip(old, new, strict=True)):
            _diff(old_item, new_item, [*tokens, index], out)
    elif old != new or (old == 0 and _zeros_of_two_signs(old, new)):
        out.append(Change(format_pointer(tokens), old, new))


def diff_views(old: JsonValue, new: JsonValue) -> tuple[Change, ...]:
    out: list[Change] = []
    _diff(old, new, [], out)
    return tuple(out)


def outlined(change: Change) -> Change:
    return Change(change.pointer, outline(change.old)[0], outline(change.new)[0])
