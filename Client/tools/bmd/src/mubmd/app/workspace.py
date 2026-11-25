from dataclasses import dataclass
from pathlib import Path

from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.ports import FileStore


@dataclass(frozen=True, slots=True)
class WorkspaceResult:
    dest: Path
    files: int
    changed: bool


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


def _matches(store: FileStore, dest: Path, manifest: dict[Path, str]) -> bool:
    return all(store.exists(dest / name) and store.sha256(dest / name) == digest for name, digest in manifest.items())


def workspace_init(store: FileStore, source: Path, dest: Path, *, yes: bool) -> WorkspaceResult:
    manifest = _manifest(store, source)
    if store.exists(dest):
        if _matches(store, dest, manifest):
            return WorkspaceResult(dest, len(manifest), changed=False)
        if store.list_files(dest) and not yes:
            raise MubmdError(
                ErrorCode.NOT_EMPTY,
                f"{dest} is not empty and does not match {source}",
                suggestion=f"mubmd workspace init {dest} --yes",
            )
    files = store.list_files(source)
    for file in files:
        store.copy(file, dest / file.relative_to(source))
    if not _matches(store, dest, manifest):
        raise MubmdError(
            ErrorCode.WRITE_VERIFY_FAILED,
            f"The copy in {dest} does not match {source.name}.sha256",
            suggestion=f"mubmd workspace init {dest} --yes",
        )
    return WorkspaceResult(dest, len(files), changed=True)
