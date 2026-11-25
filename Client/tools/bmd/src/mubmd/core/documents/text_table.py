from dataclasses import dataclass, replace
from typing import Final

from mubmd.core.errors import ErrorCode, MubmdError

TEXT_RECORD_COUNT: Final = 2048
TEXT_RECORD_SIZE: Final = 300


def text_pointer(index: int) -> str:
    return f"/texts/{index}"


def is_text_encoding(name: str) -> bool:
    try:
        return "a".encode(name).decode(name) == "a"
    except (LookupError, UnicodeError):
        return False


def encode_text(text: str, encoding: str, index: int) -> bytes:
    pointer = text_pointer(index)
    if "\0" in text:
        raise MubmdError(ErrorCode.INVALID_VALUE, f"Text {index} contains a NUL character", pointer=pointer)
    try:
        body = text.encode(encoding)
    except UnicodeEncodeError as error:
        character = error.object[error.start : error.end]
        raise MubmdError(
            ErrorCode.NOT_ENCODABLE,
            f"Text {index}: {character!r} cannot be written in {encoding}",
            pointer=pointer,
        ) from error
    if b"\0" in body or body.decode(encoding, errors="replace") != text:
        raise MubmdError(
            ErrorCode.NOT_ENCODABLE,
            f"Text {index} does not read back the same from its bytes in {encoding}",
            pointer=pointer,
        )
    if len(body) > TEXT_RECORD_SIZE:
        raise MubmdError(
            ErrorCode.TEXT_TOO_LONG,
            f"Text {index} is {len(body)} bytes, the record holds {TEXT_RECORD_SIZE}",
            pointer=pointer,
        )
    return body.ljust(TEXT_RECORD_SIZE, b"\0")


def has_tail(record: bytes) -> bool:
    end = record.find(b"\0")
    return end >= 0 and any(record[end:])


def decode_text(record: bytes, encoding: str) -> str | None:
    if has_tail(record):
        return None
    end = record.find(b"\0")
    if end < 0:
        end = len(record)
    try:
        text = record[:end].decode(encoding)
    except UnicodeDecodeError:
        return None
    return text if text.encode(encoding) == record[:end] else None


@dataclass(frozen=True, slots=True)
class TextTable:
    records: tuple[bytes, ...]
    encoding: str
    trailing: bytes = b""

    def _check_index(self, index: int) -> None:
        if not 0 <= index < len(self.records):
            raise MubmdError(
                ErrorCode.POINTER_NOT_FOUND,
                f"No text {index}: the table has texts 0 to {len(self.records) - 1}",
                pointer=text_pointer(index),
            )

    def text(self, index: int) -> str | None:
        self._check_index(index)
        return decode_text(self.records[index], self.encoding)

    def with_text(self, index: int, text: str) -> "TextTable":
        self._check_index(index)
        records = list(self.records)
        records[index] = encode_text(text, self.encoding, index)
        return replace(self, records=tuple(records))
