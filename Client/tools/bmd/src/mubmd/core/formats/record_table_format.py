from dataclasses import dataclass
from hashlib import sha256
from typing import Literal

from mubmd.core.ciphers.table_checksum import CHECKSUM_WORD, WORD_MASK, table_checksum
from mubmd.core.ciphers.xor3 import xor3
from mubmd.core.documents.record_table import RecordTable
from mubmd.core.documents.text_table import decode_text, encode_fixed_text, is_text_encoding
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding, Severity
from mubmd.core.json_types import VIEW_VERSION, JsonObject, JsonValue
from mubmd.core.ports import CatalogFile, FieldInfo, Summary


@dataclass(frozen=True, slots=True)
class RecordField:
    name: str
    size: int
    kind: Literal["text", "bytes", "uint"]


@dataclass(frozen=True, slots=True)
class RecordTableSchema:
    name: str
    count: int
    record_size: int
    fields: tuple[RecordField, ...]
    checksum_key: int
    xor_odd_words: bool
    reference: str


def _invalid(message: str, pointer: str) -> MubmdError:
    return MubmdError(ErrorCode.INVALID_VALUE, message, pointer=pointer)


def _read_hex(value: JsonValue, pointer: str, size: int | None = None) -> bytes:
    try:
        decoded = bytes.fromhex(value) if isinstance(value, str) else None
    except ValueError:
        decoded = None
    if decoded is None or (size is not None and len(decoded) != size):
        limit = "a hex string" if size is None else f"a hex string of {size} bytes"
        raise _invalid(f"Expected {limit}", pointer)
    return decoded


def _field_view(field: RecordField, data: bytes, encoding: str) -> JsonValue:
    if field.kind == "uint":
        return int.from_bytes(data, "little")
    if field.kind == "text" and b"\0" in data:
        text = decode_text(data, encoding)
        if text is not None:
            return text
    return {"hex": data.hex()}


def _field_bytes(field: RecordField, value: JsonValue, encoding: str, pointer: str) -> bytes:
    if field.kind == "uint":
        if type(value) is not int or not 0 <= value < 1 << (8 * field.size):
            raise _invalid(f"Expected an unsigned {8 * field.size}-bit integer", pointer)
        return value.to_bytes(field.size, "little")
    if field.kind == "text" and isinstance(value, str):
        return encode_fixed_text(value, encoding, field.size, pointer, field.name, require_terminator=True)
    if not isinstance(value, dict) or value.keys() != {"hex"}:
        raise _invalid("Expected a hex object" if field.kind == "bytes" else "Expected a string or hex object", pointer)
    return _read_hex(value["hex"], pointer, field.size)


def _record_view(fields: tuple[RecordField, ...], record: bytes, encoding: str) -> JsonObject:
    result: JsonObject = {}
    offset = 0
    for field in fields:
        result[field.name] = _field_view(field, record[offset : offset + field.size], encoding)
        offset += field.size
    return result


def _record_bytes(fields: tuple[RecordField, ...], value: JsonValue, encoding: str, index: int) -> bytes:
    pointer = f"/records/{index}"
    if not isinstance(value, dict) or value.keys() != {field.name for field in fields}:
        raise _invalid("The record must contain exactly the fields in its schema", pointer)
    return b"".join(_field_bytes(field, value[field.name], encoding, f"{pointer}/{field.name}") for field in fields)


def _view_encoding(view: JsonObject, name: str) -> str:
    if type(view.get("format_version")) is not int or view["format_version"] != VIEW_VERSION:
        raise _invalid(f"This tool reads format_version {VIEW_VERSION} views", "/format_version")
    if view.get("format") != name:
        raise _invalid(f"Not a {name} view", "/format")
    encoding = view.get("encoding")
    if not isinstance(encoding, str) or not is_text_encoding(encoding):
        raise _invalid("The view needs a text encoding", "/encoding")
    return encoding


