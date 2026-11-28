import hashlib
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from mutools.app.documents import check_encoding, decode_document, refuse_reference
from mutools.core.checks.emote_checks import OriginalTrigger, original_triggers, taken_triggers
from mutools.core.documents.model import Model
from mutools.core.documents.text_table import TextTable
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.findings import Finding
from mutools.core.formats.emote_table import (
    EMOTE_TABLE_FOLDER,
    EMOTE_TABLE_NAME,
    AddedEmote,
    checked_emote,
    emote_table_bytes,
    read_emote_table,
)
from mutools.core.ports import FileStore

PLAYER_FOLDER: Final = "player"
PLAYER_MODEL: Final = "Player.bmd"
TEXTS_FOLDER: Final = "Local"
TEXTS_NAME: Final = "Text.bmd"


@dataclass(frozen=True, slots=True)
class EmoteTable:
    path: Path
    exists: bool
    emotes: tuple[AddedEmote, ...]


@dataclass(frozen=True, slots=True)
class Warned[T]:
    result: T
    warnings: tuple[Finding, ...]


@dataclass(frozen=True, slots=True)
class EmoteEdit:
    path: Path
    action: int
    changed: bool
    dry_run: bool
    emotes: int


def _named(store: FileStore, folder: Path, name: str, *, folder_wanted: bool) -> Path | None:
    if not store.exists(folder):
        return None
    listed = store.folders_in(folder) if folder_wanted else store.files_in(folder)
    return next((path for path in listed if path.name.casefold() == name.casefold()), None)


def _data_folder(store: FileStore, data: Path) -> None:
    if not store.exists(data):
        raise MutoolsError(ErrorCode.FILE_NOT_FOUND, f"{data} does not exist: give the game's Data folder")


def table_path(store: FileStore, data: Path) -> Path:
    """Data/MuRet/Emotes.txt, found ignoring case as the client finds it, or where it goes."""
    folder = _named(store, data, EMOTE_TABLE_FOLDER, folder_wanted=True) or data / EMOTE_TABLE_FOLDER
    return _named(store, folder, EMOTE_TABLE_NAME, folder_wanted=False) or folder / EMOTE_TABLE_NAME


@dataclass(frozen=True, slots=True)
class _OpenTable:
    table: EmoteTable
    sha256: str | None


def _open_table(store: FileStore, data: Path, encoding: str) -> _OpenTable:
    check_encoding(encoding)
    _data_folder(store, data)
    path = table_path(store, data)
    if not store.exists(path):
        return _OpenTable(EmoteTable(path, exists=False, emotes=()), sha256=None)
    read = store.read(path)
    table = EmoteTable(path, exists=True, emotes=read_emote_table(read, encoding))
    return _OpenTable(table, hashlib.sha256(read).hexdigest())


def read_table(store: FileStore, data: Path, encoding: str) -> EmoteTable:
    return _open_table(store, data, encoding).table


def _original_triggers(store: FileStore, data: Path) -> tuple[OriginalTrigger, ...]:
    folder = _named(store, data, TEXTS_FOLDER, folder_wanted=True)
    texts = _named(store, folder, TEXTS_NAME, folder_wanted=False) if folder else None
    if texts is None:
        return original_triggers(None)
    document = decode_document(store, texts).document
    assert isinstance(document, TextTable)
    return original_triggers(document.records)


def list_emotes(store: FileStore, data: Path, encoding: str) -> Warned[EmoteTable]:
    table = read_table(store, data, encoding)
    originals = _original_triggers(store, data)
    warnings = tuple(
        finding
        for index, emote in enumerate(table.emotes)
        for finding in taken_triggers(emote, originals, encoding, f"/emotes/{index}")
    )
    return Warned(table, warnings)


def _player_actions(store: FileStore, data: Path) -> int:
    folder = _named(store, data, PLAYER_FOLDER, folder_wanted=True)
    player = _named(store, folder, PLAYER_MODEL, folder_wanted=False) if folder else None
    if player is None:
        raise MutoolsError(
            ErrorCode.FILE_NOT_FOUND,
            f"{data} has no {PLAYER_FOLDER}/{PLAYER_MODEL}, whose actions an emote plays",
        )
    model = decode_document(store, player).document
    if not isinstance(model, Model):
        raise MutoolsError(ErrorCode.CORRUPT_FILE, f"{player} is not a model")
    return len(model.actions)


def _played_by_player(store: FileStore, data: Path, emote: AddedEmote) -> None:
    actions = _player_actions(store, data)
    for pointer, action in (("/action", emote.action), ("/elf_action", emote.elf_action)):
        if action >= actions:
            raise MutoolsError(
                ErrorCode.INVALID_VALUE,
                f"Player.bmd has {actions} actions, 0 to {actions - 1}, and not action {action}: add it first",
                suggestion="mutools action import <file>/player/Player.bmd <action file>",
                pointer=pointer,
            )


def _save(
    store: FileStore, opened: _OpenTable, emotes: tuple[AddedEmote, ...], *, encoding: str, dry_run: bool
) -> None:
    path = opened.table.path
    refuse_reference(store, path)
    data = emote_table_bytes(emotes, encoding)
    if dry_run:
        return
    if not opened.table.exists:
        store.make_folder(path.parent)
    store.write(path, data, expected_sha256=opened.sha256)


def add_emote(
    store: FileStore, data: Path, emote: AddedEmote, *, replace_row: bool, encoding: str, dry_run: bool
) -> Warned[EmoteEdit]:
    checked_emote(emote)
    opened = _open_table(store, data, encoding)
    table = opened.table
    _played_by_player(store, data, emote)
    current = next((row for row in table.emotes if row.action == emote.action), None)
    if current == emote:
        unchanged = EmoteEdit(table.path, emote.action, changed=False, dry_run=dry_run, emotes=len(table.emotes))
        return Warned(unchanged, taken_triggers(emote, _original_triggers(store, data), encoding))
    if current is not None and not replace_row:
        raise MutoolsError(
            ErrorCode.USAGE,
            f"Action {emote.action} already has an emote, started by {', '.join(current.triggers)}: add --replace to "
            "change it",
            suggestion="--replace",
        )
    rows = tuple(emote if row.action == emote.action else row for row in table.emotes)
    emotes = rows if current is not None else (*rows, emote)
    _save(store, opened, emotes, encoding=encoding, dry_run=dry_run)
    edit = EmoteEdit(table.path, emote.action, changed=True, dry_run=dry_run, emotes=len(emotes))
    return Warned(edit, taken_triggers(emote, _original_triggers(store, data), encoding))


def remove_emote(store: FileStore, data: Path, action: int, *, encoding: str, dry_run: bool) -> EmoteEdit:
    opened = _open_table(store, data, encoding)
    table = opened.table
    emotes = tuple(row for row in table.emotes if row.action != action)
    if len(emotes) == len(table.emotes):
        actions = ", ".join(str(row.action) for row in table.emotes) or "none"
        raise MutoolsError(
            ErrorCode.POINTER_NOT_FOUND,
            f"Action {action} has no emote in {table.path}; the emotes are {actions}",
            suggestion="mutools emote list <file>",
        )
    _save(store, opened, emotes, encoding=encoding, dry_run=dry_run)
    return EmoteEdit(table.path, action, changed=True, dry_run=dry_run, emotes=len(emotes))
