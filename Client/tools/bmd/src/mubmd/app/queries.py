import re
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from mubmd.app.checks import findings_of
from mubmd.app.documents import decode_document, open_document
from mubmd.app.paging import DEFAULT_LIMIT, Page, page
from mubmd.core.catalog import FormatSpec, all_formats, format_by_name
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.leaves import Tokens, is_leaf, leaf_text, leaves, outline
from mubmd.core.pointer import format_pointer, parse_pointer, resolve
from mubmd.core.ports import CatalogFile, FieldInfo, FileStore

MATCHES_BY_NAME: Final = "name"
MATCHES_BY_CONTENT: Final = "content"


@dataclass(frozen=True, slots=True)
class InfoResult:
    path: Path
    format: str
    encoding: str | None
    read_by_client: bool | None
    read_only: bool
    size: int
    cipher: str
    records: int | None
    used_records: int | None
    version: int | None
    meshes: int | None
    bones: int | None
    actions: int | None
    findings: int


@dataclass(frozen=True, slots=True)
class Match:
    index: int
    pointer: str
    text: str


@dataclass(frozen=True, slots=True)
class GetResult:
    pointer: str
    value: JsonValue
    truncated: bool
    total: int | None
    next_offset: int | None


@dataclass(frozen=True, slots=True)
class FormatInfo:
    name: str
    size: int | None
    files: tuple[CatalogFile, ...]
    matches: str


@dataclass(frozen=True, slots=True)
class SchemaResult:
    format: str
    fields: tuple[FieldInfo, ...]


def info(store: FileStore, path: Path, *, encoding: str | None = None) -> InfoResult:
    decoded = decode_document(store, path, encoding=encoding)
    summary = decoded.identified.format.codec.summarize(decoded.document)
    return InfoResult(
        path=path,
        format=decoded.identified.format.name,
        encoding=decoded.identified.file.encoding,
        read_by_client=decoded.identified.file.read_by_client,
        read_only=decoded.read_only,
        size=decoded.size,
        cipher=summary.cipher,
        records=summary.records,
        used_records=summary.used_records,
        version=summary.version,
        meshes=summary.meshes,
        bones=summary.bones,
        actions=summary.actions,
        findings=len(findings_of(store, path, decoded.identified, decoded.document, None)),
    )


def _field_name(tokens: Sequence[str | int]) -> str | None:
    return next((token for token in reversed(tokens) if isinstance(token, str)), None)


def _texts(view: JsonObject, field: str | None) -> Iterator[tuple[Tokens, str]]:
    for key, value in view.items():
        if not isinstance(value, list):
            continue
        for tokens, leaf in leaves(value, (key,)):
            text = leaf_text(leaf)
            if text is not None and field in (None, _field_name(tokens)):
                yield tokens, text


def _described_field(info: FieldInfo) -> str:
    return next(token for token in reversed(parse_pointer(info.pointer)) if not token.startswith("<"))


def _check_field(spec: FormatSpec, field: str) -> None:
    if field not in map(_described_field, spec.codec.describe_fields()):
        raise MubmdError(
            ErrorCode.INVALID_VALUE,
            f"A {spec.name} table has no field {field!r}",
            suggestion=f"mubmd schema {spec.name}",
        )


def _matcher(query: str, regex: bool) -> re.Pattern[str]:
    try:
        return re.compile(query if regex else re.escape(query), 0 if regex else re.IGNORECASE)
    except re.error as error:
        raise MubmdError(ErrorCode.INVALID_VALUE, f"Bad regular expression {query!r}: {error}") from error


def find(
    store: FileStore,
    path: Path,
    query: str,
    *,
    regex: bool = False,
    field: str | None = None,
    limit: int = DEFAULT_LIMIT,
    offset: int = 0,
    encoding: str | None = None,
) -> Page[Match]:
    pattern = _matcher(query, regex)
    opened = open_document(store, path, encoding=encoding)
    if field is not None:
        _check_field(opened.identified.format, field)
    matches: list[Match] = []
    for tokens, text in _texts(opened.view, field):
        if pattern.search(text):
            index = next(token for token in tokens if isinstance(token, int))
            matches.append(Match(index, format_pointer(tokens), text))
    return page(matches, limit, offset)


def _levels(value: JsonValue, limit: int) -> tuple[JsonValue, bool]:
    if isinstance(value, list):
        items = [outline(item) for item in value[:limit]]
        return [item for item, _ in items], len(value) > limit or any(cut for _, cut in items)
    if isinstance(value, dict) and not is_leaf(value):
        members = {key: _levels(item, limit) for key, item in value.items()}
        return {key: item for key, (item, _) in members.items()}, any(cut for _, cut in members.values())
    return value, False


def get(
    store: FileStore,
    path: Path,
    pointer: str,
    *,
    limit: int = DEFAULT_LIMIT,
    offset: int = 0,
    encoding: str | None = None,
) -> GetResult:
    view = open_document(store, path, encoding=encoding).view
    found = resolve(view, pointer)
    if not isinstance(found, list):
        if offset:
            raise MubmdError(
                ErrorCode.USAGE, f"--offset skips items of a list, and {pointer} is not one", pointer=pointer
            )
        value, truncated = _levels(found, limit)
        return GetResult(pointer, value, truncated, total=None, next_offset=None)
    shown = page(found, limit, offset)
    value, truncated = _levels(list(shown.items), limit)
    whole = offset == 0 and shown.next_offset is None
    return GetResult(pointer, value, truncated or not whole, shown.total, shown.next_offset)


def list_formats() -> tuple[FormatInfo, ...]:
    return tuple(
        FormatInfo(spec.name, spec.size, spec.files, MATCHES_BY_NAME if spec.files else MATCHES_BY_CONTENT)
        for spec in all_formats()
    )


def format_schema(name: str) -> SchemaResult:
    spec = format_by_name(name)
    return SchemaResult(spec.name, spec.codec.describe_fields())
