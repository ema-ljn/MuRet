from dataclasses import dataclass
from pathlib import Path

from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.ports import FileStore


@dataclass(frozen=True, slots=True)
class WorkspaceResult:
    dest: Path
    files: int
    changed: bool
    edited: int = 0


def _manifest(store: FileStore, source: Path) -> dict[Path, str]:
    manifest = source.parent / f"{source.name}.sha256"
    if not store.exists(manifest):
        raise MubmdError(
            ErrorCode.FILE_NOT_FOUND,
            f"{source} has no {manifest.name} beside it to check the copy against",
            suggestion="mubmd workspace init <dest> --from <reference Data folder>",
        )
    entries: dict[Path, str] = {}
    for line in store.read(manifest).decode("utf-8").splitlines():
        digest, _, name = line.partition("  ")
        if name:
            entries[Path(name.removeprefix("./"))] = digest
    return entries


def _missing_and_edited(store: FileStore, dest: Path, manifest: dict[Path, str]) -> tuple[int, int]:
    missing = sum(1 for name in manifest if not store.exists(dest / name))
    present = ((name, digest) for name, digest in manifest.items() if store.exists(dest / name))
    return missing, sum(1 for name, digest in present if store.sha256(dest / name) != digest)


def workspace_init(store: FileStore, source: Path, dest: Path, *, yes: bool, dry_run: bool = False) -> WorkspaceResult:
    manifest = _manifest(store, source)
    if store.exists(dest) and store.list_files(dest):
        missing, edited = _missing_and_edited(store, dest, manifest)
        if not missing and not (edited and yes):
            return WorkspaceResult(dest, len(manifest), changed=False, edited=edited)
        if not yes:
            raise MubmdError(
                ErrorCode.NOT_EMPTY,
                f"{dest} holds files and is not a working copy of {source}: {missing} of the reference's "
                f"{len(manifest)} files are missing. Choose another folder; --yes copies the reference over this one",
            )
    files = store.list_files(source)
    if dry_run:
        return WorkspaceResult(dest, len(files), changed=True)
    for file in files:
        store.copy(file, dest / file.relative_to(source))
    if _missing_and_edited(store, dest, manifest) != (0, 0):
        raise MubmdError(
            ErrorCode.WRITE_VERIFY_FAILED, f"The copy in {dest} does not match {source.name}.sha256; copy it again"
        )
    return WorkspaceResult(dest, len(files), changed=True)
