import math
from pathlib import Path

import pytest

from mubmd.app.model_parts import tree_parents
from mubmd.app.model_scene import Quaternion, SceneBone, action_poses, angle_quaternion, model_scene
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.json_types import JsonObject
from tests.models import put, view_of

SWORD = "Item/Sword01.bmd"
MONSTER = "Monster/Monster01.bmd"
QUARTER = math.pi / 2
ROOT_HALF = math.sqrt(0.5)
CLOSE = 1e-6


@pytest.mark.parametrize(
    ("angles", "quaternion"),
    [
        ((0.0, 0.0, 0.0), (0.0, 0.0, 0.0, 1.0)),
        ((QUARTER, 0.0, 0.0), (ROOT_HALF, 0.0, 0.0, ROOT_HALF)),
        ((0.0, QUARTER, 0.0), (0.0, ROOT_HALF, 0.0, ROOT_HALF)),
        ((0.0, 0.0, QUARTER), (0.0, 0.0, ROOT_HALF, ROOT_HALF)),
        ((QUARTER, 0.0, QUARTER), (0.5, 0.5, 0.5, 0.5)),
    ],
)
def test_the_angles_turn_in_half_lifes_order(angles: tuple[float, float, float], quaternion: Quaternion) -> None:
    assert angle_quaternion(*angles) == pytest.approx(quaternion, abs=CLOSE)


def test_a_scene_has_a_corner_per_triangle_corner(reference_data: Path) -> None:
    scene = model_scene(view_of(reference_data, SWORD))

    assert scene.bones == (SceneBone("Box01", -1),)
    assert [(action.keys, action.lock) for action in scene.actions] == [(1, False)]
    (mesh,) = scene.meshes
    assert (len(mesh.bones), len(mesh.positions), len(mesh.uvs), mesh.skipped) == (204, 612, 408, 0)
    assert mesh.positions[:3] == pytest.approx((-1.417384, -1.0287832, -10.2511), abs=CLOSE)
    assert mesh.uvs[:2] == pytest.approx((0.9868, 0.4574), abs=CLOSE)
    assert mesh.bones[0] == 0


def test_a_scene_of_a_monster(reference_data: Path) -> None:
    scene = model_scene(view_of(reference_data, MONSTER))

    assert (len(scene.bones), len(scene.meshes)) == (45, 14)
    assert [len(mesh.bones) for mesh in scene.meshes[:4]] == [78, 18, 114, 210]
    assert [action.keys for action in scene.actions] == [20, 30, 7, 9, 9, 13, 9]
    assert scene.bones[3] == SceneBone("Bip01 L Thigh", 2)
    assert {scene.bones[index] for index in (9, 16, 21, 32, 41)} == {SceneBone("", -1)}


def test_a_bone_outside_the_skeleton_skips_its_triangles(reference_data: Path) -> None:
    scene = model_scene(view_of(reference_data, "object35/Object36.bmd"))

    assert [(len(mesh.bones), mesh.skipped) for mesh in scene.meshes] == [(336, 0), (0, 10)]


def test_a_quad_gives_two_triangles(reference_data: Path) -> None:
    view = view_of(reference_data, SWORD)
    put(view, "/meshes/0/triangles/0/polygon", 4)
    put(view, "/meshes/0/triangles/0/vertices/3", 3)
    put(view, "/meshes/0/triangles/0/uvs/3", 3)

    (mesh,) = model_scene(view).meshes

    assert (len(mesh.bones), mesh.skipped) == (207, 0)


def test_an_index_outside_its_list_skips_the_triangle(reference_data: Path) -> None:
    view = view_of(reference_data, SWORD)
    put(view, "/meshes/0/triangles/0/uvs/0", 9999)

    (mesh,) = model_scene(view).meshes

    assert (len(mesh.bones), mesh.skipped) == (201, 1)


def test_a_vertex_on_a_dummy_bone_is_skipped() -> None:
    corner: JsonObject = {"bone": 1, "position": [0.0, 0.0, 0.0]}
    triangle: JsonObject = {"polygon": 3, "vertices": [0, 0, 0, 0], "uvs": [0, 0, 0, 0]}
    view: JsonObject = {
        "bones": [{"name": "Root", "parent": -1, "actions": []}, {"dummy": 1}],
        "actions": [],
        "meshes": [{"vertices": [corner], "uvs": [[0.0, 0.0]], "triangles": [triangle]}],
    }

    (mesh,) = model_scene(view).meshes

    assert (len(mesh.bones), mesh.skipped) == (0, 1)


def test_a_parent_that_loops_hangs_from_the_root(reference_data: Path) -> None:
    view = view_of(reference_data, MONSTER)
    put(view, "/bones/2/parent", 3)

    scene = model_scene(view)

    assert (scene.bones[2].parent, scene.bones[3].parent) == (-1, -1)
    bones = view["bones"]
    assert isinstance(bones, list)
    assert tree_parents(bones)[2:4] == [-1, -1]


def test_a_model_with_nothing_is_an_empty_scene(reference_data: Path) -> None:
    scene = model_scene(view_of(reference_data, "Object3/Object25.bmd"))

    assert (scene.bones, scene.actions, scene.meshes) == ((), (), ())


def test_another_skeleton_gives_its_bones_and_actions(reference_data: Path) -> None:
    helm = view_of(reference_data, "player/HelmMale01.bmd")

    scene = model_scene(helm, view_of(reference_data, "player/Player.bmd"))

    assert (len(scene.bones), len(scene.actions)) == (60, 205)
    assert [len(mesh.bones) for mesh in scene.meshes] == [186, 18]
    assert {bone for mesh in scene.meshes for bone in mesh.bones} == {20}


def test_the_poses_of_an_action(reference_data: Path) -> None:
    sword = action_poses(view_of(reference_data, SWORD), 0)
    monster = action_poses(view_of(reference_data, MONSTER), 1)

    (box,) = sword.bones
    assert sword.keys == 1
    assert box is not None
    assert box.positions == pytest.approx((0.049219, 0.0, -0.078563), abs=CLOSE)
    assert box.quaternions == pytest.approx((0.649683, 0.279126, -0.279126, 0.649684), abs=CLOSE)
    root = monster.bones[0]
    assert monster.keys == 30
    assert root is not None
    assert (len(root.positions), len(root.quaternions)) == (90, 120)
    assert root.positions[:3] == pytest.approx((-0.225072, 3.207344, 101.761055), abs=CLOSE)
    assert root.quaternions[:4] == pytest.approx((0.0, 0.0, -0.707106, 0.707107), abs=CLOSE)
    assert monster.bones[9] is None


def test_an_action_the_model_does_not_have(reference_data: Path) -> None:
    with pytest.raises(MubmdError) as error:
        action_poses(view_of(reference_data, SWORD), 1)

    assert (error.value.code, error.value.pointer) == (ErrorCode.POINTER_NOT_FOUND, "/actions/1")
