from copy import deepcopy
from typing import Final

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.record_fields import float_bytes, invalid
from mutools.core.json_types import JsonObject, JsonValue
from mutools.core.leaves import HEX_KEYS, leaf_text

ACTION_FILE_FORMAT: Final = "mutools-action"
ACTION_FILE_VERSION: Final = 1
MAX_KEYS: Final = (1 << 15) - 1
INT16_MIN: Final = -(1 << 15)
INT16_MAX: Final = (1 << 15) - 1
BYTE_MAX: Final = 0xFF
VECTOR_WIDTH: Final = 3
ROOT_MEMBERS: Final = frozenset({"format", "format_version", "keys", "lock", "bones"})
BONE_MEMBERS: Final = frozenset({"name", "parent", "positions", "rotations"})
DUMMY_MEMBERS: Final = frozenset({"dummy"})
ACTION_MEMBERS: Final = ("keys", "lock", "positions")
POSE_MEMBERS: Final = ("positions", "rotations")
ROOT_PARENT: Final = -1


def _object(value: JsonValue) -> JsonObject:
    assert isinstance(value, dict)
    return value


def _list(value: JsonValue) -> list[JsonValue]:
    assert isinstance(value, list)
    return value


def _not_found(count: int, index: int, suggestion: str) -> MutoolsError:
    numbers = f"the model's actions are 0 to {count - 1}" if count else "the model has no actions"
    return MutoolsError(
        ErrorCode.POINTER_NOT_FOUND,
        f"There is no action {index}: {numbers}",
        suggestion=suggestion,
        pointer=f"/actions/{index}",
    )


def checked_actions(view: JsonObject, index: int, suggestion: str = "mutools info <file>") -> list[JsonValue]:
    actions = _list(view["actions"])
    if not 0 <= index < len(actions):
        raise _not_found(len(actions), index, suggestion)
    return actions


def is_dummy(bone: JsonObject) -> bool:
    return bone.keys() == DUMMY_MEMBERS


def _name_text(name: JsonValue) -> JsonValue:
    text = leaf_text(name)
    return name if text is None else text


def _exported_bone(bone: JsonObject, index: int) -> JsonObject:
    if is_dummy(bone):
        return dict(bone)
    pose = _object(_list(bone["actions"])[index])
    return {"name": _name_text(bone["name"]), "parent": bone["parent"], **deepcopy(pose)}


def action_file(view: JsonObject, index: int) -> JsonObject:
    action = _object(checked_actions(view, index)[index])
    return {
        "format": ACTION_FILE_FORMAT,
        "format_version": ACTION_FILE_VERSION,
        **deepcopy(action),
        "bones": [_exported_bone(_object(bone), index) for bone in _list(view["bones"])],
    }


def _members(
    value: JsonValue, pointer: str, required: frozenset[str], optional: frozenset[str] = frozenset()
) -> JsonObject:
    if isinstance(value, dict) and required <= value.keys() <= required | optional:
        return value
    raise invalid(f"Expected an object with exactly these members: {', '.join(sorted(required))}", pointer)


def _integer(value: JsonValue, lowest: int, highest: int, pointer: str) -> int:
    if type(value) is not int or not lowest <= value <= highest:
        raise invalid(f"Expected an integer from {lowest} to {highest}", pointer)
    return value


def _check_vectors(value: JsonValue, count: int, pointer: str) -> None:
    if not isinstance(value, list) or len(value) != count:
        raise invalid(f"Expected a list of {count:,} vectors, one per key", pointer)
    for index, vector in enumerate(value):
        if not isinstance(vector, list) or len(vector) != VECTOR_WIDTH:
            raise invalid(f"Expected a list of {VECTOR_WIDTH} numbers", f"{pointer}/{index}")
        for axis, number in enumerate(vector):
            float_bytes(number, f"{pointer}/{index}/{axis}")


def _check_name(value: JsonValue, pointer: str) -> None:
    if not isinstance(value, str) and not (isinstance(value, dict) and value.keys() == HEX_KEYS):
        raise invalid("Expected the bone's name as a string", pointer)


def _check_bone(value: JsonValue, keys: int, pointer: str) -> None:
    if isinstance(value, dict) and is_dummy(value):
        _integer(value["dummy"], 1, BYTE_MAX, f"{pointer}/dummy")
        return
    bone = _members(value, pointer, BONE_MEMBERS)
    _check_name(bone["name"], f"{pointer}/name")
    _integer(bone["parent"], INT16_MIN, INT16_MAX, f"{pointer}/parent")
    for member in POSE_MEMBERS:
        _check_vectors(bone[member], keys, f"{pointer}/{member}")


