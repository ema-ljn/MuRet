from collections.abc import Iterator
from dataclasses import dataclass
from hashlib import sha256
from struct import Struct
from typing import Final

from mubmd.core.ciphers.table_checksum import CHECKSUM_WORD, WORD_MASK, table_checksum
from mubmd.core.ciphers.xor3 import xor3
from mubmd.core.documents.record_table import RecordTable
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding, Severity
from mubmd.core.formats.record_fields import (
    RecordField,
    describe,
    fields_bytes,
    fields_view,
    has_client_range,
    invalid,
    leaf_at,
    leaf_bytes,
    out_of_range,
    read_hex,
    span,
    value_out_of_range,
    view_encoding,
)
from mubmd.core.json_types import VIEW_VERSION, JsonObject, JsonValue
from mubmd.core.pointer import list_index, parse_pointer
from mubmd.core.ports import CatalogFile, FieldInfo, Summary, ValueCheck

COUNT_WORD: Final = Struct("<i")
DEFAULT_ENCODING: Final = "cp1252"
RECORD_POINTER_TOKENS: Final = len(("records", "N"))


@dataclass(frozen=True, slots=True)
class RecordTableSchema:
    name: str
    count: int | None
    record_size: int
    fields: tuple[RecordField, ...]
    checksum_key: int | None
    reference: str
    xor_odd_words: bool = True
    section_size: int | None = None


