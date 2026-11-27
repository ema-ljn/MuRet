import dataclasses
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Final

from mubmd.app.checks import findings_of
from mubmd.core.catalog import Identified, identify
from mubmd.core.documents.model import MODEL_MAGIC
from mubmd.core.documents.text_table import is_text_encoding
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.findings import Finding
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.leaves import leaf_text
from mubmd.core.patch import Change, diff_views
from mubmd.core.ports import FileStore

HEADER_POINTERS: Final = frozenset(
    {"/format_version", "/format", "/encoding", "/records_sha256", "/stored_checksum", "/version"}
)
HEADER_SUGGESTIONS: Final = {
    "/encoding": "--encoding reads a file in another encoding",
    "/version": "mubmd dump <file> --output model.json, change version there, then mubmd build",
}


@dataclass(frozen=True, slots=True)
class DecodedDocument:
    path: Path
    identified: Identified
    sha256: str
    size: int
    document: Any
    read_only: bool


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
    if not is_text_encoding(encoding):
        raise MubmdError(
            ErrorCode.INVALID_VALUE, f"{encoding!r} is not a text encoding", suggestion="--encoding cp1252"
        )
    return encoding


def decode_document(store: FileStore, path: Path, *, encoding: str | None = None) -> DecodedDocument:
    data = store.read(path)
    identified = identify(path.absolute(), data)
    if encoding is not None:
        file = dataclasses.replace(identified.file, encoding=check_encoding(encoding))
        identified = dataclasses.replace(identified, file=file)
    return DecodedDocument(
        path=path,
        identified=identified,
        sha256=hashlib.sha256(data).hexdigest(),
        size=len(data),
        document=identified.format.codec.decode(data, identified.file),
        read_only=store.is_reference(path),
    )


def format_name_of(store: FileStore, path: Path) -> str | None:
    try:
        return identify(path.absolute(), store.head(path, len(MODEL_MAGIC))).format.name
    except MubmdError:
        return None


def open_document(store: FileStore, path: Path, *, encoding: str | None = None) -> OpenDocument:
    decoded = decode_document(store, path, encoding=encoding)
    return OpenDocument(
        path=decoded.path,
        identified=decoded.identified,
        sha256=decoded.sha256,
        size=decoded.size,
        document=decoded.document,
        view=decoded.identified.format.codec.to_view(decoded.document),
        read_only=decoded.read_only,
    )


def parse_json(data: bytes, source: str) -> JsonValue:
    try:
        value: JsonValue = json.loads(data)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise MubmdError(ErrorCode.INVALID_VALUE, f"{source} is not valid JSON: {error}") from error
    return value


def parse_value(raw: str, current: JsonValue, pointer: str) -> JsonValue:
    if leaf_text(current) is not None:
        return raw
    try:
        value: JsonValue = json.loads(raw)
    except json.JSONDecodeError as error:
        raise MubmdError(
            ErrorCode.INVALID_VALUE,
            f"The value at {pointer} is not a text, so the new value is JSON; got {raw!r}",
            pointer=pointer,
        ) from error
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


def encode_verified(opened: Identified, view: JsonObject) -> tuple[Any, bytes, JsonObject]:
    codec = opened.format.codec
    document = codec.from_view(view)
    data = codec.encode(document)
    decoded = codec.decode(data, opened.file)
    if decoded != document:
        raise MubmdError(
            ErrorCode.WRITE_VERIFY_FAILED, "The encoded bytes do not decode to the edited document; nothing was written"
        )
    return document, data, codec.to_view(decoded)


def refuse_header_changes(changes: tuple[Change, ...]) -> None:
    for change in changes:
        if change.pointer in HEADER_POINTERS:
            raise MubmdError(
                ErrorCode.INVALID_VALUE,
                f"{change.pointer} describes the file and cannot be edited",
                suggestion=HEADER_SUGGESTIONS.get(change.pointer),
                pointer=change.pointer,
            )


def refuse_reference(store: FileStore, path: Path) -> None:
    if store.is_reference(path):
        raise MubmdError(
            ErrorCode.REFERENCE_READ_ONLY,
            f"{path} is inside the reference folder, which is never written; edit a working copy",
            suggestion="mubmd workspace init ~/mu/Data",
        )


def save_view(store: FileStore, opened: OpenDocument, view: JsonObject, *, dry_run: bool) -> EditResult:
    unchanged = EditResult(opened.path, changed=False, dry_run=dry_run, changes=(), warnings=())
    refuse_header_changes(diff_views(opened.view, view))
    if view == opened.view:
        opened.identified.format.codec.from_view(view)
        return unchanged
    refuse_reference(store, opened.path)
    document, data, written = encode_verified(opened.identified, view)
    changes = tuple(change for change in diff_views(opened.view, written) if change.pointer not in HEADER_POINTERS)
    if not changes:
        return unchanged
    findings = findings_of(store, opened.path, opened.identified, document, opened.document)
    warnings = tuple(finding for finding in findings if touches(finding, changes))
    if not dry_run:
        store.write(opened.path, data, expected_sha256=opened.sha256)
    return EditResult(opened.path, changed=True, dry_run=dry_run, changes=changes, warnings=warnings)
