import json
from collections.abc import Callable, Sequence
from functools import partial
from typing import Any, Final
from urllib.parse import urlencode

from nicegui import ui

from mutools.adapters.web.models import Models
from mutools.adapters.web.opened import OpenedModel
from mutools.adapters.web.widgets import Press, words
from mutools.app.checks import TextureFiles
from mutools.app.model_parts import Part, counted, model_parts, part_facts, part_fields, part_title
from mutools.app.textures import mesh_texture
from mutools.core.errors import MutoolsError
from mutools.core.findings import Finding
from mutools.core.formats.model_textures import texture_use
from mutools.core.json_types import JsonValue
from mutools.core.leaves import leaf_text
from mutools.core.pointer import parse_pointer

PARTS: Final = "parts"
MODEL: Final = "model"
FINDINGS: Final = "findings"
TABS: Final = ((PARTS, "Parts"), (MODEL, "Model"))
PART_TOKENS: Final = 2
INSPECTOR_MARK: Final = "inspector"
PARTS_MARK: Final = "parts"


def numbered(part: str, group: str) -> int | None:
    tokens = parse_pointer(part)
    numbered = len(tokens) == PART_TOKENS and tokens[0] == group and tokens[1].isdecimal()
    return int(tokens[1]) if numbered else None


def _shown_value(value: JsonValue) -> str:
    text = leaf_text(value)
    return json.dumps(value) if text is None else text


def _part_nodes(parts: Sequence[Part]) -> list[dict[str, Any]]:
    return [{"id": part.pointer, "label": part.label, "children": _part_nodes(part.children)} for part in parts]


def _findings_label(findings: Sequence[Finding]) -> str:
    return f"Findings, {len(findings)}" if findings else "Findings, none"


def _fields(rows: Sequence[tuple[str, str]], bad: frozenset[str] = frozenset()) -> None:
    with ui.element("div").classes("mt-fields"):
        for name, value in rows:
            words(name, "mt-field-name")
            words(value, "mt-field-value mt-bad" if name in bad else "mt-field-value")


