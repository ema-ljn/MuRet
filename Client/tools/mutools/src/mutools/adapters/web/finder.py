from collections.abc import Callable, Sequence
from dataclasses import dataclass
from functools import partial
from pathlib import PurePosixPath
from typing import Any, Final

from nicegui import ui

from mutools.adapters.web.models import folder_of
from mutools.adapters.web.widgets import Press, words

MOST_MODELS: Final = 12
FINDER_MARK: Final = "finder"
INPUT_MARK: Final = "finder-input"
TOP_FOLDER: Final = "the top folder"


@dataclass(frozen=True, slots=True)
class Command:
    name: str
    keys: str
    run: Callable[[], Any]


def _where(file: str) -> str:
    return f"Model in {folder_of(file) or TOP_FOLDER}"


class Finder:
    def __init__(self, listed: Sequence[str], on_open: Callable[[str], Any], commands: Sequence[Command]) -> None:
        self._listed = tuple(listed)
        self._on_open = on_open
        self._commands = tuple(commands)
        self._query = ""
        with ui.dialog() as self._dialog, ui.element("div").classes("mt-palette").mark(FINDER_MARK):
            self._input = ui.input(placeholder="Find a model or run a command").props(
                'borderless dense autofocus aria-label="Find a model or run a command"'
            )
            self._input.classes("mt-search mt-palette-input").mark(INPUT_MARK)
            self._input.on_value_change(lambda event: self._find(event.value or ""))
            self._input.on("keydown.enter", self._run_first)
            self._results()

    def _models(self) -> tuple[str, ...]:
        if not self._query:
            return ()
        query = self._query.lower()
        return tuple(file for file in self._listed if query in PurePosixPath(file).stem.lower())[:MOST_MODELS]

    def _matching(self) -> tuple[Command, ...]:
        query = self._query.lower()
        return tuple(command for command in self._commands if query in command.name.lower())

    @ui.refreshable_method
    def _results(self) -> None:
        models, commands = self._models(), self._matching()
        with ui.element("div").classes("mt-palette-results").props('role="listbox"'):
            for file in models:
                with Press(PurePosixPath(file).stem, on_click=partial(self._open, file)).classes("mt-palette-item"):
                    words(_where(file), "mt-palette-kind")
            for command in commands:
                with Press(command.name, on_click=partial(self._run, command)).classes("mt-palette-item"):
                    words(command.keys, "mt-palette-kind")
            if self._query and not models and not commands:
                words(f"Nothing is called {self._query!r}.", "mt-quiet mt-palette-none")

    def _find(self, query: str) -> None:
        self._query = query.strip()
        self._results.refresh()

    def _open(self, file: str) -> None:
        self._dialog.close()
        self._on_open(file)

    def _run(self, command: Command) -> None:
        self._dialog.close()
        command.run()

    def _run_first(self) -> None:
        models, commands = self._models(), self._matching()
        if models:
            self._open(models[0])
        elif commands:
            self._run(commands[0])

    def show(self) -> None:
        self._input.value = ""
        self._dialog.open()
