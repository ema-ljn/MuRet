from collections.abc import Callable
from functools import partial
from pathlib import PurePosixPath
from typing import Final
from urllib.parse import urlencode

from nicegui import ui
from nicegui.events import GenericEventArguments

from mutools import __version__
from mutools.adapters.web.api import DONOR, OWN
from mutools.adapters.web.editor import Editor
from mutools.adapters.web.elements import RenderState, Shortcuts, Viewport
from mutools.adapters.web.finder import Command, Finder
from mutools.adapters.web.inspector import FINDINGS, MODEL, PARTS, Inspector, numbered
from mutools.adapters.web.library import Library
from mutools.adapters.web.models import Models, folder_of
from mutools.adapters.web.opened import OpenedModel, open_model
from mutools.adapters.web.theme import paint
from mutools.adapters.web.timeline import RATES, Timeline
from mutools.adapters.web.widgets import AXES, Press, icon, words
from mutools.app.model_parts import part_of
from mutools.core.errors import MutoolsError

TITLE: Final = "mutools"
VIEWS: Final = (("textured", "Textured"), ("lit", "Lit"), ("wire", "Wire"))
PICK_A_MODEL: Final = "Pick a model from the library."
PICK_A_MODEL_DETAIL: Final = "Its meshes, bones and actions show here, drawn the way the client draws them."
NOTHING_TO_DRAW: Final = "This model has no meshes and no bones: there is nothing to draw."
CANNOT_OPEN: Final = "This model cannot be opened."
REFERENCE_NOTE: Final = "Reference copy, read-only"
WORKING_NOTE: Final = "Working copy"
EDITING_NOTE: Final = "Working copy, editing"
PIXELS_NOTE: Final = "Twice the pixels at rest, one to one while the model moves."
STUDIO_MARK: Final = "studio"
VIEWPORT_MARK: Final = "viewport"


def address(file: str, on: str = OWN, action: int = 0) -> str:
    query = {"file": file} | ({"on": on} if on != OWN else {}) | ({"action": str(action)} if action else {})
    return "/?" + urlencode(query)


