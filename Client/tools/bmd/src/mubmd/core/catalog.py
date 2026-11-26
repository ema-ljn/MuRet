from dataclasses import dataclass
from pathlib import Path
from typing import Any

from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.formats.credit_format import CREDIT_FORMAT
from mubmd.core.formats.dialog_format import DIALOG_FORMAT
from mubmd.core.formats.filter_format import FILTER_FORMAT, FILTER_NAME_FORMAT
from mubmd.core.formats.gate_format import GATE_FORMAT
from mubmd.core.formats.item_format import ITEM_FORMAT
from mubmd.core.formats.item_set_format import ITEM_SET_OPTION_FORMAT, ITEM_SET_TYPE_FORMAT
from mubmd.core.formats.monster_skill_format import MONSTER_SKILL_FORMAT
from mubmd.core.formats.move_requirement_format import MOVE_REQUIREMENT_FORMAT
from mubmd.core.formats.quest_format import QUEST_FORMAT
from mubmd.core.formats.record_table_format import RecordTableFormat
from mubmd.core.formats.skill_format import SKILL_FORMAT
from mubmd.core.formats.slide_format import SLIDE_FORMAT
from mubmd.core.formats.text_format import TEXT_TABLE_SIZE, TextFormat
from mubmd.core.ports import CatalogFile, FormatCodec


@dataclass(frozen=True, slots=True)
class FormatSpec:
    name: str
    size: int | None
    files: tuple[CatalogFile, ...]
    codec: FormatCodec[Any]
    model_folder: str | None = None


@dataclass(frozen=True, slots=True)
class Identified:
    format: FormatSpec
    file: CatalogFile


def _client_file(name: str, encoding: str | None = None) -> CatalogFile:
    return CatalogFile(name, encoding, read_by_client=True)


def _other_file(name: str, encoding: str | None = None) -> CatalogFile:
    return CatalogFile(name, encoding, read_by_client=False)


def _table(codec: RecordTableFormat, *files: CatalogFile, model_folder: str | None = None) -> FormatSpec:
    return FormatSpec(codec.name, codec.file_size, files, codec, model_folder)


_FORMATS = (
    FormatSpec(
        "text",
        TEXT_TABLE_SIZE,
        (_client_file("Text.bmd", "cp1252"), _other_file("Texttest.bmd", "cp1252"), _other_file("TKOU.bmd", "gb2312")),
        TextFormat(),
    ),
    _table(
        ITEM_FORMAT,
        _client_file("item.bmd", "cp1252"),
        _other_file("itemtest.bmd", "cp949"),
        _other_file("n380.bmd", "gb2312"),
        model_folder="item",
    ),
    _table(
        SKILL_FORMAT, _client_file("skill.bmd", "cp1252"), _other_file("Skilltest.bmd", "cp949"), model_folder="skill"
    ),
    _table(GATE_FORMAT, _client_file("Gate.bmd"), _other_file("Gatetest.bmd")),
    _table(FILTER_FORMAT, _client_file("Filter.bmd", "cp1252")),
    _table(FILTER_NAME_FORMAT, _client_file("FilterName.bmd", "cp1252")),
    _table(ITEM_SET_TYPE_FORMAT, _client_file("itemsettype.bmd"), _other_file("itemsettypetest.bmd")),
    _table(
        ITEM_SET_OPTION_FORMAT,
        _client_file("itemsetoption.bmd", "cp1252"),
        _other_file("itemsetoptiontest.bmd", "cp949"),
    ),
    _table(CREDIT_FORMAT, _client_file("credit.bmd", "cp1252"), _other_file("credittest.bmd", "cp1252")),
    _table(SLIDE_FORMAT, _client_file("slide.bmd", "cp1252"), _other_file("slidetest.bmd", "cp949")),
    _table(
        MOVE_REQUIREMENT_FORMAT,
        _client_file("movereq.bmd", "cp1252"),
        _other_file("movereqtest.bmd", "gb2312"),
        _other_file("TKOUREQ.bmd", "gb2312"),
    ),
    _table(MONSTER_SKILL_FORMAT, _client_file("MonsterSkill.bmd"), _other_file("MonsterSkilltest.bmd")),
    _table(DIALOG_FORMAT, _client_file("Dialog.bmd", "cp1252"), _other_file("Dialogtest.bmd", "cp949")),
    _table(QUEST_FORMAT, _client_file("quest.bmd", "cp1252"), _other_file("questtest.bmd", "cp949")),
)


def all_formats() -> tuple[FormatSpec, ...]:
    return _FORMATS


def format_by_name(name: str) -> FormatSpec:
    for spec in _FORMATS:
        if spec.name == name:
            return spec
    known = ", ".join(spec.name for spec in _FORMATS)
    raise MubmdError(ErrorCode.UNKNOWN_FORMAT, f"No format {name!r}; known: {known}", suggestion="mubmd formats")


def identify(file_name: str | Path, data: bytes) -> Identified:
    path = Path(file_name)
    wanted = path.name.casefold()
    folders = tuple(part.casefold() for part in path.parts[-3:-1])
    for spec in _FORMATS:
        if spec.model_folder is not None and folders == ("data", spec.model_folder):
            continue
        for file in spec.files:
            if file.name.casefold() == wanted:
                return Identified(spec, file)
    raise MubmdError(
        ErrorCode.UNKNOWN_FORMAT,
        f"{file_name} is not a format this version reads",
        suggestion="mubmd formats",
    )