class Inspector:
    def __init__(self, models: Models, on_choose: Callable[[str], Any], on_tab: Callable[[str], Any]) -> None:
        self._models = models
        self._on_choose = on_choose
        self._on_tab = on_tab
        self._bones_open = False
        self._opened_for: int | None = None

    @ui.refreshable_method
    def build(self, opened: OpenedModel | None, tab: str, part: str) -> None:
        with ui.element("aside").classes("mt-inspector").props('aria-label="Inspector"').mark(INSPECTOR_MARK):
            if opened is None:
                return
            with ui.element("div").classes("mt-tabs").props('role="tablist" aria-label="Inspector"'):
                for name, label in (*TABS, (FINDINGS, _findings_label(opened.findings))):
                    Press(label, on_click=partial(self._on_tab, name)).classes("mt-tab").props(
                        f'role="tab" aria-selected="{str(name == tab).lower()}"'
                    ).chosen(name == tab)
            if tab == PARTS:
                self._parts(opened, part)
            elif tab == MODEL:
                self._model(opened, part)
            else:
                self._findings(opened.findings)

    def _swatch(self, opened: OpenedModel, files: TextureFiles, mesh: int, classes: str = "") -> None:
        found = None
        meshes = opened.own.get("meshes")
        facts = meshes[mesh] if isinstance(meshes, list) and mesh < len(meshes) else None
        if isinstance(facts, dict):
            name = leaf_text(facts.get("texture_name"))
            found = None if name is None else files.find(opened.path, texture_use(name).file)
        if found is None:
            ui.element("span").classes(f"mt-swatch {classes}".strip())
            return
        source = "/api/swatch?" + urlencode({"file": opened.file, "mesh": mesh})
        ui.element("img").classes(f"mt-swatch {classes}".strip()).props(f'src="{source}" alt="" loading="lazy"')

    def _parts(self, opened: OpenedModel, part: str) -> None:
        files = TextureFiles(self._models.store)
        chosen = numbered(part, "meshes")
        meshes = opened.own.get("meshes")
        listed = meshes if isinstance(meshes, list) else []
        with ui.element("div").classes("mt-section-head"):
            words(counted(len(listed), "mesh", "meshes"), "mt-section-title")
            words("triangles", "mt-column-name")
        with ui.element("div").classes("mt-meshes"):
            for index, mesh in enumerate(listed):
                facts = mesh if isinstance(mesh, dict) else {}
                name = leaf_text(facts.get("texture_name")) or f"mesh {index}"
                triangles = facts.get("triangles")
                with Press(name, on_click=partial(self._on_choose, f"/meshes/{index}")) as row:
                    row.classes("mt-mesh")
                    self._swatch(opened, files, index)
                    words(str(index), "mt-mesh-index")
                    words(str(len(triangles)) if isinstance(triangles, list) else "", "mt-mesh-count")
                row.chosen(index == chosen)
        if chosen is not None and chosen < len(opened.scene.meshes):
            self._mesh_card(opened, files, chosen)
        self._bones(opened, part)

    def _mesh_card(self, opened: OpenedModel, files: TextureFiles, mesh: int) -> None:
        pointer = f"/meshes/{mesh}"
        try:
            texture = mesh_texture(self._models.store, opened.path, opened.own, mesh)
            where = texture.file.relative_to(self._models.root).as_posix()
            shown, bad = ("texture", where), frozenset[str]()
        except MutoolsError as error:
            shown, bad = ("texture", f"not shown: {error.message}"), frozenset({"texture"})
        skipped = opened.scene.meshes[mesh].skipped
        notes = [shown] + ([("drawn", f"{counted(skipped, 'triangle')} not drawn")] if skipped else [])
        with ui.element("div").classes("mt-card"):
            with ui.element("div").classes("mt-card-head"):
                self._swatch(opened, files, mesh, "mt-swatch-large")
                with ui.element("div").classes("mt-card-titles"):
                    words(part_title(pointer).capitalize(), "mt-card-title")
                    name = leaf_text(part_fields(opened.own, pointer).get("texture_name"))
                    words(name or "", "mt-small")
            _fields(
                [*notes, *part_facts(opened.own, pointer)], bad | (frozenset({"drawn"}) if skipped else frozenset())
            )

    def _bones(self, opened: OpenedModel, part: str) -> None:
        bones = opened.scene.bones
        if not bones:
            return
        chosen = numbered(part, "bones")
        if chosen is not None and chosen != self._opened_for:
            self._bones_open = True
        self._opened_for = chosen
        shown = self._bones_open
        with ui.element("div").classes("mt-rows"):
            with Press("Bones", on_click=self._toggle_bones).classes("mt-row") as head:
                words(str(len(bones)), "mt-row-count")
            head.props(f'aria-expanded="{str(shown).lower()}"')
            if shown:
                for index, bone in enumerate(bones):
                    Press(f"{index}  {bone.name}", on_click=partial(self._on_choose, f"/bones/{index}")).classes(
                        "mt-row"
                    ).chosen(index == chosen)

    def _toggle_bones(self) -> None:
        self._bones_open = not self._bones_open
        self.build.refresh()

    def _model(self, opened: OpenedModel, part: str) -> None:
        nodes = _part_nodes(model_parts(opened.view).children)
        tree = ui.tree(nodes, on_select=lambda event: self._on_choose(event.value or "")).props(
            "dense no-selection-unset selected-color=primary"
        )
        tree.classes("mt-parts").mark(PARTS_MARK)
        if part:
            tree.props(f'selected="{part}"')
        with ui.element("div").classes("mt-card"):
            words(part_title(part).capitalize(), "mt-card-title")
            fields = [(name, _shown_value(value)) for name, value in part_fields(opened.view, part).items()]
            _fields([*part_facts(opened.view, part), *fields])

    def _findings(self, findings: Sequence[Finding]) -> None:
        if not findings:
            words("The checks found nothing in this model.", "mt-quiet")
            return
        with ui.element("div").classes("mt-rows"):
            for finding in findings:
                with Press(on_click=partial(self._on_choose, finding.pointer)).classes("mt-finding"):
                    words(f"{finding.code} {finding.pointer}", "mt-bad" if finding.severity == "error" else "mt-warn")
                    words(finding.message, "mt-small")