class Studio:
    def __init__(self, models: Models) -> None:
        self._models = models
        self._listed = models.listed()
        self._file = ""
        self._opened: OpenedModel | None = None
        self._trouble = ""
        self._action = 0
        self._rate = 1.0
        self._tab = PARTS
        self._part = ""
        self._bone: int | None = None
        self._editor: Editor | None = None
        self._view = VIEWS[0][0]
        self._bones = False
        self._additive = False
        self._modes: dict[str, Press] = {}
        self._toggles: dict[str, Press] = {}
        self._inspector = Inspector(models, self._choose, self._show_tab)
        self._timeline = Timeline(self._play, self._set_rate)

    def build(self, file: str, on: str, action: int) -> None:
        self._finder = Finder(self._listed, self.open, self._commands())
        shortcuts = Shortcuts(edit=self._models.edit).on("find", self._find)
        with ui.element("div").classes("mt-studio").mark(STUDIO_MARK):
            self._header()
            with ui.element("div").classes("mt-body"):
                self._library = Library(self._models, self._listed, file, self.open)
                self._library.build()
                with ui.element("main").classes("mt-main"):
                    self._viewport = Viewport().mark(VIEWPORT_MARK).on("failed", self._failed)
                    with self._viewport:
                        self._overlays()
                    self._timeline.build(None, 0, self._rate)
                    if self._models.edit:
                        self._editor = Editor(self._models, self._viewport, self._edited, self._play, self.open)
                        self._editor.build()
                self._inspector.build(None, self._tab, "")
            self._footer()
        if self._editor is not None:
            shortcuts.on("undo", self._editor.undo).on("redo", self._editor.redo).on("save", self._editor.save)
        if file:
            self.open(file, on, action, keep_address=True)

    def _commands(self) -> tuple[Command, ...]:
        views = tuple(Command(f"Show {label.lower()}", "", partial(self._set_view, view)) for view, label in VIEWS)
        rates = tuple(Command(f"Play at {label.lower()}", "", partial(self._set_rate, rate)) for rate, label in RATES)
        return (
            Command("Frame the model", "F", self._frame),
            Command("Play or pause", "Space", self._toggle_playing),
            Command("Show or hide the bones", "", lambda: self._set_bones(not self._bones)),
            Command("Draw additive", "", lambda: self._set_additive(not self._additive)),
            Command("Save a picture", "", self._picture),
            *views,
            *rates,
            Command("Show the findings", "", lambda: self._show_tab(FINDINGS)),
        )

    def _header(self) -> None:
        with ui.element("header").classes("mt-header"):
            with ui.element("div").classes("mt-brand"):
                words(TITLE, "mt-wordmark")
                words("studio", "mt-brand-studio")
            self._crumbs()
            with ui.element("div").classes("mt-header-end"):
                with ui.element("span").classes("mt-note"):
                    reference = self._models.store.is_reference(self._models.root)
                    icon("lock" if reference else "open", 13)
                    words(REFERENCE_NOTE if reference else EDITING_NOTE if self._models.edit else WORKING_NOTE)
                find = Press("Find or run", label="Find a model or run a command", on_click=self._find)
                with find.classes("mt-find"):
                    icon("search", 15)
                    words("Ctrl K", "mt-key")

    @ui.refreshable_method
    def _crumbs(self) -> None:
        with ui.element("nav").classes("mt-crumbs").props('aria-label="Where"'):
            words(self._models.root.name or str(self._models.root))
            if not self._file:
                return
            folder = folder_of(self._file)
            if folder:
                words("/", "mt-crumb-cut")
                Press(folder, on_click=lambda: self._library.show(folder)).classes("mt-crumb-folder")
            words("/", "mt-crumb-cut")
            words(PurePosixPath(self._file).name, "mt-crumb-file")

    def _footer(self) -> None:
        with ui.element("footer").classes("mt-footer"):
            RenderState("footer")
            words(PIXELS_NOTE)
            ui.element("span").classes("mt-footer-gap")
            words(str(self._models.root))
            words(f"mutools {__version__}")

    @ui.refreshable_method
    def _overlays(self) -> None:
        opened = self._opened
        if opened is None:
            self._message(CANNOT_OPEN if self._trouble else PICK_A_MODEL, self._trouble or PICK_A_MODEL_DETAIL)
            return
        self._modes, self._toggles = {}, {}
        with ui.element("div").classes("mt-vitrine-top"):
            with ui.element("div").classes("mt-modes").props('role="group" aria-label="View"'):
                for view, label in VIEWS:
                    mode = Press(label, on_click=partial(self._set_view, view)).classes("mt-mode")
                    self._modes[view] = mode.chosen(view == self._view)
                ui.element("span").classes("mt-modes-cut")
                self._toggle("bones", "Bones", self._bones, lambda: self._set_bones(not self._bones))
                self._toggle("additive", "Additive", self._additive, lambda: self._set_additive(not self._additive))
                if opened.donor is not None:
                    name = PurePosixPath(opened.donor).name
                    self._toggle("donor", f"On {name}'s skeleton", opened.on == DONOR, self._switch_skeleton)
            with ui.element("div").classes("mt-vitrine-tools"):
                with Press(label="Frame the model, F", on_click=self._frame).classes("mt-round-light"):
                    icon("frame", 17)
                with Press(label="Save a picture", on_click=self._picture).classes("mt-round-light"):
                    icon("picture", 17)
        with ui.element("div").classes("mt-label"):
            words(opened.name, "mt-model-name").tag = "h1"
            if opened.slot_line():
                words(opened.slot_line(), "mt-model-slot")
            words(opened.facts_line(), "mt-model-facts")
        with ui.element("div").classes("mt-vitrine-corner"):
            RenderState("vitrine")
            ui.html(AXES, sanitize=False, tag="span").classes("mt-axes")
        if self._trouble:
            self._message(CANNOT_OPEN, self._trouble)
        elif not opened.scene.meshes and not opened.scene.bones:
            self._message("Nothing to draw", NOTHING_TO_DRAW)

    def _message(self, title: str, text: str) -> None:
        with ui.element("div").classes("mt-vitrine-message").props('role="status"'):
            words(title, "mt-vitrine-message-title")
            words(text, "mt-vitrine-message-text")

    def _toggle(self, name: str, label: str, on: bool, flip: Callable[[], object]) -> None:
        self._toggles[name] = Press(label, on_click=flip).classes("mt-toggle").chosen(on)

    def open(self, file: str, on: str = OWN, action: int = 0, *, keep_address: bool = False) -> None:
        self._file, self._trouble, self._part, self._bone = file, "", "", None
        try:
            self._opened = open_model(self._models, file, on)
        except MutoolsError as error:
            self._opened, self._trouble = None, error.message
        opened = self._opened
        self._action = action if opened is not None and 0 <= action < len(opened.actions) else 0
        if opened is not None:
            self._bones = self._bones or not opened.drawn
            self._viewport.skeleton(self._bones)
            self._viewport.open(file, opened.on)
            self._send_action(start=False)
            ui.page_title(f"{opened.name}, {TITLE}")
        self._library.choose(file)
        self._crumbs.refresh()
        self._overlays.refresh()
        self._timeline.build.refresh(opened, self._action, self._rate)
        self._inspector.build.refresh(opened, self._tab, "")
        self._show_editor()
        if not keep_address:
            self._remember()

    def _show_editor(self) -> None:
        if self._editor is not None:
            self._editor.show(self._opened, self._action, self._bone)

    def _edited(self, action: int | None, *, replay: bool = True) -> None:
        if self._opened is None:
            return
        opened = open_model(self._models, self._file, self._opened.on)
        self._opened = opened
        shown = min(self._action, max(len(opened.actions) - 1, 0))
        if replay:
            self._viewport.reposed(action, opened.scene.actions)
        if shown != self._action and opened.actions:
            self._action = shown
            self._viewport.play(shown, repeats=opened.actions[shown].repeats, start=False)
            self._remember()
        self._overlays.refresh()
        self._timeline.build.refresh(opened, self._action, self._rate)
        self._inspector.build.refresh(opened, self._tab, self._part)
        self._show_editor()

    def _remember(self) -> None:
        if self._opened is not None:
            ui.navigate.history.replace(address(self._file, self._opened.on, self._action))

    def _failed(self, event: GenericEventArguments) -> None:
        self._trouble = str(event.args)
        self._overlays.refresh()

    def _send_action(self, *, start: bool) -> None:
        opened = self._opened
        if opened is None or not opened.actions:
            return
        shown = opened.actions[self._action]
        self._viewport.speed(shown.keys_per_second * self._rate)
        if start or self._action:
            self._viewport.play(self._action, repeats=shown.repeats, start=start)

    def _play(self, action: int) -> None:
        self._action = action
        self._send_action(start=True)
        self._timeline.build.refresh(self._opened, action, self._rate)
        self._show_editor()
        self._remember()

    def _set_rate(self, rate: float) -> None:
        self._rate = rate
        opened = self._opened
        if opened is not None and opened.actions:
            self._viewport.speed(opened.actions[self._action].keys_per_second * rate)
        self._timeline.build.refresh(opened, self._action, rate)

    def _set_view(self, view: str) -> None:
        self._view = view
        self._viewport.mode(view)
        for name, mode in self._modes.items():
            mode.chosen(name == view)

    def _set_bones(self, shown: bool) -> None:
        self._bones = shown
        self._viewport.skeleton(shown)
        if "bones" in self._toggles:
            self._toggles["bones"].chosen(shown)

    def _set_additive(self, on: bool) -> None:
        self._additive = on
        self._viewport.additive(on)
        if "additive" in self._toggles:
            self._toggles["additive"].chosen(on)

    def _switch_skeleton(self) -> None:
        if self._opened is not None:
            self.open(self._file, OWN if self._opened.on == DONOR else DONOR, self._action)

    def _find(self) -> None:
        self._finder.show()

    def _frame(self) -> None:
        self._viewport.frame()

    def _toggle_playing(self) -> None:
        self._viewport.toggle()

    def _picture(self) -> None:
        if self._opened is not None:
            self._viewport.picture(self._opened.name)

    def _show_tab(self, tab: str) -> None:
        self._tab = tab
        self._inspector.build.refresh(self._opened, tab, self._part)

    def _choose(self, pointer: str) -> None:
        opened = self._opened
        if opened is None:
            return
        part = part_of(pointer)
        mesh, bone, action = numbered(part, "meshes"), numbered(part, "bones"), numbered(part, "actions")
        if mesh is not None and part == self._part and self._tab == PARTS:
            part, mesh = "", None
        self._part, self._bone = part, bone
        self._viewport.highlight(mesh)
        self._viewport.mark_bone(bone)
        if bone is not None and not self._bones:
            self._set_bones(True)
        if action is not None and action < len(opened.actions):
            self._play(action)
        if self._tab == FINDINGS:
            self._tab = PARTS if mesh is not None or bone is not None else MODEL
        self._inspector.build.refresh(opened, self._tab, part)
        self._show_editor()


def add_page(models: Models) -> None:
    @ui.page("/", title=TITLE)
    def index(file: str = "", on: str = OWN, action: int = 0) -> None:
        paint()
        Studio(models).build(file, on, action)
