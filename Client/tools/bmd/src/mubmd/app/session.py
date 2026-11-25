from dataclasses import dataclass
from pathlib import Path

from mubmd.app.documents import EditResult, OpenDocument, open_document, save_view, touches
from mubmd.core.documents.text_table import TEXT_RECORD_SIZE
from mubmd.core.errors import MubmdError
from mubmd.core.findings import Finding
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.patch import Change, PatchOp, apply_patch, diff_views
from mubmd.core.pointer import resolve
from mubmd.core.ports import FileStore, Summary


@dataclass(frozen=True, slots=True)
class Measure:
    size: int | None
    limit: int
    error: MubmdError | None
    warnings: tuple[Finding, ...] = ()


class EditSession:
    def __init__(self, store: FileStore, opened: OpenDocument) -> None:
        self._store = store
        self._opened = opened
        self._history: list[JsonObject] = [opened.view]
        self._position = 0

    @classmethod
    def open(cls, store: FileStore, path: Path) -> "EditSession":
        return cls(store, open_document(store, path))

    @property
    def path(self) -> Path:
        return self._opened.path

    @property
    def original(self) -> JsonObject:
        return self._opened.view

    @property
    def view(self) -> JsonObject:
        return self._history[self._position]

    @property
    def format_name(self) -> str:
        return self._opened.identified.format.name

    @property
    def encoding(self) -> str:
        return str(self.view.get("encoding"))

    @property
    def read_only(self) -> bool:
        return self._opened.read_only

    @property
    def summary(self) -> Summary:
        codec = self._opened.identified.format.codec
        return codec.summarize(codec.from_view(self.view))

    @property
    def changes(self) -> tuple[Change, ...]:
        return diff_views(self._opened.view, self.view)

    @property
    def dirty(self) -> bool:
        return bool(self.changes)

    @property
    def warnings(self) -> tuple[Finding, ...]:
        changes = self.changes
        return tuple(finding for finding in self._findings(self.view) if touches(finding, changes))

    def _findings(self, view: JsonObject) -> tuple[Finding, ...]:
        codec = self._opened.identified.format.codec
        return codec.check(codec.from_view(view), self._opened.document)

    def value(self, pointer: str) -> JsonValue:
        return resolve(self.view, pointer)

    def _edited(self, pointer: str, value: JsonValue) -> JsonObject:
        view = apply_patch(self.view, (PatchOp("replace", pointer, value),))
        self._opened.identified.format.codec.from_view(view)
        return view

    def measure(self, pointer: str, text: str) -> Measure:
        try:
            size: int | None = len(text.encode(self.encoding))
        except UnicodeEncodeError:
            size = None
        try:
            view = self._edited(pointer, text)
        except MubmdError as error:
            return Measure(size, TEXT_RECORD_SIZE, error)
        warnings = tuple(finding for finding in self._findings(view) if finding.pointer == pointer)
        return Measure(size, TEXT_RECORD_SIZE, None, warnings)

    def set(self, pointer: str, value: JsonValue) -> None:
        view = self._edited(pointer, value)
        del self._history[self._position + 1 :]
        self._history.append(view)
        self._position += 1

    def undo(self) -> bool:
        if self._position == 0:
            return False
        self._position -= 1
        return True

    def redo(self) -> bool:
        if self._position == len(self._history) - 1:
            return False
        self._position += 1
        return True

    def save(self) -> EditResult:
        result = save_view(self._store, self._opened, self.view, dry_run=False)
        self._opened = open_document(self._store, self.path)
        self._history = [self._opened.view]
        self._position = 0
        return result

    def reload(self) -> None:
        mine = tuple(PatchOp("replace", change.pointer, change.new) for change in self.changes)
        fresh = open_document(self._store, self.path)
        view = apply_patch(fresh.view, mine)
        fresh.identified.format.codec.from_view(view)
        self._opened = fresh
        self._history = [fresh.view] if view == fresh.view else [fresh.view, view]
        self._position = len(self._history) - 1
