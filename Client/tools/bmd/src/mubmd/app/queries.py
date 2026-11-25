import re
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

from mubmd.app.documents import open_document
from mubmd.core.catalog import all_formats, format_by_name
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.json_types import JsonValue
from mubmd.core.pointer import format_pointer, resolve
from mubmd.core.ports import CatalogFile, FieldInfo, FileStore

DEFAULT_LIMIT = 50


@dataclass(frozen=True, slots=True)
class InfoResult:
    path: Path
    format: str
    encoding: str | None
    read_by_client: bool
    read_only: bool
    size: int
    cipher: str
    records: int
    used_records: int
    findings: int


@dataclass(frozen=True, slots=True)
class Match:
    index: int
    pointer: str
    text: str


@dataclass(frozen=True, slots=True)
class Page[T]:
    items: tuple[T, ...]
    total: int
    next_offset: int | None


@dataclass(frozen=True, slots=True)
class GetResult:
    pointer: str
    value: JsonValue
    truncated: bool


@dataclass(frozen=True, slots=True)
class FormatInfo:
    name: str
    size: int
    files: tuple[CatalogFile, ...]


@dataclass(frozen=True, slots=True)
class SchemaResult:
    format: str
    fields: tuple[FieldInfo, ...]


def info(store: FileStore, path: Path, *, encoding: str | None = None) -> InfoResult:
    opened = open_document(store, path, encoding=encoding)
    codec = opened.identified.format.codec
    summary = codec.summarize(opened.document)
    return InfoResult(
        path=path,
        format=opened.identified.format.name,
        encoding=opened.identified.file.encoding,
        read_by_client=opened.identified.file.read_by_client,
        read_only=opened.read_only,
        size=opened.size,
        cipher=summary.cipher,
        records=summary.records,
        used_records=summary.used_records,
        findings=len(codec.check(opened.document, None)),
    )


def _string_leaves(value: JsonValue, tokens: list[str | int]) -> Iterator[tuple[list[str | int], str]]:
    if isinstance(value, str):
        yield tokens, value
    elif isinstance(value, list):
        for index, item in enumerate(value):
            yield from _string_leaves(item, [*tokens, index])
    elif isinstance(value, dict):
        for key, item in value.items():
            yield from _string_leaves(item, [*tokens, key])


def _matcher(query: str, regex: bool) -> re.Pattern[str]:
    try:
        return re.compile(query if regex else re.escape(query), 0 if regex else re.IGNORECASE)
    except re.error as error:
        raise MubmdError(ErrorCode.INVALID_VALUE, f"Bad regular expression {query!r}: {error}") from error


def page[T](items: list[T], limit: int, offset: int) -> Page[T]:
    if limit < 1 or offset < 0:
        raise MubmdError(ErrorCode.USAGE, "--limit is at least 1 and --offset at least 0")
    end = offset + limit
    return Page(tuple(items[offset:end]), len(items), end if end < len(items) else None)


def find(
    store: FileStore,
    path: Path,
    query: str,
    *,
    regex: bool = False,
    limit: int = DEFAULT_LIMIT,
    offset: int = 0,
    encoding: str | None = None,
) -> Page[Match]:
    pattern = _matcher(query, regex)
    view = open_document(store, path, encoding=encoding).view
    matches: list[Match] = []
    for key, value in view.items():
        if not isinstance(value, list):
            continue
        for tokens, text in _string_leaves(value, [key]):
            if pattern.search(text):
                index = next(token for token in tokens if isinstance(token, int))
                matches.append(Match(index, format_pointer(tokens), text))
    return page(matches, limit, offset)


def _cut(value: JsonValue, limit: int) -> tuple[JsonValue, bool]:
    if isinstance(value, list):
        items = [_cut(item, limit) for item in value[:limit]]
        return [item for item, _ in items], len(value) > limit or any(cut for _, cut in items)
    if isinstance(value, dict):
        entries = {key: _cut(item, limit) for key, item in value.items()}
        return {key: item for key, (item, _) in entries.items()}, any(cut for _, cut in entries.values())
    return value, False


def get(
    store: FileStore, path: Path, pointer: str, *, limit: int = DEFAULT_LIMIT, encoding: str | None = None
) -> GetResult:
    view = open_document(store, path, encoding=encoding).view
    value, truncated = _cut(resolve(view, pointer), limit)
    return GetResult(pointer, value, truncated)


def list_formats() -> tuple[FormatInfo, ...]:
    return tuple(FormatInfo(spec.name, spec.size, spec.files) for spec in all_formats())


def format_schema(name: str) -> SchemaResult:
    spec = format_by_name(name)
    return SchemaResult(spec.name, spec.codec.describe_fields())
