import math
from collections.abc import Callable
from dataclasses import replace
from pathlib import Path

import pytest

from mutools.app.gltf_animation import (
    SAME_PLACE,
    SAME_PLACE_SHARE,
    animation_action,
    chosen_animation,
    key_times,
    place_tolerance,
    sampled,
)
from mutools.app.gltf_rig import (
    FRAMES_PER_SECOND,
    ChosenAction,
    Rig,
    RigAnimation,
    RigChannel,
    RigNode,
    model_rig,
)
from mutools.app.model_scene import angle_quaternion
from mutools.core.affine import Affine, Quaternion, both_turns
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.action_file import read_action_file, with_action
from mutools.core.json_types import JsonObject
from tests.models import at, view_of

RIDER = "skill/Rider01.bmd"
RIDER_ACTIONS = 4
FREE_ACTION = 0
LOCKED_ACTION = 2
ROOT = 0
CHILD = 1
BONE = 5
KEY = 3
ROOT_HALF = math.sqrt(0.5)
QUARTER_ABOUT_Z: Quaternion = (0.0, 0.0, ROOT_HALF, ROOT_HALF)
EIGHTH_ABOUT_Z: Quaternion = (0.0, 0.0, math.sin(math.pi / 8), math.cos(math.pi / 8))
CLOSE = 1e-5
FRAME = 1 / FRAMES_PER_SECOND


@pytest.fixture
def rider(reference_data: Path) -> JsonObject:
    return view_of(reference_data, RIDER)


@pytest.fixture
def rig(rider: JsonObject) -> Rig:
    return model_rig(rider, [], [ChosenAction(index, "Stop") for index in range(RIDER_ACTIONS)])


def refused(call: Callable[..., object], *args: object) -> MutoolsError:
    with pytest.raises(MutoolsError) as caught:
        call(*args)
    return caught.value


def channel(path: str, values: tuple[float, ...], interpolation: str = "LINEAR") -> RigChannel:
    return RigChannel(0, path, (0.0, 1.0), values, interpolation)  # type: ignore[arg-type]


def imported(rig: Rig, view: JsonObject, animation: int, at_action: int | None = None) -> JsonObject:
    return with_action(view, read_action_file(animation_action(rig, animation, view, at_action)), at_action)


def with_channels(rig: Rig, animation: int, change: Callable[[RigChannel], RigChannel | None]) -> Rig:
    found = rig.animations[animation]
    channels = tuple(changed for channel in found.channels if (changed := change(channel)) is not None)
    animations = list(rig.animations)
    animations[animation] = RigAnimation(found.name, channels)
    return replace(rig, animations=tuple(animations))


def turn_at(view: JsonObject, pointer: str) -> Quaternion:
    return angle_quaternion(*at(view, pointer))


def same_turn(first: Quaternion, second: Quaternion) -> bool:
    return abs(sum(a * b for a, b in zip(first, second, strict=True))) == pytest.approx(1.0, abs=1e-9)


def test_a_step_keeps_the_earlier_key() -> None:
    assert sampled(channel("translation", (0.0, 0.0, 0.0, 4.0, 4.0, 4.0), "STEP"), 0.5, 0.0) == (0.0, 0.0, 0.0)


def test_a_line_runs_between_the_keys() -> None:
    assert sampled(channel("translation", (0.0, 0.0, 0.0, 4.0, 8.0, 2.0)), 0.25, 0.0) == (1.0, 2.0, 0.5)


def test_a_turn_runs_along_the_sphere() -> None:
    found = sampled(channel("rotation", (0.0, 0.0, 0.0, 1.0, *QUARTER_ABOUT_Z)), 0.5, 0.0)

    assert found == pytest.approx(EIGHTH_ABOUT_Z)


def test_a_cubic_spline_with_flat_tangents_is_halfway_at_the_middle() -> None:
    values = (0.0, 0.0, 0.0, 2.0, 4.0, 6.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 4.0, 8.0, 10.0, 0.0, 0.0, 0.0)

    assert sampled(channel("translation", values, "CUBICSPLINE"), 0.5, 0.0) == pytest.approx((3.0, 6.0, 8.0))


def test_a_time_near_a_key_is_that_key() -> None:
    assert sampled(channel("translation", (0.0, 0.0, 0.0, 4.0, 4.0, 4.0), "STEP"), 0.9999, 0.001) == (4.0, 4.0, 4.0)


