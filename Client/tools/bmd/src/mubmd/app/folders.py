import json
from dataclasses import dataclass
from pathlib import Path

from mubmd.app.documents import load_view, open_document
from mubmd.core.catalog import identify
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding, Severity
from mubmd.core.patch import Change, diff_views
from mubmd.core.ports import FileStore


@dataclass(frozen=True, slots=True)
class FileFindings:
    path: Path
    findings: tuple[Finding, ...]


@dataclass(frozen=True, slots=True)
class ValidateResult:
    files: tuple[FileFindings, ...]


@dataclass(frozen=True, slots=True)
class DiffResult:
    changes: tuple[Change, ...]


@dataclass(frozen=True, slots=True)
class DumpResult:
    output: Path
    size: int


@dataclass(frozen=True, slots=True)
class RoundtripResult:
    checked: int
    identical: int
    differing: tuple[str, ...]
    skipped: int


def _validate_file(store: FileStore, path: Path, encoding: str | None) -> tuple[Finding, ...]:
    try:
        opened = open_document(store, path, encoding=encoding)
    except MubmdError as error:
        if error.code is ErrorCode.UNKNOWN_FORMAT:
            return (Finding(error.code, Severity.INFO, "", "Not a format this version reads"),)
        if error.code is ErrorCode.CORRUPT_FILE:
            return (Finding(error.code, Severity.ERROR, "", error.message),)
        raise
    return opened.identified.format.codec.check(opened.document, None)


def validate(store: FileStore, path: Path, *, encoding: str | None = None) -> ValidateResult:
    if path.suffix.casefold() == ".bmd":
        opened = open_document(store, path, encoding=encoding)
        return ValidateResult((FileFindings(path, opened.identified.format.codec.check(opened.document, None)),))
    files = store.list_bmd(path)
    return ValidateResult(tuple(FileFindings(file, _validate_file(store, file, encoding)) for file in files))


def diff(store: FileStore, a: Path, b: Path, *, encoding: str | None = None) -> DiffResult:
    return DiffResult(diff_views(load_view(store, a, encoding=encoding), load_view(store, b, encoding=encoding)))


def dump(store: FileStore, path: Path, output: Path, *, encoding: str | None = None) -> DumpResult:
    view = open_document(store, path, encoding=encoding).view
    data = (json.dumps(view, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    expected = store.sha256(output) if store.exists(output) else None
    store.write(output, data, expected_sha256=expected)
    return DumpResult(output, len(data))


def roundtrip(store: FileStore, folder: Path) -> RoundtripResult:
    checked = identical = skipped = 0
    differing: list[str] = []
    for path in store.list_bmd(folder):
        data = store.read(path)
        try:
            identified = identify(path.name, data)
        except MubmdError:
            skipped += 1
            continue
        checked += 1
        codec = identified.format.codec
        try:
            same = codec.encode(codec.decode(data, identified.file)) == data
        except MubmdError:
            same = False
        if same:
            identical += 1
        else:
            differing.append(path.relative_to(folder).as_posix())
    return RoundtripResult(checked, identical, tuple(differing), skipped)
