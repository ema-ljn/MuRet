import json
from dataclasses import dataclass
from pathlib import Path

from mubmd.app.documents import load_view, open_document
from mubmd.app.paging import DEFAULT_LIMIT, page
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
    checked: int
    skipped: int


@dataclass(frozen=True, slots=True)
class DiffResult:
    changes: tuple[Change, ...]
    total: int
    next_offset: int | None


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


def _validate_file(store: FileStore, path: Path, encoding: str | None) -> tuple[Finding, ...] | None:
    try:
        opened = open_document(store, path, encoding=encoding)
    except MubmdError as error:
        if error.code is ErrorCode.UNKNOWN_FORMAT:
            return None
        if error.code is ErrorCode.CORRUPT_FILE:
            return (Finding(error.code, Severity.ERROR, "", error.message),)
        raise
    return opened.identified.format.codec.check(opened.document, None)


def validate(store: FileStore, path: Path, *, encoding: str | None = None) -> ValidateResult:
    if path.suffix.casefold() == ".bmd":
        opened = open_document(store, path, encoding=encoding)
        findings = opened.identified.format.codec.check(opened.document, None)
        return ValidateResult((FileFindings(path, findings),), checked=1, skipped=0)
    results = [(file, _validate_file(store, file, encoding)) for file in store.list_bmd(path)]
    known = [(file, findings) for file, findings in results if findings is not None]
    files = tuple(FileFindings(file, findings) for file, findings in known if findings)
    return ValidateResult(files, checked=len(known), skipped=len(results) - len(known))


def diff(
    store: FileStore,
    a: Path,
    b: Path,
    *,
    limit: int = DEFAULT_LIMIT,
    offset: int = 0,
    encoding: str | None = None,
) -> DiffResult:
    changes = diff_views(load_view(store, a, encoding=encoding), load_view(store, b, encoding=encoding))
    shown = page(list(changes), limit, offset)
    return DiffResult(shown.items, shown.total, shown.next_offset)


def dump(
    store: FileStore, path: Path, output: Path, *, dry_run: bool = False, encoding: str | None = None
) -> DumpResult:
    if output.suffix.casefold() == ".bmd":
        raise MubmdError(ErrorCode.USAGE, f"dump writes JSON and {output.name} is a .bmd: give --output a .json name")
    view = open_document(store, path, encoding=encoding).view
    data = (json.dumps(view, ensure_ascii=False, indent=2) + "\n").encode("utf-8")
    if not dry_run:
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
