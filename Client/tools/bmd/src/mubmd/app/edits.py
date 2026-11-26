import dataclasses
from collections.abc import Sequence
from pathlib import Path

from mubmd.app.documents import (
    EditResult,
    check_encoding,
    encode_verified,
    open_document,
    parse_json,
    parse_value,
    refuse_reference,
    save_view,
    touches,
)
from mubmd.core.catalog import format_by_name, identify
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.patch import Change, PatchOp, apply_patch, diff_views
from mubmd.core.pointer import resolve
from mubmd.core.ports import FileStore


def set_value(
    store: FileStore, path: Path, pointer: str, raw: str, *, dry_run: bool, encoding: str | None = None
) -> EditResult:
    opened = open_document(store, path, encoding=encoding)
    value = parse_value(raw, resolve(opened.view, pointer), pointer)
    view = apply_patch(opened.view, (PatchOp("replace", pointer, value),))
    return save_view(store, opened, view, dry_run=dry_run)


def apply_operations(
    store: FileStore, path: Path, ops: Sequence[PatchOp], *, dry_run: bool, encoding: str | None = None
) -> EditResult:
    opened = open_document(store, path, encoding=encoding)
    return save_view(store, opened, apply_patch(opened.view, ops), dry_run=dry_run)


def build(store: FileStore, source: Path, output: Path, *, dry_run: bool) -> EditResult:
    view = parse_json(store.read(source), str(source))
    if not isinstance(view, dict):
        raise MubmdError(ErrorCode.INVALID_VALUE, f"{source} does not hold a JSON object")
    format_name = view.get("format")
    spec = format_by_name(format_name if isinstance(format_name, str) else "")
    target = identify(output.absolute(), b"")
    if target.format.name != spec.name:
        raise MubmdError(
            ErrorCode.INVALID_VALUE,
            f"{source} is a {spec.name} view and {output.name} is a {target.format.name} file",
            pointer="/format",
        )
    encoding = view.get("encoding")
    if isinstance(encoding, str):
        target = dataclasses.replace(target, file=dataclasses.replace(target.file, encoding=check_encoding(encoding)))
    refuse_reference(store, output)
    document, data, written = encode_verified(target, view)
    exists = store.exists(output)
    expected = store.sha256(output) if exists else None
    changes: tuple[Change, ...] = ()
    original = None
    if exists:
        current = store.read(output)
        if current == data:
            return EditResult(output, changed=False, dry_run=dry_run, changes=(), warnings=())
        original = spec.codec.decode(current, target.file)
        changes = diff_views(spec.codec.to_view(original), written)
    findings = spec.codec.check(document, original)
    warnings = tuple(finding for finding in findings if not exists or touches(finding, changes))
    if not dry_run:
        store.write(output, data, expected_sha256=expected)
    return EditResult(output, changed=True, dry_run=dry_run, changes=changes, warnings=warnings)
