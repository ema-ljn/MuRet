from collections import OrderedDict
from pathlib import Path
from threading import Lock
from typing import Final

from mubmd.app.documents import format_name_of
from mubmd.app.model_scene import borrows_skeleton, donor_of
from mubmd.app.session import EditSession
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.formats.model_view import MODEL_FORMAT_NAME
from mubmd.core.ports import FileStore

KEPT_SESSIONS: Final = 4


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

    def path(self, file: str) -> Path:
        try:
            path = (self.root / file).resolve()
            inside = bool(file) and self.root in path.parents and path.is_file()
        except (OSError, ValueError):
            inside = False
        if not inside:
            raise MubmdError(ErrorCode.FILE_NOT_FOUND, f"The served folder has no file {file!r}")
        return path

    def session(self, file: str) -> EditSession:
        path = self.path(file)
        if not self._is_model(path):
            raise MubmdError(ErrorCode.UNKNOWN_FORMAT, f"{file} is not a model")
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
        return donor if borrows_skeleton(self.session(file).view, self.session(donor).view) else None
