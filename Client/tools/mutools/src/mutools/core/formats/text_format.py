from typing import Final

from mutools.core.checks.text_checks import check_text_record, check_text_table
from mutools.core.ciphers.xor3 import xor3
from mutools.core.documents.text_table import (
    TEXT_RECORD_COUNT,
    TEXT_RECORD_SIZE,
    TextTable,
    decode_text,
    encode_text,
    is_text_encoding,
    text_pointer,
)
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.findings import Finding
from mutools.core.json_types import VIEW_VERSION, JsonObject, JsonValue
from mutools.core.pointer import list_index, parse_pointer
from mutools.core.ports import CatalogFile, FieldInfo, Summary, ValueCheck

TEXT_TABLE_SIZE: Final = TEXT_RECORD_COUNT * TEXT_RECORD_SIZE
DEFAULT_ENCODING: Final = "cp1252"


def _invalid(message: str, pointer: str) -> MutoolsError:
    return MutoolsError(ErrorCode.INVALID_VALUE, message, pointer=pointer)


def _hex_record(value: dict[str, JsonValue], index: int) -> bytes:
    raw = value.get("hex")
    try:
        record = bytes.fromhex(raw) if isinstance(raw, str) else b""
    except ValueError:
        record = b""
    if len(record) != TEXT_RECORD_SIZE:
        raise _invalid(f"Text {index}: a hex record has {TEXT_RECORD_SIZE * 2} hex digits", text_pointer(index))
    return record


def _record(value: JsonValue, encoding: str, index: int) -> bytes:
    if isinstance(value, str):
        return encode_text(value, encoding, index)
    if isinstance(value, dict):
        return _hex_record(value, index)
    raise _invalid(f"Text {index} is a string or {{'hex': ...}}", text_pointer(index))


def _check_header(view: JsonObject) -> str:
    if view.get("format_version") != VIEW_VERSION:
        raise _invalid(f"This tool reads format_version {VIEW_VERSION} views", "/format_version")
    if view.get("format") != "text":
        raise _invalid("Not a text view", "/format")
    encoding = view.get("encoding")
    if not isinstance(encoding, str):
        raise _invalid("The view needs an encoding", "/encoding")
    if not is_text_encoding(encoding):
        raise _invalid(f"{encoding!r} is not a text encoding", "/encoding")
    return encoding


class TextFormat:
    name = "text"

    def decode(self, data: bytes, file: CatalogFile) -> TextTable:
        if len(data) < TEXT_TABLE_SIZE:
            raise MutoolsError(
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
        records = tuple(_record(value, encoding, index) for index, value in enumerate(texts))
        trailing = view.get("trailing", "")
        try:
            tail = bytes.fromhex(trailing) if isinstance(trailing, str) else None
        except ValueError:
            tail = None
        if tail is None:
            raise _invalid("trailing is a hex string", "/trailing")
        return TextTable(records, encoding, tail)

    def check(self, document: TextTable, original: TextTable | None) -> tuple[Finding, ...]:
        return check_text_table(document, original)

    def check_value(self, pointer: str, value: JsonValue, original: TextTable) -> ValueCheck:
        tokens = parse_pointer(pointer)
        is_text = len(tokens) == len(("texts", "N")) and tokens[0] == "texts"
        index = list_index(tokens[1], len(original.records)) if is_text else None
        if index is None:
            last = text_pointer(len(original.records) - 1)
            raise MutoolsError(
                ErrorCode.POINTER_NOT_FOUND, f"No text at {pointer!r}: texts are /texts/0 to {last}", pointer=pointer
            )
        record = _record(value, original.encoding, index)
        findings = check_text_record(index, record, original.records[index], original.encoding)
        return ValueCheck(TEXT_RECORD_SIZE, findings)

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
