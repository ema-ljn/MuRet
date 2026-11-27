from collections import OrderedDict
from pathlib import Path, PurePosixPath, PureWindowsPath
from threading import Lock
from typing import Final

from mutools.app.documents import format_name_of
from mutools.app.model_scene import borrows_skeleton, donor_of
from mutools.app.session import EditSession
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.model_view import MODEL_FORMAT_NAME
from mutools.core.ports import FileStore

KEPT_SESSIONS: Final = 4
UP: Final = ".."


def _stays_inside(file: str) -> bool:
    """Whether a name is safe to look up: on Windows, resolving `//host/share/x` already asks that host for it."""
    as_written = (PurePosixPath(file), PureWindowsPath(file))
    return bool(file) and not any(path.anchor or UP in path.parts for path in as_written)


class Models:
    def __init__(self, store: FileStore, root: Path) -> None:
        self.store = store
        self.root = root.resolve()
        self._sessions: OrderedDict[Path, tuple[str, EditSession]] = OrderedDict()
        self._opening = Lock()

    def _is_model(self, path: Path) -> bool:
        return format_name_of(self.store, path) == MODEL_FORMAT_NAME

    def listed(self) -> tuple[str, ...]:
        paths = self.store.list_bmd(self.root)
        return tuple(path.relative_to(self.root).as_posix() for path in paths if self._is_model(path))

    def _served(self, file: str) -> Path | None:
        if not _stays_inside(file):
            return None
        try:
            path = (self.root / file).resolve()
            return path if self.root in path.parents and path.is_file() else None
        except (OSError, ValueError):
            return None

    def path(self, file: str) -> Path:
        path = self._served(file)
        if path is None:
            raise MutoolsError(ErrorCode.FILE_NOT_FOUND, f"The served folder has no file {file!r}")
        return path

    def session(self, file: str) -> EditSession:
        path = self.path(file)
        if not self._is_model(path):
            raise MutoolsError(ErrorCode.UNKNOWN_FORMAT, f"{file} is not a model")
        with self._opening:
            sha256 = self.store.sha256(path)
            kept = self._sessions.get(path)
            if kept is None or kept[0] != sha256:
                kept = (sha256, EditSession.open(self.store, path))
            self._sessions[path] = kept
            self._sessions.move_to_end(path)
            while len(self._sessions) > KEPT_SESSIONS:
                self._sessions.popitem(last=False)
            return kept[1]

    def donor(self, file: str) -> str | None:
        found = donor_of(self.store, self.path(file))
        if found is None:
            return None
        donor = found.relative_to(self.root).as_posix()
        try:
            skeleton = self.session(donor).view
        except MutoolsError:
            return None
        return donor if borrows_skeleton(self.session(file).view, skeleton) else None