class RecordTableFormat:
    def __init__(self, schema: RecordTableSchema) -> None:
        if sum(map(span, schema.fields)) != schema.record_size:
            raise ValueError(f"The {schema.name} fields do not cover its record size")
        self.name = schema.name
        self.schema = schema
        self._has_client_ranges = any(map(has_client_range, schema.fields))
        footer_size = 0 if schema.checksum_key is None else CHECKSUM_WORD.size
        self.file_size = None if schema.count is None else schema.count * schema.record_size + footer_size

    def _records_span(self, data: bytes, file: CatalogFile) -> tuple[int, int]:
        size = self.schema.record_size
        if self.schema.count is not None:
            end = self.schema.count * size
            if len(data) < end:
                raise MubmdError(ErrorCode.CORRUPT_FILE, f"{file.name} has {len(data)} bytes, its records need {end}")
            return 0, end
        if len(data) < COUNT_WORD.size:
            raise MubmdError(
                ErrorCode.CORRUPT_FILE,
                f"{file.name} has {len(data)} bytes, it starts with a count of {COUNT_WORD.size}",
            )
        (count,) = COUNT_WORD.unpack_from(data)
        held = (len(data) - COUNT_WORD.size) // size
        if not 0 <= count <= held:
            raise MubmdError(ErrorCode.CORRUPT_FILE, f"{file.name} says it has {count} records, the file holds {held}")
        return COUNT_WORD.size, COUNT_WORD.size + count * size

    def decode(self, data: bytes, file: CatalogFile) -> RecordTable:
        start, end = self._records_span(data, file)
        size = self.schema.record_size
        records = tuple(xor3(data[offset : offset + size]) for offset in range(start, end, size))
        footer = data[end:]
        encoding = file.encoding or DEFAULT_ENCODING
        if self.schema.checksum_key is None or len(footer) < CHECKSUM_WORD.size:
            return RecordTable(records, encoding, None, footer)
        (stored,) = CHECKSUM_WORD.unpack_from(footer)
        return RecordTable(records, encoding, stored, footer[CHECKSUM_WORD.size :])

    def encode(self, document: RecordTable) -> bytes:
        count = b"" if self.schema.count is not None else COUNT_WORD.pack(len(document.records))
        checksum = b"" if document.stored_checksum is None else CHECKSUM_WORD.pack(document.stored_checksum)
        return count + b"".join(map(xor3, document.records)) + checksum + document.trailing

    def to_view(self, document: RecordTable) -> JsonObject:
        records: list[JsonValue] = [
            fields_view(self.schema.fields, record, document.encoding) for record in document.records
        ]
        view: JsonObject = {
            "format_version": VIEW_VERSION,
            "format": self.name,
            "encoding": document.encoding,
            "records": records,
        }
        if self.schema.checksum_key is not None:
            view["records_sha256"] = sha256(b"".join(document.records)).hexdigest()
            view["stored_checksum"] = document.stored_checksum
        view["trailing"] = document.trailing.hex()
        return view

    def from_view(self, view: JsonObject) -> RecordTable:
        encoding = view_encoding(view, self.name)
        values = view.get("records")
        count = self.schema.count
        if not isinstance(values, list) or (count is not None and len(values) != count):
            expected = "The records must be a list" if count is None else f"The table must contain {count} records"
            raise invalid(expected, "/records")
        fields = self.schema.fields
        records = tuple(
            fields_bytes(fields, value, encoding, f"/records/{index}") for index, value in enumerate(values)
        )
        trailing = read_hex(view.get("trailing"), "/trailing")
        if self.schema.checksum_key is None:
            return RecordTable(records, encoding, None, trailing)
        checksum = self._view_checksum(view, records, self.schema.checksum_key)
        if checksum is None and len(trailing) >= CHECKSUM_WORD.size:
            raise invalid("Bytes after the records must follow the checksum", "/trailing")
        return RecordTable(records, encoding, checksum, trailing)

    def _view_checksum(self, view: JsonObject, records: tuple[bytes, ...], key: int) -> int | None:
        digest = read_hex(view.get("records_sha256"), "/records_sha256", sha256().digest_size)
        if "stored_checksum" not in view:
            raise invalid("The view needs a stored checksum or null", "/stored_checksum")
        stored = view["stored_checksum"]
        if stored is not None and (type(stored) is not int or not 0 <= stored <= WORD_MASK):
            raise invalid("Expected an unsigned 32-bit checksum or null", "/stored_checksum")
        if sha256(b"".join(records)).digest() != digest:
            return self._checksum(records, key)
        return stored

    def _checksum(self, records: tuple[bytes, ...], key: int) -> int:
        return table_checksum(b"".join(map(xor3, records)), key, xor_odd_words=self.schema.xor_odd_words)

    def _checksum_findings(self, document: RecordTable) -> Iterator[Finding]:
        if self.schema.checksum_key is None:
            return
        expected = self._checksum(document.records, self.schema.checksum_key)
        stored = document.stored_checksum
        if expected != stored:
            message = (
                "The stored checksum is missing"
                if stored is None
                else f"Stored 0x{stored:08X}, calculated 0x{expected:08X}"
            )
            yield Finding("CHECKSUM_MISMATCH", Severity.WARNING, "/stored_checksum", message)

    def _range_findings(self, document: RecordTable) -> Iterator[Finding]:
        if not self._has_client_ranges:
            return
        fields = self.schema.fields
        for index, record in enumerate(document.records):
            yield from out_of_range(fields, fields_view(fields, record, document.encoding), f"/records/{index}")

    def check(self, document: RecordTable, original: RecordTable | None) -> tuple[Finding, ...]:
        return (*self._checksum_findings(document), *self._range_findings(document))

    def check_value(self, pointer: str, value: JsonValue, original: RecordTable) -> ValueCheck:
        tokens = parse_pointer(pointer)
        in_a_record = len(tokens) >= RECORD_POINTER_TOKENS and tokens[0] == "records"
        if not in_a_record or list_index(tokens[1], len(original.records)) is None:
            raise MubmdError(
                ErrorCode.POINTER_NOT_FOUND,
                f"No value at {pointer!r}: the table has records 0 to {len(original.records) - 1}",
                pointer=pointer,
            )
        leaf = leaf_at(self.schema.fields, tokens[RECORD_POINTER_TOKENS:], pointer)
        leaf_bytes(leaf, value, original.encoding, pointer)
        limit = leaf.size - 1 if leaf.kind == "text" else None
        return ValueCheck(limit, tuple(value_out_of_range(leaf, value, pointer)))

    def describe_fields(self) -> tuple[FieldInfo, ...]:
        count = self.schema.count
        indices = "n" if count is None else f"0..{count - 1}"
        return describe(self.schema.fields, f"/records/<{indices}>", self.schema.reference)

    def summarize(self, document: RecordTable) -> Summary:
        used = sum(1 for record in document.records if any(record))
        return Summary("XOR3 per record", len(document.records), used, self.schema.section_size)
