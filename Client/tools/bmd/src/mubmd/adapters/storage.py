import hashlib
import os
import shutil
from pathlib import Path
from typing import Final

from mubmd.core.errors import ErrorCode, MubmdError

WORKSPACE_SUGGESTION: Final = "mubmd workspace init ~/mu/Data"


def reference_root(path: Path) -> Path | None:
    resolved = path.resolve()
    for folder in (resolved, *resolved.parents):
        if folder.name and (folder.parent / f"{folder.name}.sha256").is_file():
            return folder
    return None


def _not_found(path: Path) -> MubmdError:
    return MubmdError(ErrorCode.FILE_NOT_FOUND, f"{path} does not exist")


class LocalFileStore:
    def read(self, path: Path) -> bytes:
        if not path.is_file():
            raise _not_found(path)
        return path.read_bytes()

    def sha256(self, path: Path) -> str:
        return hashlib.sha256(self.read(path)).hexdigest()

    def exists(self, path: Path) -> bool:
        return path.exists()

    def is_reference(self, path: Path) -> bool:
        return reference_root(path) is not None

    def _check_writable(self, path: Path) -> None:
        root = reference_root(path)
        if root is not None:
            raise MubmdError(
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
            raise MubmdError(
                ErrorCode.STALE_FILE,
                f"{path} {reason}; nothing was written",
                suggestion=f"mubmd info {path}",
            )

    def write(self, path: Path, data: bytes, *, expected_sha256: str | None) -> None:
        self._check_writable(path)
        self._check_not_stale(path, expected_sha256)
        temporary = path.with_name(f"{path.name}.{os.getpid()}.tmp")
        try:
            with temporary.open("wb") as stream:
                stream.write(data)
                stream.flush()
                os.fsync(stream.fileno())
            backup = path.with_name(f"{path.name}.bak")
            if path.exists() and not backup.exists():
                shutil.copy2(path, backup)
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)

    def copy(self, source: Path, destination: Path) -> None:
        if reference_root(destination) is not None:
            raise MubmdError(
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
