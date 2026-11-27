import re
from collections.abc import Callable, Sequence
from functools import partial
from pathlib import PurePosixPath
from typing import Any, Final

from nicegui import ui

from mutools.adapters.web.elements import Thumbnail
from mutools.adapters.web.models import LibraryEntry, Models, folder_of
from mutools.adapters.web.widgets import Press, icon, words
from mutools.app.model_parts import counted
from mutools.core.errors import MutoolsError

WORLD_FOLDER: Final = re.compile(r"object\d+", re.IGNORECASE)
PREFERRED_FOLDER: Final = "Monster"
TOP_FOLDER: Final = "."
MOST_FOUND: Final = 60
LIBRARY_MARK: Final = "library"
SEARCH_MARK: Final = "find-model"
UNREADABLE: Final = "Cannot be read"


def _folder_label(folder: str) -> str:
    return folder or TOP_FOLDER


def _facts(entry: LibraryEntry) -> str:
    return f"{counted(entry.meshes, 'mesh', 'meshes')}, {counted(entry.actions, 'action')}"


class Library:
    def __init__(self, models: Models, listed: Sequence[str], chosen: str, on_pick: Callable[[str], Any]) -> None:
        self._models = models
        self._listed = tuple(listed)
        self._folders = models.folders(self._listed)
        self._on_pick = on_pick
        self._chosen = chosen
        self._plates: dict[str, Press] = {}
        self._query = ""
        known = {folder for folder, _ in self._folders}
        if chosen in self._listed:
            self._folder = folder_of(chosen)
        elif PREFERRED_FOLDER in known:
            self._folder = PREFERRED_FOLDER
        else:
            self._folder = self._folders[0][0] if self._folders else ""

    def build(self) -> None:
        with ui.element("aside").classes("mt-library").props('aria-label="Library"').mark(LIBRARY_MARK):
            self._head()
            self._search = ui.input(placeholder="Find a model by name").props(
                'borderless dense aria-label="Find a model"'
            )
            self._search.classes("mt-search").mark(SEARCH_MARK).on_value_change(
                lambda event: self._find(event.value or "")
            )
            self._grid()

    @ui.refreshable_method
    def _head(self) -> None:
        with ui.element("div").classes("mt-folder-head"):
            with ui.element("div"):
                if self._query:
                    words("Found", "mt-folder-title")
                    words(counted(len(self._found()), "model"), "mt-folder-count")
                else:
                    words(_folder_label(self._folder), "mt-folder-title")
                    words(counted(self._count(self._folder), "model"), "mt-folder-count")
            with Press("Folders", label="Choose a folder").classes("mt-link"):
                icon("down", 12)
                with ui.menu().props("anchor='bottom right' self='top right'"), ui.element("div").classes("mt-index"):
                    self._index()

    def _index(self) -> None:
        beings = [(folder, count) for folder, count in self._folders if not WORLD_FOLDER.fullmatch(folder)]
        worlds = [(folder, count) for folder, count in self._folders if WORLD_FOLDER.fullmatch(folder)]
        with ui.element("div"):
            for folder, count in beings:
                with Press(_folder_label(folder), on_click=partial(self.show, folder)).classes("mt-index-row") as row:
                    words(f"{count:,}", "mt-index-count")
                row.chosen(folder == self._folder)
        if worlds:
            words("World objects, by map folder", "mt-index-title")
            with ui.element("div").classes("mt-index-grid"):
                for folder, count in worlds:
                    with Press(folder, on_click=partial(self.show, folder)).classes("mt-index-small") as row:
                        words(f"{count:,}", "mt-index-small-count")
                    row.chosen(folder == self._folder)

    def _count(self, folder: str) -> int:
        return next((count for name, count in self._folders if name == folder), 0)

    def _found(self) -> tuple[str, ...]:
        query = self._query.lower()
        return tuple(file for file in self._listed if query in file.rpartition("/")[2].lower())[:MOST_FOUND]

    def _shown(self) -> tuple[str, ...]:
        if self._query:
            return self._found()
        return tuple(file for file in self._listed if folder_of(file) == self._folder)

    @ui.refreshable_method
    def _grid(self) -> None:
        self._plates = {}
        with ui.element("div").classes("mt-plates"):
            for file in self._shown():
                try:
                    entry: LibraryEntry | None = self._models.entry(file)
                except MutoolsError:
                    entry = None
                with Press(PurePosixPath(file).stem, on_click=partial(self._on_pick, file)).classes(
                    "mt-plate"
                ) as plate:
                    Thumbnail(file, entry.stamp if entry else "")
                    words(_facts(entry) if entry else UNREADABLE, "mt-plate-facts")
                plate.chosen(file == self._chosen)
                self._plates[file] = plate

    def _find(self, query: str) -> None:
        self._query = query.strip()
        self._head.refresh()
        self._grid.refresh()

    def show(self, folder: str) -> None:
        self._folder = folder
        if self._search.value:
            self._search.value = ""
            return
        self._query = ""
        self._head.refresh()
        self._grid.refresh()

    def choose(self, file: str) -> None:
        if file in self._listed and file not in self._plates and not self._query:
            self._chosen = file
            self.show(folder_of(file))
            return
        previous = self._plates.get(self._chosen)
        if previous is not None:
            previous.chosen(False)
        self._chosen = file
        plate = self._plates.get(file)
        if plate is not None:
            plate.chosen(True)
