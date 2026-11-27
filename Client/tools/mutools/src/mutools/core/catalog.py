from dataclasses import dataclass
from pathlib import Path
from typing import Any, Final

from mutools.core.documents.model import MODEL_MAGIC
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.credit_format import CREDIT_FORMAT
from mutools.core.formats.dialog_format import DIALOG_FORMAT
from mutools.core.formats.filter_format import FILTER_FORMAT, FILTER_NAME_FORMAT
from mutools.core.formats.gate_format import GATE_FORMAT
from mutools.core.formats.item_format import ITEM_FORMAT
from mutools.core.formats.item_set_format import ITEM_SET_OPTION_FORMAT, ITEM_SET_TYPE_FORMAT
from mutools.core.formats.model_format import MODEL_ENCODING, ModelFormat
from mutools.core.formats.monster_skill_format import MONSTER_SKILL_FORMAT
from mutools.core.formats.move_requirement_format import MOVE_REQUIREMENT_FORMAT
from mutools.core.formats.quest_format import QUEST_FORMAT
from mutools.core.formats.record_table_format import RecordTableFormat
from mutools.core.formats.skill_format import SKILL_FORMAT
from mutools.core.formats.slide_format import SLIDE_FORMAT
from mutools.core.formats.text_format import TEXT_TABLE_SIZE, TextFormat
from mutools.core.ports import CatalogFile, FormatCodec


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


MODEL_FILE_SUFFIX: Final = ".bmd"
MODEL_SPEC: Final = FormatSpec(ModelFormat.name, None, (), ModelFormat())

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
    MODEL_SPEC,
)


def all_formats() -> tuple[FormatSpec, ...]:
    return _FORMATS


def format_by_name(name: str) -> FormatSpec:
    for spec in _FORMATS:
        if spec.name == name:
            return spec
    known = ", ".join(spec.name for spec in _FORMATS)
    raise MutoolsError(ErrorCode.UNKNOWN_FORMAT, f"No format {name!r}; known: {known}", suggestion="mutools formats")


def _table_named(path: Path, size: int) -> Identified | None:
    wanted = path.name.casefold()
    folder = path.parent.name.casefold()
    for spec in _FORMATS:
        if folder == spec.model_folder and (spec.size is None or size < spec.size):
            continue
        for file in spec.files:
            if file.name.casefold() == wanted:
                return Identified(spec, file)
    return None


def _model(path: Path) -> Identified:
    return Identified(MODEL_SPEC, CatalogFile(path.name, MODEL_ENCODING, read_by_client=None))


def _unknown(file_name: str | Path) -> MutoolsError:
    return MutoolsError(
        ErrorCode.UNKNOWN_FORMAT,
        f"{file_name} is not a format this version reads",
        suggestion="mutools formats",
    )


def identify(file_name: str | Path, data: bytes) -> Identified:
    path = Path(file_name)
    table = _table_named(path, len(data))
    if table is not None:
        return table
    if data.startswith(MODEL_MAGIC):
        return _model(path)
    raise _unknown(file_name)


def target_of(output: Path, spec: FormatSpec) -> Identified:
    table = _table_named(output, 0)
    if table is None:
        if spec is MODEL_SPEC and output.suffix.casefold() == MODEL_FILE_SUFFIX:
            return _model(output)
        raise _unknown(output)
    if table.format is not spec:
        raise MutoolsError(
            ErrorCode.INVALID_VALUE,
            f"The view is a {spec.name} view and {output.name} is a {table.format.name} file",
            pointer="/format",
        )
    return table
