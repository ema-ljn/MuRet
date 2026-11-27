from collections.abc import Callable
from copy import deepcopy
from pathlib import Path

import pytest

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.action_file import (
    ACTION_FILE_FORMAT,
    action_file,
    read_action_file,
    with_action,
    without_action,
)
from mutools.core.formats.model_view import model_from_view
from mutools.core.json_types import JsonObject, JsonValue
from tests.models import at, put, view_of

RIDER = "skill/Rider01.bmd"
RIDER_ACTIONS = 4
LOCKED_ACTION = 2
DUMMY_BONE = 10
FIRST_REAL_BONE = 0
CHILD_BONE = 1


@pytest.fixture(scope="module")
def rider(reference_data: Path) -> JsonObject:
    return view_of(reference_data, RIDER)


def refused(call: Callable[..., object], *args: object) -> MutoolsError:
    with pytest.raises(MutoolsError) as caught:
        call(*args)
    return caught.value


def test_an_exported_action_holds_its_keys_and_every_bone_in_them(rider: JsonObject) -> None:
    exported = action_file(rider, 1)

    assert (exported["format"], exported["format_version"]) == (ACTION_FILE_FORMAT, 1)
    assert (exported["keys"], exported["lock"]) == (at(rider, "/actions/1/keys"), 0)
    assert "positions" not in exported
    assert at(exported, f"/bones/{CHILD_BONE}") == {
        "name": at(rider, f"/bones/{CHILD_BONE}/name/text"),
        "parent": at(rider, f"/bones/{CHILD_BONE}/parent"),
        **at(rider, f"/bones/{CHILD_BONE}/actions/1"),
    }


def test_a_locked_action_keeps_its_positions(rider: JsonObject) -> None:
    exported = action_file(rider, LOCKED_ACTION)

    assert exported["lock"] == at(rider, f"/actions/{LOCKED_ACTION}/lock") != 0
    assert exported["positions"] == at(rider, f"/actions/{LOCKED_ACTION}/positions")


def test_a_dummy_bone_stays_a_dummy(rider: JsonObject) -> None:
    assert at(action_file(rider, 0), f"/bones/{DUMMY_BONE}") == at(rider, f"/bones/{DUMMY_BONE}")


def test_an_action_out_of_range_is_not_found(rider: JsonObject) -> None:
    error = refused(action_file, rider, RIDER_ACTIONS)

    assert (error.code, error.pointer) == (ErrorCode.POINTER_NOT_FOUND, f"/actions/{RIDER_ACTIONS}")


def test_an_action_put_back_over_itself_changes_nothing(rider: JsonObject) -> None:
    assert with_action(rider, action_file(rider, LOCKED_ACTION), LOCKED_ACTION) == rider


def test_an_added_action_is_the_last_one_of_the_model_and_of_every_bone(rider: JsonObject) -> None:
    added = with_action(rider, action_file(rider, LOCKED_ACTION))

    assert at(added, "/actions")[-1] == at(rider, f"/actions/{LOCKED_ACTION}")
    assert len(at(added, "/actions")) == RIDER_ACTIONS + 1
    assert at(added, f"/bones/{CHILD_BONE}/actions/{RIDER_ACTIONS}") == at(
        rider, f"/bones/{CHILD_BONE}/actions/{LOCKED_ACTION}"
    )
    assert at(added, f"/bones/{DUMMY_BONE}") == at(rider, f"/bones/{DUMMY_BONE}")
    model_from_view(added)


def test_adding_leaves_the_model_view_it_was_given_as_it_was(rider: JsonObject) -> None:
    before = deepcopy(rider)

    with_action(rider, action_file(rider, 0))

    assert rider == before


def test_an_action_goes_over_another_with_the_same_keys(rider: JsonObject) -> None:
    replaced = with_action(rider, action_file(rider, 0), 1)

    assert at(replaced, f"/bones/{CHILD_BONE}/actions/1") == at(rider, f"/bones/{CHILD_BONE}/actions/0")
    assert len(at(replaced, "/actions")) == RIDER_ACTIONS


