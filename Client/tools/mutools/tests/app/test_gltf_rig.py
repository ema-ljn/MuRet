import struct
from itertools import pairwise
from pathlib import Path

import pytest

from mutools.app.gltf_rig import (
    FRAMES_PER_SECOND,
    JPEG,
    ChosenAction,
    Rig,
    RigImage,
    SkinnedMesh,
    joint_names,
    model_rig,
)
from mutools.app.model_scene import SceneMesh, angle_quaternion, model_scene, rest_bounds
from mutools.core.affine import Affine
from mutools.core.json_types import JsonObject
from tests.models import at, view_of

RIDER = "skill/Rider01.bmd"
BLOOD_CASTLE = "NPC/BloodCastle01.bmd"
RIDER_REAL_BONES = 21
ROOT = 0
CHILD = 1
LOCKED_ACTION = 2
LOCKED_KEYS = 7
CLOSE = 1e-5
FLOAT32 = struct.Struct("<f")


@pytest.fixture
def rider(reference_data: Path) -> JsonObject:
    return view_of(reference_data, RIDER)


def rig_of(view: JsonObject, *, meshes: bool = False) -> Rig:
    skinned = [SkinnedMesh(f"mesh {index}", mesh, None) for index, mesh in enumerate(model_scene(view).meshes)]
    actions = at(view, "/actions")
    return model_rig(view, skinned if meshes else [], [ChosenAction(index, "Stop") for index in range(len(actions))])


def stored(view: JsonObject, pointer: str) -> tuple[float, ...]:
    return tuple(FLOAT32.unpack(FLOAT32.pack(number))[0] for number in at(view, pointer))


def global_of(rig: Rig, node: int) -> Affine:
    found = rig.nodes[node]
    local = found.local
    return local if found.parent is None else global_of(rig, found.parent) @ local


def test_a_joint_is_named_after_its_bone(rider: JsonObject) -> None:
    assert joint_names(at(rider, "/bones"))[CHILD] == "Bip01 Footsteps"


def test_a_name_that_repeats_gets_the_bone_number(reference_data: Path) -> None:
    names = joint_names(at(view_of(reference_data, BLOOD_CASTLE), "/bones"))

    assert [name for name in names.values() if name.startswith("Box02")] == ["Box02 (40)", "Box02 (43)", "Box02 (46)"]


def test_a_dummy_bone_has_no_joint(rider: JsonObject) -> None:
    rig = rig_of(rider)

    assert len(rig.joints) == len(rig.nodes) == RIDER_REAL_BONES


def test_a_root_joint_turns_mu_s_z_up_into_gltf_s_y_up(rider: JsonObject) -> None:
    x, y, z = stored(rider, f"/bones/{ROOT}/actions/0/positions/0")

    assert rig_of(rider).nodes[ROOT].translation == pytest.approx((x, z, -y), abs=CLOSE)


def test_a_child_joint_keeps_mu_s_own_numbers(rider: JsonObject) -> None:
    node = rig_of(rider).nodes[CHILD]

    assert node.translation == stored(rider, f"/bones/{CHILD}/actions/0/positions/0")
    assert node.rotation == angle_quaternion(*stored(rider, f"/bones/{CHILD}/actions/0/rotations/0"))


def test_the_inverse_bind_matrices_undo_the_joints_at_rest(rider: JsonObject) -> None:
    rig = rig_of(rider)

    for joint, bind in zip(rig.joints, rig.inverse_binds, strict=True):
        undone = Affine.from_columns(bind) @ global_of(rig, joint)
        assert undone.applied((1.0, 2.0, 3.0)) == pytest.approx((1.0, 2.0, 3.0), abs=CLOSE)


def test_the_meshes_stand_where_the_studio_frames_them(rider: JsonObject) -> None:
    bounds = rest_bounds(rider)
    positions = [number for mesh in rig_of(rider, meshes=True).meshes for number in mesh.positions]
    xs, ys, zs = positions[0::3], positions[1::3], positions[2::3]

    assert bounds is not None
    assert (min(xs), -max(zs), min(ys)) == pytest.approx(bounds.low, abs=0.01)
    assert (max(xs), -min(zs), max(ys)) == pytest.approx(bounds.high, abs=0.01)


def test_a_mesh_without_corners_is_left_out(rider: JsonObject) -> None:
    empty = SkinnedMesh("empty", SceneMesh((), (), (), (), 0), None)

    assert model_rig(rider, [empty], []).meshes == ()


def test_a_mesh_carries_its_texture_as_an_image(rider: JsonObject) -> None:
    mesh = model_scene(rider).meshes[0]
    image = RigImage(b"\xff\xd8", JPEG)

    rig = model_rig(rider, [SkinnedMesh("textured", mesh, image)], [])

    assert (rig.images, rig.meshes[0].image) == ((image,), 0)


def test_an_animation_is_named_after_its_number_and_name(rider: JsonObject) -> None:
    rig = model_rig(rider, [], [ChosenAction(LOCKED_ACTION, "Attack 1")])

    assert [animation.name for animation in rig.animations] == ["002 Attack 1"]


def test_an_animation_holds_one_key_a_frame(rider: JsonObject) -> None:
    channel = rig_of(rider).animations[LOCKED_ACTION].channels[0]

    assert channel.times == tuple(key / FRAMES_PER_SECOND for key in range(LOCKED_KEYS))


def test_an_animation_holds_the_stored_pose(rider: JsonObject) -> None:
    channels = rig_of(rider).animations[LOCKED_ACTION].channels
    (place, turn) = [channel for channel in channels if channel.node == CHILD]
    key = 3

    assert place.values[key * 3 : key * 3 + 3] == stored(rider, f"/bones/{CHILD}/actions/2/positions/{key}")
    expected = angle_quaternion(*stored(rider, f"/bones/{CHILD}/actions/2/rotations/{key}"))
    found = turn.values[key * 4 : key * 4 + 4]
    assert abs(sum(a * b for a, b in zip(found, expected, strict=True))) == pytest.approx(1.0)


def test_turns_keep_one_sign_from_key_to_key(rider: JsonObject) -> None:
    for channel in rig_of(rider).animations[LOCKED_ACTION].channels:
        if channel.path == "rotation":
            turns = [channel.values[start : start + 4] for start in range(0, len(channel.values), 4)]
            assert all(sum(a * b for a, b in zip(x, y, strict=True)) >= 0 for x, y in pairwise(turns))
