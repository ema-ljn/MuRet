from collections.abc import Callable
from pathlib import Path

import pytest

from mutools.app.model_scene import angle_quaternion
from mutools.app.pose_edits import copy_action, copy_key, move_bone, remove_key, turn_bone
from mutools.app.session import EditSession
from mutools.core.errors import ErrorCode, MutoolsError
from tests.app.conftest import TEXT, WORK
from tests.app.fake_store import FakeStore
from tests.models import at

RIDER = WORK / "skill" / "Rider01.bmd"
RIDER_ACTIONS = 4
LOCKED_ACTION = 2
LOCKED_KEYS = 7
FREE_ACTION = 0
BONE = 4


@pytest.fixture
def session(reference_data: Path, text_bytes: bytes) -> EditSession:
    rider = (reference_data / "skill" / "Rider01.bmd").read_bytes()
    return EditSession.open(FakeStore({RIDER: rider, TEXT: text_bytes}), RIDER)


def refused(call: Callable[..., object], *args: object) -> MutoolsError:
    with pytest.raises(MutoolsError) as caught:
        call(*args)
    return caught.value


def position(session: EditSession, bone: int, key: int, action: int = LOCKED_ACTION) -> list[float]:
    found = at(session.view, f"/bones/{bone}/actions/{action}/positions/{key}")
    assert isinstance(found, list)
    return found


def test_a_turn_writes_the_angles_of_its_quaternion(session: EditSession) -> None:
    assert turn_bone(session, LOCKED_ACTION, BONE, 3, angle_quaternion(0.5, -0.25, 1.0))

    assert at(session.view, f"/bones/{BONE}/actions/{LOCKED_ACTION}/rotations/3") == [0.5, -0.25, 1.0]


def test_a_turn_that_comes_back_the_same_changes_nothing(session: EditSession) -> None:
    turn_bone(session, LOCKED_ACTION, BONE, 3, angle_quaternion(0.5, -0.25, 1.0))
    session.save()

    assert not turn_bone(session, LOCKED_ACTION, BONE, 3, angle_quaternion(0.5, -0.25, 1.0))
    assert not session.dirty


def test_a_move_writes_the_position_it_is_given(session: EditSession) -> None:
    move_bone(session, FREE_ACTION, BONE, 2, (1.0, 2.0, 3.0))

    assert position(session, BONE, 2, FREE_ACTION) == [1.0, 2.0, 3.0]


def test_a_move_of_a_locked_first_bone_at_key_0_writes_where_it_is_shown(session: EditSession) -> None:
    move_bone(session, LOCKED_ACTION, 0, 0, (1.0, 2.0, 3.0))

    assert position(session, 0, 0) == [1.0, 2.0, 3.0]


def test_a_move_of_a_locked_first_bone_at_another_key_moves_its_own_place(session: EditSession) -> None:
    held, before = position(session, 0, 0), position(session, 0, 3)

    move_bone(session, LOCKED_ACTION, 0, 3, (held[0] + 1.0, held[1] - 2.0, 50.0))

    assert position(session, 0, 3) == pytest.approx([before[0] + 1.0, before[1] - 2.0, 50.0], abs=1e-4)
    assert position(session, 0, 0) == held


def test_every_edit_is_one_step_to_undo(session: EditSession) -> None:
    original = session.view
    turn_bone(session, LOCKED_ACTION, BONE, 3, angle_quaternion(0.5, -0.25, 1.0))
    copy_key(session, LOCKED_ACTION, 3)

    assert session.undo()
    assert at(session.view, f"/actions/{LOCKED_ACTION}/keys") == LOCKED_KEYS
    assert session.undo()
    assert session.view is original
    assert session.redo()
    assert session.redo()
    assert at(session.view, f"/actions/{LOCKED_ACTION}/keys") == LOCKED_KEYS + 1


def test_a_removed_key_leaves_the_session(session: EditSession) -> None:
    remove_key(session, LOCKED_ACTION, 0)

    assert at(session.view, f"/actions/{LOCKED_ACTION}/keys") == LOCKED_KEYS - 1


def test_a_copied_action_comes_after_the_last(session: EditSession) -> None:
    assert copy_action(session, LOCKED_ACTION) == RIDER_ACTIONS

    assert at(session.view, f"/actions/{RIDER_ACTIONS}") == at(session.view, f"/actions/{LOCKED_ACTION}")
    poses = at(session.view, f"/bones/{BONE}/actions")
    assert poses[RIDER_ACTIONS] == poses[LOCKED_ACTION]


def test_a_quaternion_of_three_numbers_is_refused(session: EditSession) -> None:
    error = refused(turn_bone, session, LOCKED_ACTION, BONE, 0, (0.0, 0.0, 1.0))

    assert error.code is ErrorCode.INVALID_VALUE
    assert not session.dirty


def test_a_table_has_no_bones_to_turn(reference_data: Path, text_bytes: bytes) -> None:
    table = EditSession.open(FakeStore({TEXT: text_bytes}), TEXT)

    assert refused(turn_bone, table, 0, 0, 0, (0.0, 0.0, 0.0, 1.0)).code is ErrorCode.USAGE