def read_action_file(value: JsonValue) -> JsonObject:
    root = _members(value, "", ROOT_MEMBERS, frozenset({"positions"}))
    if root["format"] != ACTION_FILE_FORMAT:
        raise invalid(f'Not an action file: its format is not "{ACTION_FILE_FORMAT}"', "/format")
    if type(root["format_version"]) is not int or root["format_version"] != ACTION_FILE_VERSION:
        raise invalid(f"This tool reads format_version {ACTION_FILE_VERSION} action files", "/format_version")
    keys = _integer(root["keys"], 0, MAX_KEYS, "/keys")
    if _integer(root["lock"], 0, BYTE_MAX, "/lock"):
        if "positions" not in root:
            raise invalid("An action whose lock is not 0 has positions", "")
        _check_vectors(root["positions"], keys, "/positions")
    elif "positions" in root:
        raise invalid("An action has positions only when its lock is not 0", "/positions")
    bones = root["bones"]
    if not isinstance(bones, list):
        raise invalid("Expected a list of bones", "/bones")
    for index, bone in enumerate(bones):
        _check_bone(bone, keys, f"/bones/{index}")
    return root


def _bone_label(bone: JsonObject) -> str:
    if is_dummy(bone):
        return "a dummy bone"
    name = _name_text(bone["name"])
    parent = bone["parent"]
    return f"{name!r}, a root" if parent == ROOT_PARENT else f"{name!r} under bone {parent}"


def _same_bone(model_bone: JsonObject, file_bone: JsonObject) -> bool:
    if is_dummy(model_bone) or is_dummy(file_bone):
        return is_dummy(model_bone) and is_dummy(file_bone)
    return _name_text(model_bone["name"]) == file_bone["name"] and model_bone["parent"] == file_bone["parent"]


def _check_skeleton(model_bones: list[JsonValue], file_bones: list[JsonValue]) -> None:
    if len(model_bones) != len(file_bones):
        raise invalid(
            f"The action file has {len(file_bones):,} bones and the model {len(model_bones):,}: "
            "the action is for another skeleton",
            "/bones",
        )
    pairs = zip(map(_object, model_bones), map(_object, file_bones), strict=True)
    for index, (model_bone, file_bone) in enumerate(pairs):
        if not _same_bone(model_bone, file_bone):
            raise invalid(
                f"Bone {index} is {_bone_label(file_bone)} in the action file and {_bone_label(model_bone)} in the "
                "model: the action is for another skeleton",
                f"/bones/{index}",
            )


def _placed(items: list[JsonValue], item: JsonValue, at: int | None) -> list[JsonValue]:
    return [*items, item] if at is None else [*items[:at], item, *items[at + 1 :]]


def _with_pose(model_bone: JsonObject, file_bone: JsonObject, at: int | None) -> JsonObject:
    if is_dummy(model_bone):
        return model_bone
    pose: JsonObject = {member: file_bone[member] for member in POSE_MEMBERS}
    return {**model_bone, "actions": _placed(_list(model_bone["actions"]), pose, at)}


def with_action(view: JsonObject, action: JsonObject, at: int | None = None) -> JsonObject:
    actions = _list(view["actions"])
    if at is not None:
        checked_actions(view, at, "leave --at out to add the action after the last one")
    model_bones, file_bones = _list(view["bones"]), _list(action["bones"])
    _check_skeleton(model_bones, file_bones)
    entry: JsonObject = {member: action[member] for member in ACTION_MEMBERS if member in action}
    bones: list[JsonValue] = [
        _with_pose(_object(model_bone), _object(file_bone), at)
        for model_bone, file_bone in zip(model_bones, file_bones, strict=True)
    ]
    return {**view, "actions": _placed(actions, entry, at), "bones": bones}


def _without_pose(bone: JsonObject, index: int) -> JsonObject:
    if is_dummy(bone):
        return bone
    poses = _list(bone["actions"])
    return {**bone, "actions": [*poses[:index], *poses[index + 1 :]]}


def without_action(view: JsonObject, index: int) -> JsonObject:
    actions = checked_actions(view, index)
    bones: list[JsonValue] = [_without_pose(_object(bone), index) for bone in _list(view["bones"])]
    return {**view, "actions": [*actions[:index], *actions[index + 1 :]], "bones": bones}
