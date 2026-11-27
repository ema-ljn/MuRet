from collections.abc import Callable, Sequence
from typing import Final

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.action_file import MAX_KEYS, checked_actions, is_dummy
from mutools.core.formats.record_fields import FLOAT32, float_bytes, float_view, invalid
from mutools.core.json_types import JsonObject, JsonValue

POSE_PARTS: Final = ("positions", "rotations")

type _PoseEdit = Callable[[list[JsonValue]], list[JsonValue]]


def _object(value: JsonValue) -> JsonObject:
    assert isinstance(value, dict)
    return value


def _list(value: JsonValue) -> list[JsonValue]:
    assert isinstance(value, list)
    return value


def _replaced(items: list[JsonValue], index: int, item: JsonValue) -> list[JsonValue]:
    return [*items[:index], item, *items[index + 1 :]]


def _stored(vector: Sequence[float], pointer: str) -> list[JsonValue]:
    return [float_view(float_bytes(number, f"{pointer}/{axis}")) for axis, number in enumerate(vector)]


def _real_bone(view: JsonObject, bone: int) -> JsonObject:
    bones = _list(view["bones"])
    if not 0 <= bone < len(bones):
        raise MutoolsError(ErrorCode.POINTER_NOT_FOUND, f"There is no bone {bone}", pointer=f"/bones/{bone}")
    found = _object(bones[bone])
    if is_dummy(found):
        raise invalid(f"Bone {bone} is a dummy bone, which has no pose", f"/bones/{bone}")
    return found


def _keys_of(view: JsonObject, action: int) -> int:
    keys = _object(checked_actions(view, action)[action])["keys"]
    assert isinstance(keys, int)
    return keys


def _check_key(view: JsonObject, action: int, key: int, pointer: str) -> int:
    keys = _keys_of(view, action)
    if not 0 <= key < keys:
        raise MutoolsError(
            ErrorCode.POINTER_NOT_FOUND, f"Action {action} has {keys} keys, 0 to {keys - 1}", pointer=pointer
        )
    return keys


def checked_pose(view: JsonObject, action: int, bone: int, key: int, part: str = "rotations") -> JsonObject:
    checked_actions(view, action)
    found = _real_bone(view, bone)
    _check_key(view, action, key, f"/bones/{bone}/actions/{action}/{part}/{key}")
    return found


def pose_vector(view: JsonObject, action: int, bone: int, key: int, part: str) -> tuple[float, ...]:
    pose = _object(_list(checked_pose(view, action, bone, key, part)["actions"])[action])
    pointer = f"/bones/{bone}/actions/{action}/{part}/{key}"
    vector = enumerate(_list(_list(pose[part])[key]))
    return tuple(FLOAT32.unpack(float_bytes(number, f"{pointer}/{axis}"))[0] for axis, number in vector)


def with_pose(
    view: JsonObject,
    action: int,
    bone: int,
    key: int,
    *,
    position: Sequence[float] | None = None,
    rotation: Sequence[float] | None = None,
) -> JsonObject:
    found = checked_pose(view, action, bone, key, "rotations" if rotation is not None else "positions")
    pointer = f"/bones/{bone}/actions/{action}"
    pose = dict(_object(_list(found["actions"])[action]))
    for part, vector in (("positions", position), ("rotations", rotation)):
        if vector is not None:
            pose[part] = _replaced(_list(pose[part]), key, _stored(vector, f"{pointer}/{part}/{key}"))
    changed = {**found, "actions": _replaced(_list(found["actions"]), action, pose)}
    return {**view, "bones": _replaced(_list(view["bones"]), bone, changed)}


def _each_pose(view: JsonObject, action: int, edit: _PoseEdit) -> list[JsonValue]:
    bones: list[JsonValue] = []
    for value in _list(view["bones"]):
        bone = _object(value)
        if is_dummy(bone):
            bones.append(bone)
            continue
        poses = _list(bone["actions"])
        pose = _object(poses[action])
        edited: JsonObject = {**pose, **{part: edit(_list(pose[part])) for part in POSE_PARTS}}
        bones.append({**bone, "actions": _replaced(poses, action, edited)})
    return bones


def _edited_keys(view: JsonObject, action: int, keys: int, edit: _PoseEdit) -> JsonObject:
    actions = _list(view["actions"])
    entry: JsonObject = {**_object(actions[action]), "keys": keys}
    if "positions" in entry:
        entry["positions"] = edit(_list(entry["positions"]))
    return {**view, "actions": _replaced(actions, action, entry), "bones": _each_pose(view, action, edit)}


def with_key_copied(view: JsonObject, action: int, key: int) -> JsonObject:
    keys = _check_key(view, action, key, f"/actions/{action}/keys")
    if keys >= MAX_KEYS:
        raise invalid(f"An action holds at most {MAX_KEYS:,} keys", f"/actions/{action}/keys")

    def copied(vectors: list[JsonValue]) -> list[JsonValue]:
        return [*vectors[: key + 1], list(_list(vectors[key])), *vectors[key + 1 :]]

    return _edited_keys(view, action, keys + 1, copied)


def without_key(view: JsonObject, action: int, key: int) -> JsonObject:
    keys = _check_key(view, action, key, f"/actions/{action}/keys")
    if keys == 1:
        raise invalid("An action keeps at least one key", f"/actions/{action}/keys")

    def removed(vectors: list[JsonValue]) -> list[JsonValue]:
        return [*vectors[:key], *vectors[key + 1 :]]

    return _edited_keys(view, action, keys - 1, removed)
