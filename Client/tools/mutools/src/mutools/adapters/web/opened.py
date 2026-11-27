from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Final

from mutools.adapters.web.api import DONOR, OWN
from mutools.adapters.web.models import Models
from mutools.app.action_speeds import ActionSpeeds, Source, action_speeds
from mutools.app.model_parts import counted
from mutools.app.model_scene import Scene, model_scene
from mutools.core.findings import Finding
from mutools.core.json_types import JsonObject

DEFAULT_KEYS_PER_SECOND: Final = 8.0
ENCRYPTED_VERSION: Final = 12
KILOBYTE: Final = 1024
SPEED_DIGITS: Final = 2


@dataclass(frozen=True, slots=True)
class ShownAction:
    index: int
    name: str
    keys: int
    lock: bool
    repeats: bool
    client_keys_per_second: float | None

    @property
    def keys_per_second(self) -> float:
        return self.client_keys_per_second or DEFAULT_KEYS_PER_SECOND

    @property
    def seconds(self) -> float:
        return self.keys / self.keys_per_second


@dataclass(frozen=True, slots=True)
class OpenedModel:
    file: str
    path: Path
    own: JsonObject
    view: JsonObject
    findings: tuple[Finding, ...]
    donor: str | None
    on: str
    skeleton: str
    scene: Scene
    speeds: ActionSpeeds
    actions: tuple[ShownAction, ...]
    size: int

    @property
    def name(self) -> str:
        return PurePosixPath(self.file).stem

    @property
    def source(self) -> Source:
        return self.speeds.source

    @property
    def drawn(self) -> bool:
        return any(mesh.bones for mesh in self.scene.meshes)

    @property
    def triangles(self) -> int:
        meshes = self.own.get("meshes")
        listed = meshes if isinstance(meshes, list) else []
        return sum(
            len(triangles)
            for mesh in listed
            if isinstance(mesh, dict) and isinstance(triangles := mesh.get("triangles"), list)
        )

    def slot_line(self) -> str:
        if self.speeds.model is None:
            return "The player's skeleton and actions" if self.source == "player" else ""
        kind = "a monster" if self.source == "monster" else "an NPC"
        return f"Model 0x{self.speeds.model:x} in the client, {kind}"

    def facts_line(self) -> str:
        version = self.own.get("version")
        cipher = "encrypted with K16" if version == ENCRYPTED_VERSION else "not encrypted"
        parts = [
            counted(len(self.scene.meshes), "mesh", "meshes"),
            counted(len(self.scene.bones), "bone"),
            counted(len(self.scene.actions), "action"),
        ]
        listed = f"{', '.join(parts)} and {counted(self.triangles, 'triangle')}"
        return f"{listed}. Version {version}, {cipher}, {max(round(self.size / KILOBYTE), 1)} KB."

    def speed_line(self, action: ShownAction, rate: float) -> str:
        speed = f"{round(action.keys_per_second * rate, SPEED_DIGITS):g} keys a second"
        if action.client_keys_per_second is None:
            known = "the client's speed for this action is not known"
        elif self.source == "npc":
            known = "as the client plays NPCs"
        elif self.source == "monster":
            known = "as the client plays this monster"
        else:
            known = "as the client plays the player"
        return f"{speed}, {known}" if rate == 1 else f"{speed}, {rate:g} times {known.removeprefix('as ')}"


def _shown(scene: Scene, speeds: ActionSpeeds) -> tuple[ShownAction, ...]:
    return tuple(
        ShownAction(
            index,
            (speeds.names[index] or f"action {index}").capitalize(),
            action.keys,
            action.lock,
            speeds.speeds[index].repeats,
            speeds.speeds[index].keys_per_second,
        )
        for index, action in enumerate(scene.actions)
    )


def open_model(models: Models, file: str, on: str) -> OpenedModel:
    session = models.session(file)
    donor = models.donor(file)
    shown_on = DONOR if on == DONOR and donor is not None else OWN
    view: JsonObject = session.view
    skeleton = file
    if donor is not None and shown_on == DONOR:
        posed = models.session(donor).view
        view = {**session.view, "actions": posed["actions"], "bones": posed["bones"]}
        skeleton = donor
    scene = model_scene(view)
    speeds = action_speeds(skeleton, len(scene.actions))
    return OpenedModel(
        file=file,
        path=session.path,
        own=session.view,
        view=view,
        findings=session.findings,
        donor=donor,
        on=shown_on,
        skeleton=skeleton,
        scene=scene,
        speeds=speeds,
        actions=_shown(scene, speeds),
        size=session.path.stat().st_size,
    )
