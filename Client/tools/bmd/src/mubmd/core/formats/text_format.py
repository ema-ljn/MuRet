import codecs
from typing import Final

from mubmd.core.checks.text_checks import check_text_table
from mubmd.core.ciphers.xor3 import xor3
from mubmd.core.documents.text_table import (
    TEXT_RECORD_COUNT,
    TEXT_RECORD_SIZE,
    TextTable,
    decode_text,
    encode_text,
    text_pointer,
)
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.ports import CatalogFile, FieldInfo, Summary

VIEW_VERSION: Final = 1
TEXT_TABLE_SIZE: Final = TEXT_RECORD_COUNT * TEXT_RECORD_SIZE
DEFAULT_ENCODING: Final = "cp1252"


def _invalid(message: str, pointer: str) -> MubmdError:
    return MubmdError(ErrorCode.INVALID_VALUE, message, pointer=pointer)


def _hex_record(value: dict[str, JsonValue], index: int) -> bytes:
    raw = value.get("hex")
    try:
        record = bytes.fromhex(raw) if isinstance(raw, str) else b""
    except ValueError:
        record = b""
    if len(record) != TEXT_RECORD_SIZE:
        raise _invalid(f"Text {index}: a hex record has {TEXT_RECORD_SIZE * 2} hex digits", text_pointer(index))
    return record


def _check_header(view: JsonObject) -> str:
    if view.get("format_version") != VIEW_VERSION:
        raise _invalid(f"This tool reads format_version {VIEW_VERSION} views", "/format_version")
    if view.get("format") != "text":
        raise _invalid("Not a text view", "/format")
    encoding = view.get("encoding")
    if not isinstance(encoding, str):
        raise _invalid("The view needs an encoding", "/encoding")
    try:
        codecs.lookup(encoding)
    except LookupError as error:
        raise _invalid(f"Unknown encoding {encoding!r}", "/encoding") from error
    return encoding


class TextFormat:
    name = "text"

    def decode(self, data: bytes, file: CatalogFile) -> TextTable:
        if len(data) < TEXT_TABLE_SIZE:
            raise MubmdError(
                ErrorCode.CORRUPT_FILE,
                f"{file.name} has {len(data)} bytes, a text table has at least {TEXT_TABLE_SIZE}",
            )
        plain = xor3(data[:TEXT_TABLE_SIZE])
        records = tuple(
            plain[start : start + TEXT_RECORD_SIZE] for start in range(0, TEXT_TABLE_SIZE, TEXT_RECORD_SIZE)
        )
        return TextTable(records, file.encoding or DEFAULT_ENCODING, data[TEXT_TABLE_SIZE:])

    def encode(self, document: TextTable) -> bytes:
        return xor3(b"".join(document.records)) + document.trailing

    def to_view(self, document: TextTable) -> JsonObject:
        texts: list[JsonValue] = []
        for record in document.records:
            text = decode_text(record, document.encoding)
            texts.append(text if text is not None else {"hex": record.hex()})
        view: JsonObject = {
            "format_version": VIEW_VERSION,
            "format": self.name,
            "encoding": document.encoding,
            "texts": texts,
        }
        if document.trailing:
            view["trailing"] = document.trailing.hex()
        return view

    def from_view(self, view: JsonObject) -> TextTable:
        encoding = _check_header(view)
        texts = view.get("texts")
        if not isinstance(texts, list) or len(texts) != TEXT_RECORD_COUNT:
            raise _invalid(f"A text view holds {TEXT_RECORD_COUNT} texts", "/texts")
        records: list[bytes] = []
        for index, value in enumerate(texts):
            if isinstance(value, str):
                records.append(encode_text(value, encoding, index))
            elif isinstance(value, dict):
                records.append(_hex_record(value, index))
            else:
                raise _invalid(f"Text {index} is a string or {{'hex': ...}}", text_pointer(index))
        trailing = view.get("trailing", "")
        try:
            tail = bytes.fromhex(trailing) if isinstance(trailing, str) else None
        except ValueError:
            tail = None
        if tail is None:
            raise _invalid("trailing is a hex string", "/trailing")
        return TextTable(tuple(records), encoding, tail)

    def check(self, document: TextTable, original: TextTable | None) -> tuple[Finding, ...]:
        return check_text_table(document, original)

    def describe_fields(self) -> tuple[FieldInfo, ...]:
        return (
            FieldInfo(
                f"/texts/<0..{TEXT_RECORD_COUNT - 1}>",
                "string",
                f"{TEXT_RECORD_SIZE} bytes in the file's encoding",
                "Client/docs/research/05-formatos-de-datos.md#tablas-bmd",
            ),
        )

    def summarize(self, document: TextTable) -> Summary:
        return Summary("XOR3", len(document.records), sum(1 for record in document.records if any(record)))
