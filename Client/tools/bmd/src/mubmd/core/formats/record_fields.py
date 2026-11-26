from collections.abc import Iterator, Sequence
from contextlib import suppress
from dataclasses import dataclass
from math import isfinite
from struct import Struct
from typing import Final, Literal

from mubmd.core.documents.text_table import decode_text, encode_fixed_text
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding, Severity
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.pointer import list_index
from mubmd.core.ports import FieldInfo

Kind = Literal["text", "bytes", "uint", "int", "f32", "struct"]

FLOAT32: Final = Struct("<f")


@dataclass(frozen=True, slots=True)
class RecordField:
    name: str
    size: int
    kind: Kind
    count: int | None = None
    fields: tuple["RecordField", ...] = ()
    lowest: int | None = None
    highest: int | None = None


def span(field: RecordField) -> int:
    return field.size * (field.count or 1)


def struct(name: str, fields: tuple[RecordField, ...], count: int | None = None) -> RecordField:
    return RecordField(name, sum(map(span, fields)), "struct", count, fields)


def invalid(message: str, pointer: str) -> MubmdError:
    return MubmdError(ErrorCode.INVALID_VALUE, message, pointer=pointer)


def read_hex(value: JsonValue, pointer: str, size: int | None = None) -> bytes:
    try:
        decoded = bytes.fromhex(value) if isinstance(value, str) else None
    except ValueError:
        decoded = None
    if decoded is None or (size is not None and len(decoded) != size):
        limit = "a hex string" if size is None else f"a hex string of {size} bytes"
        raise invalid(f"Expected {limit}", pointer)
    return decoded


def _text_view(data: bytes, encoding: str) -> JsonValue:
    end = data.find(b"\0")
    text = decode_text(data[: end + 1], encoding) if end >= 0 else None
    if text is None:
        return {"hex": data.hex()}
    tail = data[end + 1 :].rstrip(b"\0")
    return {"text": text, "tail": tail.hex()} if tail else text


def _float_view(data: bytes) -> JsonValue:
    number: float = FLOAT32.unpack(data)[0]
    return number if isfinite(number) else {"f32": data.hex()}


def _leaf_view(field: RecordField, data: bytes, encoding: str) -> JsonValue:
    match field.kind:
        case "uint" | "int":
            return int.from_bytes(data, "little", signed=field.kind == "int")
        case "f32":
            return _float_view(data)
        case "text":
            return _text_view(data, encoding)
        case _:
            return {"hex": data.hex()}


def _element_view(field: RecordField, data: bytes, encoding: str) -> JsonValue:
    if field.kind == "struct":
        return fields_view(field.fields, data, encoding)
    return _leaf_view(field, data, encoding)


def _field_view(field: RecordField, data: bytes, encoding: str) -> JsonValue:
    if field.count is None:
        return _element_view(field, data, encoding)
    return [
        _element_view(field, data[start : start + field.size], encoding) for start in range(0, len(data), field.size)
    ]


def fields_view(fields: tuple[RecordField, ...], data: bytes, encoding: str) -> JsonObject:
    view: JsonObject = {}
    offset = 0
    for field in fields:
        size = span(field)
        view[field.name] = _field_view(field, data[offset : offset + size], encoding)
        offset += size
    return view


def _integer_bytes(field: RecordField, value: JsonValue, pointer: str) -> bytes:
    signed = field.kind == "int"
    bits = 8 * field.size
    lowest = -(1 << (bits - 1)) if signed else 0
    if type(value) is not int or not lowest <= value < lowest + (1 << bits):
        raise invalid(f"Expected {'a signed' if signed else 'an unsigned'} {bits}-bit integer", pointer)
    return value.to_bytes(field.size, "little", signed=signed)


def _float_bytes(value: JsonValue, pointer: str) -> bytes:
    if isinstance(value, dict) and value.keys() == {"f32"}:
        return read_hex(value["f32"], pointer, FLOAT32.size)
    if isinstance(value, int | float) and not isinstance(value, bool):
        with suppress(OverflowError):
            if isfinite(value):
                return FLOAT32.pack(value)
    raise invalid('Expected a finite number a float32 holds, or {"f32": "<8 hex digits>"}', pointer)


def _hex_bytes(field: RecordField, value: JsonValue, pointer: str, expected: str) -> bytes:
    if not isinstance(value, dict) or value.keys() != {"hex"}:
        raise invalid(expected, pointer)
    return read_hex(value["hex"], pointer, field.size)


def _tailed_text_bytes(field: RecordField, value: JsonObject, encoding: str, pointer: str) -> bytes:
    text = value["text"]
    if not isinstance(text, str):
        raise invalid("Expected a string as the text", pointer)
    tail = read_hex(value["tail"], pointer)
    padded = encode_fixed_text(text, encoding, field.size, pointer, field.name, require_terminator=True)
    body = padded[: padded.index(b"\0") + 1] + tail
    if len(body) > field.size:
        raise MubmdError(
            ErrorCode.TEXT_TOO_LONG,
            f"{field.name}, its 0 and its tail are {len(body)} bytes, the field holds {field.size}",
            pointer=pointer,
        )
    return body.ljust(field.size, b"\0")


