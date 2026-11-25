import codecs
import dataclasses
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from mubmd.core.catalog import Identified, identify
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.patch import Change, diff_views
from mubmd.core.ports import FileStore


@dataclass(frozen=True, slots=True)
class OpenDocument:
    path: Path
    identified: Identified
    sha256: str
    size: int
    document: Any
    view: JsonObject
    read_only: bool


@dataclass(frozen=True, slots=True)
class EditResult:
    path: Path
    changed: bool
    dry_run: bool
    changes: tuple[Change, ...]
    warnings: tuple[Finding, ...]


def check_encoding(encoding: str) -> str:
    try:
        codecs.lookup(encoding)
    except LookupError as error:
        raise MubmdError(
            ErrorCode.INVALID_VALUE, f"Unknown encoding {encoding!r}", suggestion="--encoding cp1252"
        ) from error
    return encoding


def open_document(store: FileStore, path: Path, *, encoding: str | None = None) -> OpenDocument:
    data = store.read(path)
    identified = identify(path.name, data)
    if encoding is not None:
        file = dataclasses.replace(identified.file, encoding=check_encoding(encoding))
        identified = dataclasses.replace(identified, file=file)
    codec = identified.format.codec
    document = codec.decode(data, identified.file)
    return OpenDocument(
        path=path,
        identified=identified,
        sha256=hashlib.sha256(data).hexdigest(),
        size=len(data),
        document=document,
        view=codec.to_view(document),
        read_only=store.is_reference(path),
    )


def parse_json(data: bytes, source: str) -> JsonValue:
    try:
        value: JsonValue = json.loads(data)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise MubmdError(ErrorCode.INVALID_VALUE, f"{source} is not valid JSON: {error}") from error
    return value


def load_view(store: FileStore, path: Path, *, encoding: str | None = None) -> JsonObject:
    if path.suffix.casefold() == ".json":
        view = parse_json(store.read(path), str(path))
        if not isinstance(view, dict):
            raise MubmdError(ErrorCode.INVALID_VALUE, f"{path} does not hold a JSON object")
        return view
    return open_document(store, path, encoding=encoding).view


def touches(finding: Finding, changes: tuple[Change, ...]) -> bool:
    return any(
        finding.pointer == change.pointer
        or finding.pointer.startswith(f"{change.pointer}/")
        or change.pointer.startswith(f"{finding.pointer}/")
        for change in changes
    )


def encode_verified(opened: Identified, view: JsonObject) -> tuple[Any, bytes]:
    codec = opened.format.codec
    document = codec.from_view(view)
    data = codec.encode(document)
    if codec.decode(data, opened.file) != document:
        raise MubmdError(
            ErrorCode.WRITE_VERIFY_FAILED,
            "The encoded bytes do not decode to the edited document; nothing was written",
            suggestion="mubmd roundtrip <folder>",
        )
    return document, data


def refuse_reference(store: FileStore, path: Path) -> None:
    if store.is_reference(path):
        raise MubmdError(
            ErrorCode.REFERENCE_READ_ONLY,
            f"{path} is inside the reference folder, which is never written; edit a working copy",
            suggestion="mubmd workspace init ~/mu/Data",
        )


def save_view(store: FileStore, opened: OpenDocument, view: JsonObject, *, dry_run: bool) -> EditResult:
    changes = diff_views(opened.view, view)
    if not changes:
        return EditResult(opened.path, changed=False, dry_run=dry_run, changes=(), warnings=())
    refuse_reference(store, opened.path)
    document, data = encode_verified(opened.identified, view)
    findings = opened.identified.format.codec.check(document, opened.document)
    warnings = tuple(finding for finding in findings if touches(finding, changes))
    if not dry_run:
        store.write(opened.path, data, expected_sha256=opened.sha256)
    return EditResult(opened.path, changed=True, dry_run=dry_run, changes=changes, warnings=warnings)
