import json
import math
from collections.abc import Sequence
from contextlib import suppress
from functools import partial
from pathlib import PurePosixPath
from typing import Any, Final
from urllib.parse import urlencode

from nicegui import ui
from nicegui.events import ValueChangeEventArguments

from mubmd.adapters.palette import LORENCIA
from mubmd.adapters.web.api import DONOR, OWN
from mubmd.adapters.web.models import Models
from mubmd.adapters.web.scene import BmdModel
from mubmd.app.model_parts import Part, counted, model_parts, part_facts, part_fields, part_of, part_title
from mubmd.app.model_scene import Bounds, model_scene, rest_bounds
from mubmd.app.textures import mesh_texture
from mubmd.core.errors import MubmdError
from mubmd.core.findings import Finding
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.leaves import leaf_text
from mubmd.core.pointer import parse_pointer

DEFAULT_SPEED: Final = 8
SPEEDS: Final = (2, 4, 8, 16)
FILES_MARK: Final = "files"
PARTS_MARK: Final = "parts"
THIS_FOLDER: Final = "."
PICK_A_MODEL: Final = "Pick a model on the left."
NOTHING_TO_DRAW: Final = "This model has no meshes and no bones: there is nothing to draw."
SHOW_CHOSEN_FILE: Final = "document.querySelector('.q-tree__node--selected')?.scrollIntoView({block: 'center'})"
PART_TOKENS: Final = 2
SCENE_WIDTH: Final = 960
SCENE_HEIGHT: Final = 640
FRAMES_PER_SECOND: Final = 60
DEFAULT_BOUNDS: Final = Bounds((-50.0, -50.0, 0.0), (50.0, 50.0, 100.0))
VIEW_DIRECTION: Final = (0.45, -0.8, 0.4)
VIEW_DISTANCE: Final = 2.2
STYLE: Final = f"""
body {{ background: {LORENCIA.background}; color: {LORENCIA.foreground}; }}
.mubmd-panel {{ background: {LORENCIA.surface}; border: 1px solid {LORENCIA.panel}; border-radius: 4px; }}
.mubmd-side {{ width: 20rem; flex-shrink: 0; max-height: calc(100vh - 6rem); overflow: auto; }}
.mubmd-title {{ color: {LORENCIA.accent}; font-weight: 600; }}
.mubmd-dim {{ color: {LORENCIA.secondary}; }}
.mubmd-scene {{ width: 100%; height: calc(100vh - 17rem); min-height: 20rem; }}
.mubmd-key {{ min-width: 5rem; font-variant-numeric: tabular-nums; }}
"""


def _address(file: str, on: str = OWN) -> str:
    return "/?" + urlencode({"file": file} if on == OWN else {"file": file, "on": on})


def _folder_label(folder: str) -> str:
    return folder or THIS_FOLDER


def _file_nodes(listed: Sequence[str]) -> list[dict[str, Any]]:
    folders: dict[str, list[str]] = {}
    for file in listed:
        folders.setdefault(_folder_label(file.rpartition("/")[0]), []).append(file)
    return [
        {
            "id": folder,
            "label": f"{folder} ({len(files)})",
            "selectable": False,
            "children": [{"id": file, "label": PurePosixPath(file).name} for file in files],
        }
        for folder, files in folders.items()
    ]


def _part_nodes(parts: Sequence[Part]) -> list[dict[str, Any]]:
    return [{"id": part.pointer, "label": part.label, "children": _part_nodes(part.children)} for part in parts]


def _shown_value(value: JsonValue) -> str:
    text = leaf_text(value)
    return json.dumps(value) if text is None else text


def _numbered(part: str, group: str) -> int | None:
    tokens = parse_pointer(part)
    numbered = len(tokens) == PART_TOKENS and tokens[0] == group and tokens[1].isdecimal()
    return int(tokens[1]) if numbered else None


def _paint() -> None:
    ui.colors(
        primary=LORENCIA.primary,
        secondary=LORENCIA.secondary,
        accent=LORENCIA.accent,
        dark=LORENCIA.surface,
        dark_page=LORENCIA.background,
        positive=LORENCIA.success,
        negative=LORENCIA.error,
        warning=LORENCIA.warning,
    )
    ui.add_css(STYLE)