def _text_bytes(field: RecordField, value: JsonValue, encoding: str, pointer: str) -> bytes:
    if isinstance(value, str):
        return encode_fixed_text(value, encoding, field.size, pointer, field.name, require_terminator=True)
    if isinstance(value, dict) and value.keys() == {"text", "tail"}:
        return _tailed_text_bytes(field, value, encoding, pointer)
    return _hex_bytes(field, value, pointer, "Expected a string, a text with its tail or a hex object")


def leaf_bytes(field: RecordField, value: JsonValue, encoding: str, pointer: str) -> bytes:
    match field.kind:
        case "uint" | "int":
            return _integer_bytes(field, value, pointer)
        case "f32":
            return _float_bytes(value, pointer)
        case "text":
            return _text_bytes(field, value, encoding, pointer)
        case _:
            return _hex_bytes(field, value, pointer, "Expected a hex object")


def _element_bytes(field: RecordField, value: JsonValue, encoding: str, pointer: str) -> bytes:
    if field.kind == "struct":
        return fields_bytes(field.fields, value, encoding, pointer)
    return leaf_bytes(field, value, encoding, pointer)


def _field_bytes(field: RecordField, value: JsonValue, encoding: str, pointer: str) -> bytes:
    if field.count is None:
        return _element_bytes(field, value, encoding, pointer)
    if not isinstance(value, list) or len(value) != field.count:
        raise invalid(f"Expected a list of {field.count} values", pointer)
    return b"".join(
        _element_bytes(field, element, encoding, f"{pointer}/{index}") for index, element in enumerate(value)
    )


def fields_bytes(fields: tuple[RecordField, ...], value: JsonValue, encoding: str, pointer: str) -> bytes:
    if not isinstance(value, dict) or value.keys() != {field.name for field in fields}:
        names = ", ".join(field.name for field in fields)
        raise invalid(f"Expected an object with exactly these fields: {names}", pointer)
    return b"".join(_field_bytes(field, value[field.name], encoding, f"{pointer}/{field.name}") for field in fields)


def _find_leaf(fields: tuple[RecordField, ...], tokens: Sequence[str]) -> RecordField | None:
    if not tokens:
        return None
    field = next((field for field in fields if field.name == tokens[0]), None)
    if field is None:
        return None
    rest = tokens[1:]
    if field.count is not None:
        if not rest or list_index(rest[0], field.count) is None:
            return None
        rest = rest[1:]
    if field.kind == "struct":
        return _find_leaf(field.fields, rest)
    return None if rest else field


def leaf_at(fields: tuple[RecordField, ...], tokens: Sequence[str], pointer: str) -> RecordField:
    leaf = _find_leaf(fields, tokens)
    if leaf is None:
        raise MubmdError(ErrorCode.POINTER_NOT_FOUND, f"No single value at {pointer}", pointer=pointer)
    return leaf


def _client_range(field: RecordField) -> str | None:
    if field.lowest is None and field.highest is None:
        return None
    if field.lowest is None:
        return f"at most {field.highest}"
    if field.highest is None:
        return f"at least {field.lowest}"
    return f"{field.lowest} to {field.highest}"


def _limit(field: RecordField) -> str:
    size = f"{field.size} bytes"
    client_range = _client_range(field)
    return size if client_range is None else f"{size}; the client handles {client_range}"


def _describe(fields: tuple[RecordField, ...], prefix: str, reference: str, start: int) -> Iterator[FieldInfo]:
    offset = start
    for field in fields:
        pointer = f"{prefix}/{field.name}"
        if field.count is not None:
            pointer += f"/<0..{field.count - 1}>"
        if field.kind == "struct":
            yield from _describe(field.fields, pointer, reference, offset)
        else:
            yield FieldInfo(pointer, field.kind, _limit(field), reference, offset)
        offset += span(field)


def describe(fields: tuple[RecordField, ...], prefix: str, reference: str) -> tuple[FieldInfo, ...]:
    return tuple(_describe(fields, prefix, reference, 0))


def has_client_range(field: RecordField) -> bool:
    return _client_range(field) is not None or any(map(has_client_range, field.fields))


def value_out_of_range(field: RecordField, value: JsonValue, pointer: str) -> Iterator[Finding]:
    if field.kind == "struct":
        if isinstance(value, dict):
            yield from out_of_range(field.fields, value, pointer)
        return
    if type(value) is not int:
        return
    too_low = field.lowest is not None and value < field.lowest
    too_high = field.highest is not None and value > field.highest
    if too_low or too_high:
        message = f"{value} is outside what the client handles ({_client_range(field)})"
        yield Finding("OUT_OF_RANGE", Severity.ERROR, pointer, message)


def out_of_range(fields: tuple[RecordField, ...], value: JsonObject, pointer: str) -> Iterator[Finding]:
    for field in filter(has_client_range, fields):
        place = f"{pointer}/{field.name}"
        item = value.get(field.name)
        if field.count is None:
            yield from value_out_of_range(field, item, place)
        elif isinstance(item, list):
            for index, element in enumerate(item):
                yield from value_out_of_range(field, element, f"{place}/{index}")
