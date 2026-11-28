from dataclasses import dataclass
from pathlib import Path

from mutools.app.documents import EditResult, OpenDocument, open_document, parse_json, save_view
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.action_file import action_file, read_action_file, with_action, without_action
from mutools.core.formats.model_view import MODEL_FORMAT_NAME
from mutools.core.json_text import view_text
from mutools.core.json_types import JsonObject
from mutools.core.ports import FileStore


@dataclass(frozen=True, slots=True)
class ActionExport:
    output: Path
    action: int
    keys: int
    size: int


@dataclass(frozen=True, slots=True)
class ActionEdit:
    action: int
    actions: int
    result: EditResult


def open_model(store: FileStore, path: Path) -> OpenDocument:
    opened = open_document(store, path)
    format_name = opened.identified.format.name
    if format_name != MODEL_FORMAT_NAME:
        raise MutoolsError(
            ErrorCode.USAGE,
            f"{path.name} is a {format_name} table, and only models have actions",
            suggestion="mutools info <file>",
        )
    return opened


def _count(view: JsonObject) -> int:
    actions = view["actions"]
    assert isinstance(actions, list)
    return len(actions)


def export_action(store: FileStore, path: Path, index: int, output: Path, *, dry_run: bool) -> ActionExport:
    if output.suffix.casefold() == ".bmd":
        raise MutoolsError(
            ErrorCode.USAGE, f"An action file is JSON and {output.name} is a .bmd: give --output a .json name"
        )
    exported = action_file(open_model(store, path).view, index)
    data = view_text(exported).encode("utf-8")
    if not dry_run:
        expected = store.sha256(output) if store.exists(output) else None
        store.write(output, data, expected_sha256=expected)
    keys = exported["keys"]
    assert isinstance(keys, int)
    return ActionExport(output, index, keys, len(data))


def import_action(store: FileStore, path: Path, source: Path, *, at: int | None = None, dry_run: bool) -> ActionEdit:
    action = read_action_file(parse_json(store.read(source), str(source)))
    opened = open_model(store, path)
    view = with_action(opened.view, action, at)
    result = save_view(store, opened, view, dry_run=dry_run)
    return ActionEdit(_count(opened.view) if at is None else at, _count(view), result)


def remove_action(store: FileStore, path: Path, index: int, *, yes: bool, dry_run: bool) -> ActionEdit:
    opened = open_model(store, path)
    view = without_action(opened.view, index)
    last = _count(opened.view) - 1
    if index != last and not yes:
        raise MutoolsError(
            ErrorCode.USAGE,
            f"Removing action {index} moves actions {index + 1} to {last} down one number, and the client and the "
            "server name a character's actions by their number: add --yes to remove it",
            suggestion="--yes",
        )
    return ActionEdit(index, _count(view), save_view(store, opened, view, dry_run=dry_run))
