from collections.abc import Sequence
from typing import Final

from mutools.app.model_scene import POSITION_WIDTH, quaternion_angles
from mutools.app.session import EditSession
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.action_file import action_file, with_action
from mutools.core.formats.action_keys import checked_pose, pose_vector, with_key_copied, with_pose, without_key
from mutools.core.formats.model_view import MODEL_FORMAT_NAME
from mutools.core.json_types import JsonObject, JsonValue

HELD_BONE: Final = 0
QUATERNION_WIDTH: Final = 4


def _object(value: JsonValue) -> JsonObject:
    assert isinstance(value, dict)
    return value


def _list(value: JsonValue) -> list[JsonValue]:
    assert isinstance(value, list)
    return value


def _model(session: EditSession) -> JsonObject:
    if session.format_name != MODEL_FORMAT_NAME:
        raise MutoolsError(
            ErrorCode.USAGE, f"{session.path.name} is a {session.format_name} table, and only models have actions"
        )
    return session.view


def _vector(values: Sequence[float], width: int, what: str) -> tuple[float, ...]:
    if len(values) != width:
        raise MutoolsError(ErrorCode.INVALID_VALUE, f"Expected {width} numbers for the {what}")
    return tuple(float(number) for number in values)


def _stored_position(view: JsonObject, action: int, bone: int, key: int, shown: tuple[float, ...]) -> tuple[float, ...]:
    """The client shows the first bone of a locked action at the x and y of key 0 (0x4eb860).

    A move of it at another key adds what it moved in x and y to that key's own place.
    """
    lock = _object(_list(view["actions"])[action])["lock"]
    if bone != HELD_BONE or key == 0 or not lock:
        return shown
    held = pose_vector(view, action, bone, 0, "positions")
    stored = pose_vector(view, action, bone, key, "positions")
    return stored[0] + shown[0] - held[0], stored[1] + shown[1] - held[1], shown[2]


def turn_bone(session: EditSession, action: int, bone: int, key: int, quaternion: Sequence[float]) -> bool:
    x, y, z, w = _vector(quaternion, QUATERNION_WIDTH, "quaternion")
    return session.change(with_pose(_model(session), action, bone, key, rotation=quaternion_angles(x, y, z, w)))


def move_bone(session: EditSession, action: int, bone: int, key: int, position: Sequence[float]) -> bool:
    view = _model(session)
    shown = _vector(position, POSITION_WIDTH, "position")
    checked_pose(view, action, bone, key, "positions")
    stored = _stored_position(view, action, bone, key, shown)
    return session.change(with_pose(view, action, bone, key, position=stored))


def copy_key(session: EditSession, action: int, key: int) -> None:
    session.change(with_key_copied(_model(session), action, key))


def remove_key(session: EditSession, action: int, key: int) -> None:
    session.change(without_key(_model(session), action, key))


def copy_action(session: EditSession, action: int) -> int:
    view = _model(session)
    copied = with_action(view, action_file(view, action))
    session.change(copied)
    return len(_list(copied["actions"])) - 1