def test_a_time_past_the_last_key_is_the_last_key() -> None:
    assert sampled(channel("translation", (0.0, 0.0, 0.0, 4.0, 4.0, 4.0)), 3.0, 0.0) == (4.0, 4.0, 4.0)


def test_keys_come_one_per_smallest_step() -> None:
    times, _ = key_times([RigChannel(0, "rotation", (0.0, 1 / 60, 5 / 60), (0.0,) * 12)])

    assert times == pytest.approx(tuple(key / 60 for key in range(6)))


def test_a_frame_blender_left_out_is_still_a_key() -> None:
    times, _ = key_times([RigChannel(0, "rotation", (0.0, 2 * FRAME), (0.0,) * 8)])

    assert times == pytest.approx((0.0, FRAME, 2 * FRAME))


def test_one_time_is_one_key() -> None:
    assert key_times([RigChannel(0, "rotation", (0.5,), (0.0, 0.0, 0.0, 1.0))])[0] == (0.5,)


def test_more_keys_than_an_action_holds_are_refused() -> None:
    error = refused(key_times, [RigChannel(0, "rotation", (0.0, 0.0001, 10.0), (0.0,) * 12)])

    assert error.code is ErrorCode.INVALID_VALUE


def test_the_only_animation_is_taken_without_asking(rig: Rig) -> None:
    assert chosen_animation(replace(rig, animations=rig.animations[:1]), None, "rider.glb") == 0


def test_an_animation_is_found_by_its_name(rig: Rig) -> None:
    assert chosen_animation(rig, "002 Stop", "rider.glb") == LOCKED_ACTION


def test_an_animation_is_found_by_its_number(rig: Rig) -> None:
    assert chosen_animation(rig, "3", "rider.glb") == 3


def test_several_animations_need_one_named(rig: Rig) -> None:
    error = refused(chosen_animation, rig, None, "rider.glb")

    assert (error.code, error.suggestion) == (ErrorCode.USAGE, "--animation 0")


def test_an_unknown_animation_is_refused(rig: Rig) -> None:
    assert refused(chosen_animation, rig, "Dance", "rider.glb").code is ErrorCode.USAGE


def test_a_file_without_animations_is_refused(rig: Rig) -> None:
    assert refused(chosen_animation, replace(rig, animations=()), None, "rider.glb").code is ErrorCode.INVALID_VALUE


@pytest.mark.parametrize("action", range(RIDER_ACTIONS))
def test_an_action_imported_over_itself_leaves_the_model_as_it_was(rig: Rig, rider: JsonObject, action: int) -> None:
    assert imported(rig, rider, action, action) == rider


def test_an_added_action_holds_the_animation_s_pose(rig: Rig, rider: JsonObject) -> None:
    added = imported(rig, rider, FREE_ACTION)

    pointer = f"/bones/{BONE}/actions/{{}}/rotations/{KEY}"
    assert same_turn(turn_at(added, pointer.format(RIDER_ACTIONS)), turn_at(rider, pointer.format(FREE_ACTION)))
    assert at(added, f"/actions/{RIDER_ACTIONS}/lock") == 0


def test_a_root_bone_comes_back_through_the_nodes_above_it(rig: Rig, rider: JsonObject) -> None:
    above = Affine.of((5.0, 6.0, 7.0), QUARTER_ABOUT_Z)
    undone = above.inverse()

    def under(channel: RigChannel) -> RigChannel:
        if channel.node != ROOT:
            return channel
        width = 3 if channel.path == "translation" else 4
        keys = [channel.values[start : start + width] for start in range(0, len(channel.values), width)]
        if channel.path == "translation":
            moved = [number for key in keys for number in undone.applied((key[0], key[1], key[2]))]
        else:
            moved = [number for key in keys for number in both_turns(undone.turn(), (key[0], key[1], key[2], key[3]))]
        return replace(channel, values=tuple(moved))

    nodes = [replace(node, parent=len(rig.nodes)) if node.parent is None else node for node in rig.nodes]
    nodes.append(RigNode("Armature", None, above.place, above.turn()))
    moved_rig = with_channels(replace(rig, nodes=tuple(nodes)), FREE_ACTION, under)

    assert imported(moved_rig, rider, FREE_ACTION, FREE_ACTION) == rider


