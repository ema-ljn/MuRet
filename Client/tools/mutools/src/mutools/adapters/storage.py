import hashlib
import os
import shutil
from collections.abc import Iterator
from contextlib import contextmanager
from pathlib import Path
from typing import Final

from filelock import FileLock, Timeout

from mutools.core.errors import ErrorCode, MutoolsError

WORKSPACE_SUGGESTION: Final = "mutools workspace init ~/mu/Data"
LOCK_NAME: Final = ".mutools.lock"
LOCK_TIMEOUT_SECONDS: Final = 10.0
BACKUP_SUFFIX: Final = ".orig"


def write_lock(path: Path) -> FileLock:
    return FileLock(path.parent / LOCK_NAME, timeout=LOCK_TIMEOUT_SECONDS)


def reference_root(path: Path) -> Path | None:
    resolved = path.resolve()
    for folder in (resolved, *resolved.parents):
        if folder.name and (folder.parent / f"{folder.name}.sha256").is_file():
            return folder
    return None


def _not_found(path: Path) -> MutoolsError:
    return MutoolsError(ErrorCode.FILE_NOT_FOUND, f"{path} does not exist")


def _write_beside(path: Path, data: bytes) -> Path:
    temporary = path.with_name(f"{path.name}.{os.getpid()}.tmp")
    with temporary.open("xb") as stream:
        try:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        except BaseException:
            temporary.unlink(missing_ok=True)
            raise
    return temporary


@contextmanager
def _locked(path: Path) -> Iterator[None]:
    try:
        with write_lock(path):
            yield
    except Timeout as error:
        raise MutoolsError(
            ErrorCode.IO_ERROR,
            f"Another mutools has been writing in {path.parent} for {LOCK_TIMEOUT_SECONDS:g} s; nothing was written",
        ) from error


def _back_up_once(path: Path) -> None:
    backup = path.with_name(f"{path.name}{BACKUP_SUFFIX}")
    if not path.exists() or backup.exists():
        return
    partial = backup.with_name(f"{backup.name}.{os.getpid()}.tmp")
    try:
        shutil.copy2(path, partial)
        os.replace(partial, backup)
    finally:
        partial.unlink(missing_ok=True)


def _listed(folder: Path) -> list[Path]:
    try:
        return sorted(folder.iterdir())
    except OSError:
        return []


class LocalFileStore:
    def read(self, path: Path) -> bytes:
        if not path.is_file():
            raise _not_found(path)
        return path.read_bytes()

    def head(self, path: Path, count: int) -> bytes:
        if not path.is_file():
            raise _not_found(path)
        with path.open("rb") as stream:
            return stream.read(count)

    def sha256(self, path: Path) -> str:
        return hashlib.sha256(self.read(path)).hexdigest()

    def exists(self, path: Path) -> bool:
        return path.exists()

    def is_reference(self, path: Path) -> bool:
        return reference_root(path) is not None

    def _check_writable(self, path: Path) -> None:
        root = reference_root(path)
        if root is not None:
            raise MutoolsError(
                ErrorCode.REFERENCE_READ_ONLY,
                f"{path} is inside the reference folder {root}, which is never written; edit a working copy",
                suggestion=WORKSPACE_SUGGESTION,
            )
        if not path.parent.is_dir():
            raise _not_found(path.parent)

    def _check_not_stale(self, path: Path, expected_sha256: str | None) -> None:
        current = self.sha256(path) if path.exists() else None
        if current != expected_sha256:
            reason = "already exists" if expected_sha256 is None else "changed on disk since it was read"
            raise MutoolsError(
                ErrorCode.STALE_FILE,
                f"{path} {reason}; nothing was written",
                suggestion=f"mutools info {path}",
            )

    def write(self, path: Path, data: bytes, *, expected_sha256: str | None) -> None:
        self._check_writable(path)
        self._check_not_stale(path, expected_sha256)
        temporary = _write_beside(path, data)
        try:
            with _locked(path):
                self._check_not_stale(path, expected_sha256)
                _back_up_once(path)
                os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)

    def copy(self, source: Path, destination: Path) -> None:
        if reference_root(destination) is not None:
            raise MutoolsError(
                ErrorCode.REFERENCE_READ_ONLY,
                f"{destination} is inside a reference folder, which is never written",
                suggestion=WORKSPACE_SUGGESTION,
            )
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, destination)

    def _walk(self, folder: Path) -> list[Path]:
        if not folder.is_dir():
            raise _not_found(folder)
        return sorted(path for path in folder.rglob("*") if path.is_file())

    def list_bmd(self, folder: Path) -> tuple[Path, ...]:
        return tuple(path for path in self._walk(folder) if path.suffix.casefold() == ".bmd")

    def list_files(self, folder: Path) -> tuple[Path, ...]:
        return tuple(self._walk(folder))

    def files_in(self, folder: Path) -> tuple[Path, ...]:
        return tuple(path for path in _listed(folder) if path.is_file())

    def folders_in(self, folder: Path) -> tuple[Path, ...]:
        return tuple(path for path in _listed(folder) if path.is_dir())
