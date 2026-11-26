from dataclasses import dataclass
from hashlib import sha256

from mubmd.core.ciphers.table_checksum import CHECKSUM_WORD, WORD_MASK, table_checksum
from mubmd.core.ciphers.xor3 import xor3
from mubmd.core.documents.record_table import RecordTable
from mubmd.core.documents.text_table import is_text_encoding
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding, Severity
from mubmd.core.formats.record_fields import RecordField, describe, fields_bytes, fields_view, invalid, read_hex, span
from mubmd.core.json_types import VIEW_VERSION, JsonObject, JsonValue
from mubmd.core.ports import CatalogFile, FieldInfo, Summary


@dataclass(frozen=True, slots=True)
class RecordTableSchema:
    name: str
    count: int
    record_size: int
    fields: tuple[RecordField, ...]
    checksum_key: int
    xor_odd_words: bool
    reference: str


def _view_encoding(view: JsonObject, name: str) -> str:
    if type(view.get("format_version")) is not int or view["format_version"] != VIEW_VERSION:
        raise invalid(f"This tool reads format_version {VIEW_VERSION} views", "/format_version")
    if view.get("format") != name:
        raise invalid(f"Not a {name} view", "/format")
    encoding = view.get("encoding")
    if not isinstance(encoding, str) or not is_text_encoding(encoding):
        raise invalid("The view needs a text encoding", "/encoding")
    return encoding


class RecordTableFormat:
    def __init__(self, schema: RecordTableSchema) -> None:
        if sum(map(span, schema.fields)) != schema.record_size:
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
            fields_view(self._schema.fields, record, document.encoding) for record in document.records
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
            raise invalid(f"The table must contain {self._schema.count} records", "/records")
        fields = self._schema.fields
        records = tuple(
            fields_bytes(fields, value, encoding, f"/records/{index}") for index, value in enumerate(values)
        )
        trailing = read_hex(view.get("trailing"), "/trailing")
        checksum = self._view_checksum(view, records)
        if checksum is None and len(trailing) >= CHECKSUM_WORD.size:
            raise invalid("Bytes after the records must follow the checksum", "/trailing")
        return RecordTable(records, encoding, checksum, trailing)

    def _view_checksum(self, view: JsonObject, records: tuple[bytes, ...]) -> int | None:
        digest = read_hex(view.get("records_sha256"), "/records_sha256", sha256().digest_size)
        if "stored_checksum" not in view:
            raise invalid("The view needs a stored checksum or null", "/stored_checksum")
        stored = view["stored_checksum"]
        if stored is not None and (type(stored) is not int or not 0 <= stored <= WORD_MASK):
            raise invalid("Expected an unsigned 32-bit checksum or null", "/stored_checksum")
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
        return describe(self._schema.fields, f"/records/<0..{self._schema.count - 1}>", self._schema.reference)

    def summarize(self, document: RecordTable) -> Summary:
        return Summary("XOR3 per record", len(document.records), sum(1 for record in document.records if any(record)))
