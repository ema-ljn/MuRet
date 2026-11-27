import hashlib
from pathlib import Path

from mubmd.core.errors import ErrorCode, MubmdError


class FakeStore:
    def __init__(self, files: dict[Path, bytes], reference: Path | None = None) -> None:
        self.files = dict(files)
        self.reference = reference
        self.writes = 0

    def read(self, path: Path) -> bytes:
        if path not in self.files:
            raise MubmdError(ErrorCode.FILE_NOT_FOUND, f"{path} does not exist")
        return self.files[path]

    def head(self, path: Path, count: int) -> bytes:
        return self.read(path)[:count]

    def sha256(self, path: Path) -> str:
        return hashlib.sha256(self.read(path)).hexdigest()

    def exists(self, path: Path) -> bool:
        return any(file == path or path in file.parents for file in self.files)

    def is_reference(self, path: Path) -> bool:
        return self.reference is not None and (path == self.reference or self.reference in path.parents)

    def write(self, path: Path, data: bytes, *, expected_sha256: str | None) -> None:
        if self.is_reference(path):
            raise MubmdError(ErrorCode.REFERENCE_READ_ONLY, f"{path} is read-only")
        current = self.sha256(path) if path in self.files else None
        if current != expected_sha256:
            raise MubmdError(ErrorCode.STALE_FILE, f"{path} changed")
        self.files[path] = data
        self.writes += 1

    def copy(self, source: Path, destination: Path) -> None:
        if self.is_reference(destination):
            raise MubmdError(ErrorCode.REFERENCE_READ_ONLY, f"{destination} is read-only")
        self.files[destination] = self.read(source)

    def list_files(self, folder: Path) -> tuple[Path, ...]:
        if not self.exists(folder):
            raise MubmdError(ErrorCode.FILE_NOT_FOUND, f"{folder} does not exist")
        return tuple(sorted(file for file in self.files if folder in file.parents))

    def list_bmd(self, folder: Path) -> tuple[Path, ...]:
        return tuple(file for file in self.list_files(folder) if file.suffix.casefold() == ".bmd")

    def files_in(self, folder: Path) -> tuple[Path, ...]:
        return tuple(sorted(file for file in self.files if file.parent == folder))

    def folders_in(self, folder: Path) -> tuple[Path, ...]:
        inside = {parent for file in self.files for parent in file.parents if parent.parent == folder}
        return tuple(sorted(inside - {folder}))
