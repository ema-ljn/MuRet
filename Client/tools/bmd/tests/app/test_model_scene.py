import math
from pathlib import Path

import pytest

from mubmd.adapters.storage import LocalFileStore
from mubmd.app.model_parts import tree_parents
from mubmd.app.model_scene import (
    Bounds,
    Quaternion,
    SceneBone,
    action_poses,
    angle_quaternion,
    borrows_skeleton,
    donor_of,
    model_scene,
    rest_bounds,
)
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.json_types import JsonObject, JsonValue
from tests.models import put, view_of

SWORD = "Item/Sword01.bmd"
MONSTER = "Monster/Monster01.bmd"
QUARTER = math.pi / 2
ROOT_HALF = math.sqrt(0.5)
CLOSE = 1e-6
NEAR = 0.01
PLAYER = "player/Player.bmd"
HELM = "player/HelmMale01.bmd"
OWN_SKELETON = ("Helper01", "Helper02", "Helper03", "Helper04", "Wing02", "Wing03", "Wing04", "playerTest")

type Corner = tuple[float, float, float]


def assert_box(bounds: Bounds | None, low: Corner, high: Corner) -> None:
    assert bounds is not None
    assert bounds.low == pytest.approx(low, abs=NEAR)
    assert bounds.high == pytest.approx(high, abs=NEAR)


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
    helm = view_of(reference_data, HELM)

    scene = model_scene(helm, view_of(reference_data, PLAYER))

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


def test_a_locked_action_keeps_its_first_bone_in_place(reference_data: Path) -> None:
    walk = action_poses(view_of(reference_data, MONSTER), 2)

    first = walk.bones[0]
    assert first is not None
    assert first.positions[0::3] == pytest.approx([-0.225] * 7, abs=NEAR)
    assert first.positions[1::3] == pytest.approx([3.207] * 7, abs=NEAR)
    assert first.positions[2::3] == pytest.approx([101.761, 108.772, 109.859] * 2 + [101.761], abs=NEAR)


def test_an_action_the_model_does_not_have(reference_data: Path) -> None:
    with pytest.raises(MubmdError) as error:
        action_poses(view_of(reference_data, SWORD), 1)

    assert (error.value.code, error.value.pointer) == (ErrorCode.POINTER_NOT_FOUND, "/actions/1")


@pytest.mark.parametrize(
    ("name", "low", "high"),
    [
        (SWORD, (-1.673, -64.725, -9.254), (1.771, 10.318, 8.245)),
        (MONSTER, (-79.39, -112.313, -1.25), (84.139, 51.869, 214.797)),
        ("NPC/BloodCastle01.bmd", (-211.746, -128.875, -12.23), (64.793, 276.016, 139.654)),
    ],
)
def test_the_box_of_a_model_at_rest(reference_data: Path, name: str, low: Corner, high: Corner) -> None:
    assert_box(rest_bounds(view_of(reference_data, name)), low, high)


def test_the_box_leaves_out_what_is_not_drawn(reference_data: Path) -> None:
    bounds = rest_bounds(view_of(reference_data, "object35/Object36.bmd"))

    assert_box(bounds, (-161.949, -145.952, -18.27), (193.877, 196.27, 186.079))


def test_a_model_with_no_meshes_is_boxed_by_its_bones(reference_data: Path) -> None:
    bounds = rest_bounds(view_of(reference_data, PLAYER))

    assert_box(bounds, (-20.701, -23.303, -6.908), (23.044, 24.656, 168.342))


def test_a_model_with_nothing_has_no_box(reference_data: Path) -> None:
    assert rest_bounds(view_of(reference_data, "Object3/Object25.bmd")) is None


def test_two_bones_in_a_chain() -> None:
    def bone(parent: int, position: list[JsonValue], turn: list[JsonValue]) -> JsonObject:
        return {"name": "Bone", "parent": parent, "actions": [{"positions": [position], "rotations": [turn]}]}

    corners: list[JsonValue] = [
        {"bone": 1, "position": [1.0, 0.0, 0.0]},
        {"bone": 1, "position": [0.0, 1.0, 0.0]},
        {"bone": 1, "position": [0.0, 0.0, 1.0]},
    ]
    triangle: JsonObject = {"polygon": 3, "vertices": [0, 1, 2, 0], "uvs": [0, 0, 0, 0]}
    view: JsonObject = {
        "bones": [bone(-1, [0.0, 0.0, 10.0], [0.0, 0.0, QUARTER]), bone(0, [5.0, 0.0, 0.0], [0.0, 0.0, 0.0])],
        "actions": [{"keys": 1, "lock": 0}],
        "meshes": [{"vertices": corners, "uvs": [[0.0, 0.0]], "triangles": [triangle]}],
    }

    assert_box(rest_bounds(view), (-1.0, 5.0, 10.0), (0.0, 6.0, 11.0))


def test_a_part_on_the_players_skeleton(reference_data: Path) -> None:
    helm = view_of(reference_data, HELM)

    on_player = rest_bounds(helm, view_of(reference_data, PLAYER))

    assert_box(on_player, (-13.141, -16.272, 160.149), (25.936, 34.578, 192.624))
    assert_box(rest_bounds(helm), (-16.199, -19.041, 150.363), (13.603, 30.907, 182.822))


def test_the_parts_that_borrow_the_players_skeleton(reference_data: Path) -> None:
    player = view_of(reference_data, PLAYER)
    others = sorted(path for path in (reference_data / "player").glob("*.bmd") if path.name != "Player.bmd")

    borrows = {path.stem: borrows_skeleton(view_of(reference_data, f"player/{path.name}"), player) for path in others}

    assert sorted(name for name, borrowed in borrows.items() if not borrowed) == sorted(OWN_SKELETON)
    assert sum(borrows.values()) == 219


def test_the_donor_is_the_folders_player(reference_data: Path) -> None:
    store = LocalFileStore()

    assert donor_of(store, reference_data / HELM) == reference_data / PLAYER
    assert donor_of(store, reference_data / PLAYER) is None
    assert donor_of(store, reference_data / SWORD) is None
