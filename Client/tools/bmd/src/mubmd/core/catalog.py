from dataclasses import dataclass
from typing import Any

from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.formats.text_format import TEXT_TABLE_SIZE, TextFormat
from mubmd.core.ports import CatalogFile, FormatCodec


@dataclass(frozen=True, slots=True)
class FormatSpec:
    name: str
    size: int
    files: tuple[CatalogFile, ...]
    codec: FormatCodec[Any]


@dataclass(frozen=True, slots=True)
class Identified:
    format: FormatSpec
    file: CatalogFile


_FORMATS = (
    FormatSpec(
        "text",
        TEXT_TABLE_SIZE,
        (
            CatalogFile("Text.bmd", "cp1252", read_by_client=True),
            CatalogFile("Texttest.bmd", "cp1252", read_by_client=False),
            CatalogFile("TKOU.bmd", "gb2312", read_by_client=False),
        ),
        TextFormat(),
    ),
)


def all_formats() -> tuple[FormatSpec, ...]:
    return _FORMATS


def format_by_name(name: str) -> FormatSpec:
    for spec in _FORMATS:
        if spec.name == name:
            return spec
    known = ", ".join(spec.name for spec in _FORMATS)
    raise MubmdError(ErrorCode.UNKNOWN_FORMAT, f"No format {name!r}; known: {known}", suggestion="mubmd formats")


def identify(file_name: str, data: bytes) -> Identified:
    wanted = file_name.casefold()
    for spec in _FORMATS:
        for file in spec.files:
            if file.name.casefold() == wanted:
                return Identified(spec, file)
    raise MubmdError(
        ErrorCode.UNKNOWN_FORMAT,
        f"{file_name} is not a format this version reads",
        suggestion="mubmd formats",
    )