def test_a_bone_without_channels_keeps_its_node_s_pose(rig: Rig, rider: JsonObject) -> None:
    still = with_channels(rig, FREE_ACTION, lambda channel: None if channel.node == BONE else channel)

    added = imported(still, rider, FREE_ACTION)

    rest = rig.nodes[BONE].rotation
    for key in range(at(rider, f"/actions/{FREE_ACTION}/keys")):
        assert same_turn(turn_at(added, f"/bones/{BONE}/actions/{RIDER_ACTIONS}/rotations/{key}"), rest)


def test_a_changed_key_takes_new_numbers_and_the_others_keep_theirs(rig: Rig, rider: JsonObject) -> None:
    def turned(channel: RigChannel) -> RigChannel:
        if (channel.node, channel.path) != (BONE, "rotation"):
            return channel
        values = list(channel.values)
        values[KEY * 4 : KEY * 4 + 4] = both_turns(QUARTER_ABOUT_Z, tuple(values[KEY * 4 : KEY * 4 + 4]))  # type: ignore[arg-type]
        return replace(channel, values=tuple(values))

    edited = imported(with_channels(rig, FREE_ACTION, turned), rider, FREE_ACTION, FREE_ACTION)

    rotations = f"/bones/{BONE}/actions/{FREE_ACTION}/rotations"
    assert at(edited, f"{rotations}/{KEY}") != at(rider, f"{rotations}/{KEY}")
    assert [key for key, turn in enumerate(at(edited, rotations)) if turn != at(rider, f"{rotations}/{key}")] == [KEY]


def test_a_model_of_the_usual_size_keeps_a_place_within_a_hundredth(rider: JsonObject) -> None:
    assert place_tolerance(at(rider, "/bones")) == SAME_PLACE


def test_a_big_model_keeps_a_place_within_a_share_of_its_largest_place() -> None:
    far = 5000.0
    action: JsonObject = {"positions": [[0.0, -far, 1.0]], "rotations": [[0, 0, 0]]}
    bone: JsonObject = {"name": "Bip01", "parent": -1, "actions": [action]}

    assert place_tolerance([bone]) == pytest.approx(far * SAME_PLACE_SHARE)


def test_an_action_over_a_locked_one_takes_its_lock(rig: Rig, rider: JsonObject) -> None:
    edited = imported(rig, rider, LOCKED_ACTION, LOCKED_ACTION)

    assert at(edited, f"/actions/{LOCKED_ACTION}") == at(rider, f"/actions/{LOCKED_ACTION}")


def test_a_locked_action_with_other_keys_repeats_its_first_lock_position(rig: Rig, rider: JsonObject) -> None:
    edited = imported(rig, rider, FREE_ACTION, LOCKED_ACTION)

    first = at(rider, f"/actions/{LOCKED_ACTION}/positions/0")
    keys = at(rider, f"/actions/{FREE_ACTION}/keys")
    assert at(edited, f"/actions/{LOCKED_ACTION}") == {"keys": keys, "lock": 1, "positions": [first] * keys}


def test_a_file_without_a_bone_s_joint_is_for_another_skeleton(rig: Rig, rider: JsonObject) -> None:
    nodes = tuple(replace(node, name="Tail") if index == BONE else node for index, node in enumerate(rig.nodes))

    error = refused(animation_action, replace(rig, nodes=nodes), FREE_ACTION, rider)

    assert (error.code, error.pointer) == (ErrorCode.INVALID_VALUE, f"/bones/{BONE}")


def test_a_node_named_as_a_bone_gives_way_to_the_skin_s_joint(rig: Rig, rider: JsonObject) -> None:
    nodes = (*rig.nodes, RigNode(rig.nodes[BONE].name, None))

    assert imported(replace(rig, nodes=nodes), rider, FREE_ACTION, FREE_ACTION) == rider


def test_a_joint_under_another_parent_is_for_another_skeleton(rig: Rig, rider: JsonObject) -> None:
    nodes = tuple(replace(node, parent=ROOT) if index == BONE else node for index, node in enumerate(rig.nodes))

    error = refused(animation_action, replace(rig, nodes=nodes), FREE_ACTION, rider)

    assert error.code is ErrorCode.INVALID_VALUE
    assert "hangs from 'Bip01'" in error.message


def test_an_animation_that_moves_no_bone_is_refused(rig: Rig, rider: JsonObject) -> None:
    still = with_channels(rig, FREE_ACTION, lambda _: None)

    assert refused(animation_action, still, FREE_ACTION, rider).code is ErrorCode.INVALID_VALUE