def test_a_place_out_of_range_is_not_found(rider: JsonObject) -> None:
    error = refused(with_action, rider, action_file(rider, 0), RIDER_ACTIONS)

    assert (error.code, error.pointer) == (ErrorCode.POINTER_NOT_FOUND, f"/actions/{RIDER_ACTIONS}")


def test_another_tail_on_a_name_is_the_same_bone(rider: JsonObject) -> None:
    other = deepcopy(rider)
    put(other, f"/bones/{CHILD_BONE}/name/tail", "abcd")

    assert len(at(with_action(other, action_file(rider, 0)), "/actions")) == RIDER_ACTIONS + 1


def test_another_skeleton_is_refused_at_its_bone_count(reference_data: Path, rider: JsonObject) -> None:
    error = refused(with_action, view_of(reference_data, "NPC/BloodCastle01.bmd"), action_file(rider, 0))

    assert (error.code, error.pointer) == (ErrorCode.INVALID_VALUE, "/bones")


@pytest.mark.parametrize(
    ("pointer", "value"),
    [
        (f"/bones/{CHILD_BONE}/name", "Bip01 Head"),
        (f"/bones/{CHILD_BONE}/parent", -1),
        (f"/bones/{DUMMY_BONE}", {"name": "Bip01", "parent": -1, "positions": [], "rotations": []}),
    ],
)
def test_a_bone_that_differs_is_refused_at_that_bone(rider: JsonObject, pointer: str, value: JsonValue) -> None:
    exported = action_file(rider, 0)
    put(exported, pointer, value)

    error = refused(with_action, rider, exported)

    assert error.code is ErrorCode.INVALID_VALUE
    assert error.pointer is not None
    assert pointer.startswith(error.pointer)
    assert "Bip01" in error.message


def test_an_action_file_reads_back_as_it_was_written(rider: JsonObject) -> None:
    exported = action_file(rider, LOCKED_ACTION)

    assert read_action_file(deepcopy(exported)) == exported


@pytest.mark.parametrize(
    ("pointer", "value"),
    [
        ("/format", "model"),
        ("/format_version", 2),
        ("/keys", -1),
        ("/lock", 256),
        ("/positions", [[0.0, 0.0, 0.0]]),
        (f"/bones/{CHILD_BONE}/rotations", [[0.0, 0.0, 0.0]]),
        (f"/bones/{CHILD_BONE}/positions/0", [0.0, 0.0]),
        (f"/bones/{CHILD_BONE}/positions/0/0", "a"),
        (f"/bones/{CHILD_BONE}/parent", 1 << 15),
        (f"/bones/{DUMMY_BONE}/dummy", 0),
    ],
)
def test_an_action_file_that_does_not_fit_is_refused_where_it_does_not(
    rider: JsonObject, pointer: str, value: JsonValue
) -> None:
    exported = action_file(rider, LOCKED_ACTION)
    put(exported, pointer, value)

    error = refused(read_action_file, exported)

    assert error.code is ErrorCode.INVALID_VALUE
    assert error.pointer is not None
    assert pointer.startswith(error.pointer)


def test_positions_without_a_lock_are_refused(rider: JsonObject) -> None:
    exported = action_file(rider, 0)
    exported["positions"] = []

    assert refused(read_action_file, exported).pointer == "/positions"


def test_something_else_than_an_object_is_not_an_action_file() -> None:
    assert refused(read_action_file, []).code is ErrorCode.INVALID_VALUE


def test_a_removed_action_leaves_the_model_and_every_bone(rider: JsonObject) -> None:
    removed = without_action(rider, 1)

    assert at(removed, "/actions") == [at(rider, f"/actions/{index}") for index in (0, 2, 3)]
    assert at(removed, f"/bones/{CHILD_BONE}/actions/1") == at(rider, f"/bones/{CHILD_BONE}/actions/2")
    assert at(removed, f"/bones/{DUMMY_BONE}") == at(rider, f"/bones/{DUMMY_BONE}")
    model_from_view(removed)


def test_removing_an_action_out_of_range_is_not_found(rider: JsonObject) -> None:
    assert refused(without_action, rider, RIDER_ACTIONS).code is ErrorCode.POINTER_NOT_FOUND