class RecordTableFormat:
    def __init__(self, schema: RecordTableSchema) -> None:
        if sum(field.size for field in schema.fields) != schema.record_size:
            raise ValueError(f"The {schema.name} fields do not cover its record size")
        self.name = schema.name
        self._schema = schema
        self._table_size = schema.count * schema.record_size

    def decode(self, data: bytes, file: CatalogFile) -> RecordTable:
        if len(data) < self._table_size:
            raise MubmdError(
                ErrorCode.CORRUPT_FILE, f"{file.name} has {len(data)} bytes, its records need {self._table_size}"
            )
        size = self._schema.record_size
        records = tuple(xor3(data[start : start + size]) for start in range(0, self._table_size, size))
        footer = data[self._table_size :]
        if len(footer) < CHECKSUM_WORD.size:
            return RecordTable(records, file.encoding or "cp1252", None, footer)
        (stored,) = CHECKSUM_WORD.unpack_from(footer)
        return RecordTable(records, file.encoding or "cp1252", stored, footer[CHECKSUM_WORD.size :])

    def encode(self, document: RecordTable) -> bytes:
        checksum = b"" if document.stored_checksum is None else CHECKSUM_WORD.pack(document.stored_checksum)
        return b"".join(map(xor3, document.records)) + checksum + document.trailing

    def to_view(self, document: RecordTable) -> JsonObject:
        records: list[JsonValue] = [
            _record_view(self._schema.fields, record, document.encoding) for record in document.records
        ]
        return {
            "format_version": VIEW_VERSION,
            "format": self.name,
            "encoding": document.encoding,
            "records": records,
            "records_sha256": sha256(b"".join(document.records)).hexdigest(),
            "stored_checksum": document.stored_checksum,
            "trailing": document.trailing.hex(),
        }

    def from_view(self, view: JsonObject) -> RecordTable:
        encoding = _view_encoding(view, self.name)
        values = view.get("records")
        if not isinstance(values, list) or len(values) != self._schema.count:
            raise _invalid(f"The table must contain {self._schema.count} records", "/records")
        fields = self._schema.fields
        records = tuple(_record_bytes(fields, value, encoding, index) for index, value in enumerate(values))
        trailing = _read_hex(view.get("trailing"), "/trailing")
        checksum = self._view_checksum(view, records)
        if checksum is None and len(trailing) >= CHECKSUM_WORD.size:
            raise _invalid("Bytes after the records must follow the checksum", "/trailing")
        return RecordTable(records, encoding, checksum, trailing)

    def _view_checksum(self, view: JsonObject, records: tuple[bytes, ...]) -> int | None:
        digest = _read_hex(view.get("records_sha256"), "/records_sha256", sha256().digest_size)
        if "stored_checksum" not in view:
            raise _invalid("The view needs a stored checksum or null", "/stored_checksum")
        stored = view["stored_checksum"]
        if stored is not None and (type(stored) is not int or not 0 <= stored <= WORD_MASK):
            raise _invalid("Expected an unsigned 32-bit checksum or null", "/stored_checksum")
        if sha256(b"".join(records)).digest() != digest:
            return self._checksum(records)
        return stored

    def _checksum(self, records: tuple[bytes, ...]) -> int:
        return table_checksum(
            b"".join(map(xor3, records)), self._schema.checksum_key, xor_odd_words=self._schema.xor_odd_words
        )

    def check(self, document: RecordTable, original: RecordTable | None) -> tuple[Finding, ...]:
        expected = self._checksum(document.records)
        stored = document.stored_checksum
        if expected == stored:
            return ()
        message = (
            "The stored checksum is missing"
            if stored is None
            else f"Stored 0x{stored:08X}, calculated 0x{expected:08X}"
        )
        return (Finding("CHECKSUM_MISMATCH", Severity.WARNING, "/stored_checksum", message),)

    def describe_fields(self) -> tuple[FieldInfo, ...]:
        prefix = f"/records/<0..{self._schema.count - 1}>"
        return tuple(
            FieldInfo(f"{prefix}/{field.name}", field.kind, f"{field.size} bytes", self._schema.reference)
            for field in self._schema.fields
        )

    def summarize(self, document: RecordTable) -> Summary:
        return Summary("XOR3 per record", len(document.records), sum(1 for record in document.records if any(record)))