def _files_panel(listed: Sequence[str], file: str) -> None:
    def go(event: ValueChangeEventArguments[str | None]) -> None:
        if event.value is not None and event.value in listed:
            ui.navigate.to(_address(event.value))

    with ui.column().classes("mubmd-panel mubmd-side q-pa-sm"):
        search = ui.input(placeholder="filter").props("dense clearable dark").classes("w-full")
        tree = (
            ui.tree(_file_nodes(listed), on_select=go)
            .props("dense dark no-selection-unset selected-color=primary")
            .mark(FILES_MARK)
        )
        search.bind_value_to(tree, "filter")
        if file in listed:
            tree.expand([_folder_label(file.rpartition("/")[0])]).select(file)
            ui.context.client.on_connect(lambda: ui.run_javascript(SHOW_CHOSEN_FILE))


class _ModelPanels:
    def __init__(self, models: Models, file: str, on: str) -> None:
        session = models.session(file)
        self._models = models
        self._file = file
        self._path = session.path
        self._own: JsonObject = session.view
        self._findings = session.findings
        self._donor = models.donor(file)
        self._on = DONOR if on == DONOR and self._donor is not None else OWN
        self._view: JsonObject = self._own
        if self._donor is not None and self._on == DONOR:
            posed = models.session(self._donor).view
            self._view = {**self._own, "actions": posed["actions"], "bones": posed["bones"]}
        self._scene = model_scene(self._view)
        self._bounds = rest_bounds(self._view) or DEFAULT_BOUNDS
        self._action = 0
        self._playing = bool(self._scene.actions)

    def _keys(self) -> int:
        return self._scene.actions[self._action].keys if self._scene.actions else 0

    def _action_text(self) -> str:
        return f"action {self._action} · {counted(self._keys(), 'key')}" if self._scene.actions else "no actions"

    def _camera(self, scene: ui.scene) -> None:
        low, high = self._bounds.low, self._bounds.high
        center = [(low[axis] + high[axis]) / 2 for axis in range(len(low))]
        distance = max(math.dist(low, high), 1.0) * VIEW_DISTANCE / 2
        x, y, z = (center[axis] + VIEW_DIRECTION[axis] * distance for axis in range(len(center)))
        look_at_x, look_at_y, look_at_z = center
        scene.move_camera(x, y, z, look_at_x, look_at_y, look_at_z, duration=0)

    def _send_state(self) -> None:
        self._model.speed(self._speed.value)
        self._model.skeleton(self._skeleton.value)
        self._model.additive(self._additive.value)
        if self._scene.actions:
            self._model.play(self._action)
            if not self._playing:
                self._model.pause()

    def _show_playing(self) -> None:
        self._play_button.props(f"icon={'pause' if self._playing else 'play_arrow'}")

    async def _toggle_playing(self) -> None:
        if self._playing:
            key = None
            with suppress(TimeoutError):
                key = await self._model.pause()
            if isinstance(key, int):
                self._key.value = key
        else:
            self._model.resume()
        self._playing = not self._playing
        self._show_playing()

    def _play(self, action: int) -> None:
        self._action = action
        self._playing = True
        self._action_label.text = self._action_text()
        self._key.props(f"max={max(self._keys() - 1, 0)}")
        self._key.value = 0
        self._model.play(action)
        self._show_playing()

    def _toolbar(self) -> None:
        with ui.row().classes("items-center w-full q-gutter-x-sm"):
            self._play_button = ui.button(on_click=self._toggle_playing).props("flat dense round")
            self._show_playing()
            self._key = ui.slider(
                min=0,
                max=max(self._keys() - 1, 0),
                step=1,
                value=0,
                on_change=lambda event: self._model.seek(event.value),
            ).classes("col")
            ui.label().classes("mubmd-key")
            self._action_label = ui.label(self._action_text()).classes("mubmd-dim")
            self._speed = ui.select(
                {speed: f"{speed} keys/s" for speed in SPEEDS},
                value=DEFAULT_SPEED,
                on_change=lambda event: self._model.speed(event.value),
            ).props("dense borderless dark")
        with ui.row().classes("items-center w-full q-gutter-x-md"):
            drawn = any(mesh.bones for mesh in self._scene.meshes)
            self._skeleton = ui.switch(
                "skeleton", value=not drawn, on_change=lambda event: self._model.skeleton(event.value)
            )
            self._additive = ui.switch("additive", on_change=lambda event: self._model.additive(event.value))
            if self._donor is not None:
                ui.switch(
                    f"On {PurePosixPath(self._donor).name}'s skeleton",
                    value=self._on == DONOR,
                    on_change=lambda event: ui.navigate.to(_address(self._file, DONOR if event.value else OWN)),
                )

    def _middle(self) -> None:
        with ui.column().classes("mubmd-panel col q-pa-sm"):
            ui.label(self._file).classes("mubmd-title")
            self._toolbar()
            if not self._scene.bones and not self._scene.meshes:
                ui.label(NOTHING_TO_DRAW).classes("mubmd-dim")
            distance = math.dist(self._bounds.low, self._bounds.high) * VIEW_DISTANCE
            camera = ui.scene.perspective_camera(near=max(distance / 1000, 0.01), far=max(distance * 50, 1000))
            # NiceGUI's grid comes with a floor that hides what a model has under z = 0: bmd_model.js draws ours.
            with ui.scene(
                SCENE_WIDTH,
                SCENE_HEIGHT,
                grid=False,
                camera=camera,
                background_color=LORENCIA.background,
                fps=FRAMES_PER_SECOND,
            ).classes("mubmd-scene") as scene:
                self._model = BmdModel(self._file, self._on)
            self._camera(scene)
            scene.on("init", self._send_state)

    def _mesh_notes(self, mesh: int) -> list[tuple[str, str]]:
        notes: list[tuple[str, str]] = []
        skipped = self._scene.meshes[mesh].skipped
        if skipped:
            notes.append(("drawn", f"{counted(skipped, 'triangle')} not drawn"))
        try:
            mesh_texture(self._models.store, self._path, self._own, mesh)
        except MubmdError as error:
            notes.append(("texture", f"not shown: {error.message}"))
        return notes

    @ui.refreshable_method
    def _details(self, part: str) -> None:
        mesh = _numbered(part, "meshes")
        notes = self._mesh_notes(mesh) if mesh is not None and mesh < len(self._scene.meshes) else []
        fields = [(name, _shown_value(value)) for name, value in part_fields(self._view, part).items()]
        ui.label(part_title(part)).classes("mubmd-title")
        with ui.grid(columns="auto 1fr").classes("gap-x-4 gap-y-0 w-full"):
            for name, value in (*part_facts(self._view, part), *notes, *fields):
                ui.label(name).classes("mubmd-dim")
                ui.label(value).style("overflow-wrap: anywhere")

    def _choose(self, pointer: str | None) -> None:
        part = part_of(pointer or "")
        bone = _numbered(part, "bones")
        action = _numbered(part, "actions")
        self._details.refresh(part)
        self._model.highlight(_numbered(part, "meshes"))
        self._model.mark_bone(bone)
        if bone is not None:
            self._skeleton.value = True
        if action is not None and action < len(self._scene.actions):
            self._play(action)

    def _choose_finding(self, finding: Finding) -> None:
        part = part_of(finding.pointer)
        self._parts.select(part or None)
        self._choose(part)

    def _right(self) -> None:
        with ui.column().classes("mubmd-panel mubmd-side q-pa-sm"):
            nodes = _part_nodes(model_parts(self._view).children)
            self._parts = ui.tree(nodes, on_select=lambda event: self._choose(event.value)).props(
                "dense dark selected-color=primary"
            )
            self._parts.mark(PARTS_MARK)
            ui.separator()
            self._details("")
            ui.separator()
            ui.label(f"findings ({len(self._findings)})").classes("mubmd-title")
            for finding in self._findings:
                with ui.column().classes("gap-0 cursor-pointer").on("click", partial(self._choose_finding, finding)):
                    ui.label(f"{finding.code} {finding.pointer}").classes(
                        "text-negative" if finding.severity == "error" else "text-warning"
                    )
                    ui.label(finding.message).classes("mubmd-dim")

    def build(self) -> None:
        self._middle()
        self._right()


def add_page(models: Models) -> None:
    @ui.page("/", title="mubmd")
    def index(file: str = "", on: str = OWN) -> None:
        _paint()
        with ui.header().classes("items-center q-py-xs").style(f"background: {LORENCIA.panel}"):
            ui.label("◆ mubmd").classes("mubmd-title")
            ui.label(str(models.root)).classes("mubmd-dim")
        with ui.row().classes("w-full no-wrap items-start q-pa-sm q-gutter-x-sm"):
            _files_panel(models.listed(), file)
            if not file:
                ui.label(PICK_A_MODEL).classes("mubmd-dim q-pa-md")
                return
            try:
                panels = _ModelPanels(models, file, on)
            except MubmdError as error:
                ui.label(error.message).classes("text-negative q-pa-md")
                return
            panels.build()
