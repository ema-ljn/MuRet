import math
import shutil
from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

from mutools.adapters.gltf.reader import read_glb
from mutools.adapters.gltf.writer import write_glb
from mutools.adapters.storage import LocalFileStore
from mutools.app.gltf_actions import export_gltf, import_gltf
from mutools.app.model_scene import angle_quaternion
from mutools.core.json_types import JsonObject
from tests.models import at, view_of

pytestmark = pytest.mark.blender
bpy: Any = pytest.importorskip("bpy")
mathutils: Any = pytest.importorskip("mathutils")

FILES = ("player/Player.bmd", "player/HelmMale01.bmd", "player/head_02.OZJ", "player/hide.OZJ")
ACTIONS = (0, 1, 194)
EDITED_ACTION = 1
CALF = 4
KEY = 3
TURN = 0.3
STORE = LocalFileStore()


@pytest.fixture
def player(tmp_path: Path, reference_data: Path) -> Path:
    for name in FILES:
        target = tmp_path / "Data" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(reference_data / name, target)
    return tmp_path / "Data" / "player" / "Player.bmd"


def through_blender(source: Path, target: Path, edit: Callable[[Any], None] | None = None) -> None:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(source))
    if edit is not None:
        edit(next(found for found in bpy.data.objects if found.type == "ARMATURE"))
    bpy.ops.export_scene.gltf(filepath=str(target), export_format="GLB")


def exported(player: Path) -> Path:
    output = player.with_suffix(".glb")
    helm = player.with_name("HelmMale01.bmd")
    export_gltf(STORE, write_glb, player, output, parts=[helm], actions=ACTIONS, dry_run=False)
    return output


def turn_the_calf(armature: Any) -> None:
    armature.animation_data.action = bpy.data.actions[f"{EDITED_ACTION:03} Action {EDITED_ACTION}"]
    bpy.context.scene.frame_set(KEY)
    calf = armature.pose.bones["Bip01 L Calf"]
    calf.rotation_quaternion = calf.rotation_quaternion @ mathutils.Quaternion((1.0, 0.0, 0.0), TURN)
    calf.keyframe_insert("rotation_quaternion", frame=KEY)


def turn_of(view: JsonObject, key: int) -> tuple[float, ...]:
    return angle_quaternion(*at(view, f"/bones/{CALF}/actions/{EDITED_ACTION}/rotations/{key}"))


def test_actions_come_back_from_blender_as_they_were(player: Path) -> None:
    before = player.read_bytes()
    back = player.with_name("back.glb")
    through_blender(exported(player), back)

    for action in ACTIONS:
        animation = f"{action:03} Action {action}"
        import_gltf(STORE, read_glb, player, back, animation=animation, at=action, dry_run=False)

    assert player.read_bytes() == before


def test_a_turn_made_in_blender_comes_back_in_the_action(player: Path, reference_data: Path) -> None:
    back = player.with_name("back.glb")
    through_blender(exported(player), back, turn_the_calf)

    animation = f"{EDITED_ACTION:03} Action {EDITED_ACTION}"
    import_gltf(STORE, read_glb, player, back, animation=animation, at=EDITED_ACTION, dry_run=False)

    stored, edited = view_of(reference_data, "player/Player.bmd"), view_of(player.parents[1], "player/Player.bmd")
    dot = abs(sum(a * b for a, b in zip(turn_of(stored, KEY), turn_of(edited, KEY), strict=True)))
    assert 2 * math.acos(min(1.0, dot)) == pytest.approx(TURN, abs=1e-3)
    changed = [
        pointer
        for pointer in (f"/bones/{bone}/actions/{EDITED_ACTION}" for bone in range(len(at(stored, "/bones"))))
        if "dummy" not in at(stored, pointer.rsplit("/actions", 1)[0]) and at(stored, pointer) != at(edited, pointer)
    ]
    assert changed == [f"/bones/{CALF}/actions/{EDITED_ACTION}"]
    rotations = f"/bones/{CALF}/actions/{EDITED_ACTION}/rotations"
    assert [key for key, turn in enumerate(at(edited, rotations)) if turn != at(stored, f"{rotations}/{key}")] == [KEY]
