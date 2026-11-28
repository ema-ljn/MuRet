from collections.abc import Callable
from pathlib import Path
from typing import Any

import pytest

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.action_keys import pose_vector, with_key_copied, with_pose, without_key
from mutools.core.formats.model_view import model_from_view
from mutools.core.json_types import JsonObject
from tests.models import at, view_of

RIDER = "skill/Rider01.bmd"
LOCKED_ACTION = 2
LOCKED_KEYS = 7
BONE = 4
DUMMY_BONE = 10


@pytest.fixture(scope="module")
def rider(reference_data: Path) -> JsonObject:
    return view_of(reference_data, RIDER)


def refused(call: Callable[..., object], *args: object, **options: object) -> MutoolsError:
    with pytest.raises(MutoolsError) as caught:
        call(*args, **options)
    return caught.value


def pose(view: JsonObject, bone: int, action: int = LOCKED_ACTION) -> Any:
    return at(view, f"/bones/{bone}/actions/{action}")


def test_a_turn_changes_one_key_of_one_bone(rider: JsonObject) -> None:
    turned = with_pose(rider, LOCKED_ACTION, BONE, 3, rotation=(0.5, -0.25, 1.0))

    assert pose(turned, BONE)["rotations"][3] == [0.5, -0.25, 1.0]
    assert pose(turned, BONE)["positions"] == pose(rider, BONE)["positions"]
    assert pose(turned, BONE)["rotations"][2] == pose(rider, BONE)["rotations"][2]
    model_from_view(turned)


def test_a_move_changes_only_the_position(rider: JsonObject) -> None:
    moved = with_pose(rider, LOCKED_ACTION, BONE, 0, position=(1.0, 2.0, 3.0))

    assert pose(moved, BONE)["positions"][0] == [1.0, 2.0, 3.0]
    assert pose(moved, BONE)["rotations"] == pose(rider, BONE)["rotations"]


def test_a_pose_is_kept_as_the_file_stores_it(rider: JsonObject) -> None:
    turned = with_pose(rider, LOCKED_ACTION, BONE, 0, rotation=(0.123456789012, -2.0, 1 / 3))

    assert pose(turned, BONE)["rotations"][0] == [0.12345679, -2.0, 0.33333334]


def test_a_pose_shares_what_it_does_not_change(rider: JsonObject) -> None:
    turned = with_pose(rider, LOCKED_ACTION, BONE, 0, rotation=(0.1, 0.2, 0.3))

    assert at(turned, "/bones/3") is at(rider, "/bones/3")
    assert pose(turned, BONE, 0) is pose(rider, BONE, 0)
    assert pose(turned, BONE)["positions"] is pose(rider, BONE)["positions"]
    assert at(turned, "/meshes") is at(rider, "/meshes")


def test_the_view_it_was_given_stays_as_it_was(rider: JsonObject) -> None:
    before = pose(rider, BONE)["rotations"][0]

    with_pose(rider, LOCKED_ACTION, BONE, 0, rotation=(0.1, 0.2, 0.3))

    assert pose(rider, BONE)["rotations"][0] == before


@pytest.mark.parametrize(
    ("action", "bone", "key", "pointer"),
    [
        (4, BONE, 0, "/actions/4"),
        (LOCKED_ACTION, 24, 0, "/bones/24"),
        (LOCKED_ACTION, BONE, LOCKED_KEYS, f"/bones/{BONE}/actions/{LOCKED_ACTION}/rotations/{LOCKED_KEYS}"),
    ],
)
def test_a_pose_out_of_range_is_not_found(rider: JsonObject, action: int, bone: int, key: int, pointer: str) -> None:
    error = refused(with_pose, rider, action, bone, key, rotation=(0.0, 0.0, 0.0))

    assert (error.code, error.pointer) == (ErrorCode.POINTER_NOT_FOUND, pointer)


def test_a_dummy_bone_has_no_pose(rider: JsonObject) -> None:
    error = refused(with_pose, rider, LOCKED_ACTION, DUMMY_BONE, 0, rotation=(0.0, 0.0, 0.0))

    assert (error.code, error.pointer) == (ErrorCode.INVALID_VALUE, f"/bones/{DUMMY_BONE}")


def test_a_number_a_float32_cannot_hold_is_refused(rider: JsonObject) -> None:
    error = refused(with_pose, rider, LOCKED_ACTION, BONE, 0, position=(1e39, 0.0, 0.0))

    assert error.code is ErrorCode.INVALID_VALUE


def test_a_pose_vector_is_read_as_numbers(rider: JsonObject) -> None:
    stored = pose(rider, 0)["positions"][3]

    assert pose_vector(rider, LOCKED_ACTION, 0, 3, "positions") == pytest.approx(stored, abs=1e-5)


def test_a_copied_key_follows_the_one_it_copies_in_every_bone(rider: JsonObject) -> None:
    copied = with_key_copied(rider, LOCKED_ACTION, 3)

    assert at(copied, f"/actions/{LOCKED_ACTION}/keys") == LOCKED_KEYS + 1
    for part in ("positions", "rotations"):
        listed = pose(rider, BONE)[part]
        assert pose(copied, BONE)[part] == [*listed[:4], listed[3], *listed[4:]]
    model_from_view(copied)


def test_a_copied_key_copies_the_lock_position(rider: JsonObject) -> None:
    copied = with_key_copied(rider, LOCKED_ACTION, 0)

    locks = at(rider, f"/actions/{LOCKED_ACTION}/positions")
    assert at(copied, f"/actions/{LOCKED_ACTION}/positions") == [locks[0], *locks]


def test_a_copied_key_is_a_list_of_its_own(rider: JsonObject) -> None:
    copied = with_key_copied(rider, LOCKED_ACTION, 3)

    assert pose(copied, BONE)["rotations"][4] is not pose(copied, BONE)["rotations"][3]


def test_a_removed_key_leaves_every_bone(rider: JsonObject) -> None:
    removed = without_key(rider, LOCKED_ACTION, 0)

    assert at(removed, f"/actions/{LOCKED_ACTION}/keys") == LOCKED_KEYS - 1
    assert pose(removed, BONE)["rotations"] == pose(rider, BONE)["rotations"][1:]
    assert at(removed, f"/actions/{LOCKED_ACTION}/positions") == at(rider, f"/actions/{LOCKED_ACTION}/positions")[1:]
    model_from_view(removed)


def test_the_last_key_of_an_action_stays(reference_data: Path) -> None:
    sword = view_of(reference_data, "Item/Sword01.bmd")

    error = refused(without_key, sword, 0, 0)

    assert (error.code, error.pointer) == (ErrorCode.INVALID_VALUE, "/actions/0/keys")


def test_a_key_out_of_range_is_not_found(rider: JsonObject) -> None:
    assert refused(with_key_copied, rider, LOCKED_ACTION, LOCKED_KEYS).code is ErrorCode.POINTER_NOT_FOUND
