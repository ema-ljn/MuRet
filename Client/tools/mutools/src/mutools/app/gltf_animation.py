import math
from bisect import bisect_left
from collections.abc import Sequence
from dataclasses import dataclass
from functools import reduce
from itertools import pairwise
from operator import matmul
from typing import Final

from mutools.app.gltf_rig import (
    FRAMES_PER_SECOND,
    ROTATION,
    SCALE,
    TRANSLATION,
    TURN_UP,
    Rig,
    RigAnimation,
    RigChannel,
    is_real,
    joint_names,
    stored_vector,
)
from mutools.app.model_parts import NO_PARENT, tree_parents
from mutools.app.model_scene import angle_quaternion, quaternion_angles
from mutools.core.affine import Affine, Quaternion, Vector, normalized, vector
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.action_file import ACTION_FILE_FORMAT, ACTION_FILE_VERSION, MAX_KEYS, name_text
from mutools.core.formats.record_fields import float_bytes, float_view, invalid
from mutools.core.json_types import JsonObject, JsonValue

SAME_PLACE: Final = 1e-2
SAME_PLACE_SHARE: Final = 2e-5
SAME_TURN: Final = 1e-3
NEAR_KEY: Final = 1e-3
NEARLY_SAME_TURN: Final = 1 - 1e-9
CUBIC: Final = "CUBICSPLINE"
STEP: Final = "STEP"
CUBIC_PARTS: Final = 3
IN_TANGENT, VALUE, OUT_TANGENT = range(CUBIC_PARTS)
PATHS: Final = frozenset({TRANSLATION, ROTATION, SCALE})
UNDO_TURN_UP: Final = TURN_UP.inverse()

type _Pose = tuple[Vector, Quaternion]


@dataclass(frozen=True, slots=True)
class _Bone:
    index: int
    chain: tuple[int, ...]
    root: bool


def _value(channel: RigChannel, key: int, part: int = VALUE) -> tuple[float, ...]:
    cubic = channel.interpolation == CUBIC
    width = len(channel.values) // (len(channel.times) * (CUBIC_PARTS if cubic else 1))
    start = (key * CUBIC_PARTS + part) * width if cubic else key * width
    return channel.values[start : start + width]


def _slerp(first: Sequence[float], second: Sequence[float], share: float) -> Quaternion:
    dot = sum(a * b for a, b in zip(first, second, strict=True))
    if dot < 0:
        second, dot = [-number for number in second], -dot
    if dot > NEARLY_SAME_TURN:
        return normalized(a + (b - a) * share for a, b in zip(first, second, strict=True))
    angle = math.acos(dot)
    near, far = math.sin((1 - share) * angle), math.sin(share * angle)
    return normalized((a * near + b * far) / math.sin(angle) for a, b in zip(first, second, strict=True))


def _hermite(channel: RigChannel, before: int, after: int, share: float, span: float) -> tuple[float, ...]:
    squared, cubed = share * share, share * share * share
    weights = (2 * cubed - 3 * squared + 1, cubed - 2 * squared + share, -2 * cubed + 3 * squared, cubed - squared)
    parts = (
        _value(channel, before),
        tuple(number * span for number in _value(channel, before, OUT_TANGENT)),
        _value(channel, after),
        tuple(number * span for number in _value(channel, after, IN_TANGENT)),
    )
    return tuple(
        sum(weight * part[axis] for weight, part in zip(weights, parts, strict=True)) for axis in range(len(parts[0]))
    )


def _key_at(times: Sequence[float], time: float, near: float) -> int | None:
    after = bisect_left(times, time - near)
    if after < len(times) and times[after] <= time + near:
        return after
    if after in (0, len(times)):
        return min(after, len(times) - 1)
    return None


def sampled(channel: RigChannel, time: float, near: float) -> tuple[float, ...]:
    """A channel's value at a time, as glTF interpolates it; a time within near of a key, or past the ends, is a key."""
    times = channel.times
    key = _key_at(times, time, near)
    if key is not None:
        return _value(channel, key)
    after = bisect_left(times, time)
    before = after - 1
    if channel.interpolation == STEP:
        return _value(channel, before)
    span = times[after] - times[before]
    share = (time - times[before]) / span
    if channel.interpolation == CUBIC:
        value = _hermite(channel, before, after, share, span)
        return normalized(value) if channel.path == ROTATION else value
    if channel.path == ROTATION:
        return _slerp(_value(channel, before), _value(channel, after), share)
    return tuple(a + (b - a) * share for a, b in zip(_value(channel, before), _value(channel, after), strict=True))


def chosen_animation(rig: Rig, wanted: str | None, source: str) -> int:
    names = [animation.name for animation in rig.animations]
    if not names:
        raise invalid(f"{source} has no animations", "/animations")
    if wanted is None:
        if len(names) == 1:
            return 0
        raise MutoolsError(
            ErrorCode.USAGE,
            f"{source} has {len(names)} animations, from {names[0]!r} to {names[-1]!r}: choose one with --animation, "
            "by its name or its number from 0",
            suggestion="--animation 0",
        )
    if wanted in names:
        return names.index(wanted)
    if wanted.isdecimal() and int(wanted) < len(names):
        return int(wanted)
    raise MutoolsError(
        ErrorCode.USAGE,
        f"{source} has no animation named {wanted!r}, and its animations are numbered 0 to {len(names) - 1}",
        suggestion="--animation 0",
    )


def _joint_nodes(rig: Rig, names: dict[int, str]) -> dict[int, int]:
    nodes_named: dict[str, list[int]] = {}
    for index, node in enumerate(rig.nodes):
        nodes_named.setdefault(node.name, []).append(index)
    joints = set(rig.joints)
    found: dict[int, int] = {}
    for bone, name in names.items():
        nodes = nodes_named.get(name, [])
        if len(nodes) > 1:
            nodes = [node for node in nodes if node in joints] or nodes
        if len(nodes) != 1:
            count = f"no joint named {name!r}" if not nodes else f"{len(nodes)} nodes named {name!r}"
            raise invalid(
                f"The file has {count}, as bone {bone} of the model is: it is for another skeleton", f"/bones/{bone}"
            )
        found[bone] = nodes[0]
    return found


def _chain(rig: Rig, node: int, joints: set[int]) -> tuple[tuple[int, ...], int | None]:
    chain = [node]
    above = rig.nodes[node].parent
    while above is not None and above not in joints:
        chain.append(above)
        if len(chain) > len(rig.nodes):
            raise MutoolsError(ErrorCode.CORRUPT_FILE, "The file's nodes hang from each other in a circle")
        above = rig.nodes[above].parent
    return tuple(reversed(chain)), above


def _bones(rig: Rig, bones: Sequence[JsonValue]) -> list[_Bone]:
    names = joint_names(bones)
    parents = tree_parents(bones)
    node_of = _joint_nodes(rig, names)
    joints = set(node_of.values())
    found = []
    for bone, node in node_of.items():
        chain, above = _chain(rig, node, joints)
        parent = parents[bone]
        expected = None if parent == NO_PARENT else node_of[parent]
        if above != expected:
            hangs = "from no joint" if above is None else f"from {rig.nodes[above].name!r}"
            model = "is a root" if parent == NO_PARENT else f"hangs from {names[parent]!r}"
            raise invalid(
                f"Joint {names[bone]!r} hangs {hangs} in the file, and in the model bone {bone} {model}: the file is "
                "for another skeleton",
                f"/bones/{bone}",
            )
        found.append(_Bone(bone, chain, parent == NO_PARENT))
    return found


def _moving(animation: RigAnimation, bones: Sequence[_Bone]) -> dict[int, dict[str, RigChannel]]:
    used = {node for bone in bones for node in bone.chain}
    channels: dict[int, dict[str, RigChannel]] = {}
    for channel in animation.channels:
        if channel.node in used and channel.path in PATHS:
            channels.setdefault(channel.node, {})[channel.path] = channel
    if not channels:
        raise invalid(f"Animation {animation.name!r} moves none of the model's bones", "/animations")
    return channels


def key_times(channels: Sequence[RigChannel]) -> tuple[tuple[float, ...], float]:
    """One key per frame: the smallest step between the times the channels hold, but never more than a frame at 24
    frames a second, because Blender leaves out the frames that change nothing; and how near a key a time is that key.
    """
    first = min(channel.times[0] for channel in channels)
    last = max(channel.times[-1] for channel in channels)
    if last <= first:
        return (first,), 0.0
    steps = (later - earlier for channel in channels for earlier, later in pairwise(channel.times) if later > earlier)
    step = min(1 / FRAMES_PER_SECOND, *steps)
    keys = round((last - first) / step) + 1
    if keys > MAX_KEYS:
        raise invalid(
            f"The animation would need {keys:,} keys, one per step of {step:g} s, and an action holds {MAX_KEYS:,}",
            "/animations",
        )
    return tuple(first + (last - first) * key / (keys - 1) for key in range(keys)), step * NEAR_KEY


def _local(rig: Rig, node: int, channels: dict[str, RigChannel], time: float, near: float) -> Affine:
    rest = rig.nodes[node]
    place = vector(sampled(channels[TRANSLATION], time, near)) if TRANSLATION in channels else rest.translation
    turn = normalized(sampled(channels[ROTATION], time, near)) if ROTATION in channels else rest.rotation
    scale = vector(sampled(channels[SCALE], time, near)) if SCALE in channels else rest.scale
    return Affine.of(place, turn, scale)


def _poses(
    rig: Rig, bones: Sequence[_Bone], moving: dict[int, dict[str, RigChannel]], time: float, near: float
) -> dict[int, _Pose]:
    locals_: dict[int, Affine] = {}
    poses: dict[int, _Pose] = {}
    for bone in bones:
        for node in bone.chain:
            if node not in locals_:
                locals_[node] = _local(rig, node, moving.get(node, {}), time, near)
        moved = reduce(matmul, (locals_[node] for node in bone.chain))
        if bone.root:
            moved = UNDO_TURN_UP @ moved
        poses[bone.index] = moved.place, moved.turn()
    return poses


def _stored_floats(numbers: Vector, pointer: str) -> list[JsonValue]:
    return [float_view(float_bytes(number, f"{pointer}/{axis}")) for axis, number in enumerate(numbers)]


def _same_turn(stored: JsonValue, turn: Quaternion) -> bool:
    dot = abs(sum(a * b for a, b in zip(angle_quaternion(*stored_vector(stored)), turn, strict=True)))
    return 2 * math.acos(min(1.0, dot)) <= SAME_TURN


def _same_place(stored: JsonValue, place: Vector, near: float) -> bool:
    return all(abs(a - b) <= near for a, b in zip(stored_vector(stored), place, strict=True))


def place_tolerance(bones: Sequence[JsonValue]) -> float:
    """How near its stored place a key comes back as it was: Blender's float noise grows with the model's places."""
    numbers = (
        abs(number)
        for bone in bones
        if isinstance(bone, dict) and is_real(bone)
        for action in _listed(bone.get("actions"))
        if isinstance(action, dict)
        for place in _listed(action.get("positions"))
        for number in stored_vector(place)
    )
    return max(SAME_PLACE, SAME_PLACE_SHARE * max(numbers, default=0.0))


def _listed(value: JsonValue) -> list[JsonValue]:
    return value if isinstance(value, list) else []


def _bone_entry(
    bone: JsonObject, index: int, poses: Sequence[_Pose], kept: JsonObject | None, near_place: float
) -> JsonObject:
    stored_places = _listed(kept.get("positions")) if kept else []
    stored_turns = _listed(kept.get("rotations")) if kept else []
    places: list[JsonValue] = []
    turns: list[JsonValue] = []
    for key, (place, turn) in enumerate(poses):
        pointer = f"/bones/{index}"
        if key < len(stored_places) and _same_place(stored_places[key], place, near_place):
            places.append(list(_listed(stored_places[key])))
        else:
            places.append(_stored_floats(place, f"{pointer}/positions/{key}"))
        if key < len(stored_turns) and _same_turn(stored_turns[key], turn):
            turns.append(list(_listed(stored_turns[key])))
        else:
            turns.append(_stored_floats(vector(quaternion_angles(*turn)), f"{pointer}/rotations/{key}"))
    return {"name": name_text(bone["name"]), "parent": bone["parent"], "positions": places, "rotations": turns}


def _lock(view: JsonObject, at: int | None, keys: int) -> JsonObject:
    replaced = _listed(view.get("actions"))[at] if at is not None else None
    lock = replaced.get("lock", 0) if isinstance(replaced, dict) else 0
    if not lock or not isinstance(replaced, dict):
        return {"lock": 0}
    held = _listed(replaced.get("positions"))
    if len(held) == keys:
        return {"lock": lock, "positions": [list(_listed(place)) for place in held]}
    first = list(_listed(held[0])) if held else [0.0, 0.0, 0.0]
    return {"lock": lock, "positions": [list(first) for _ in range(keys)]}


def animation_action(rig: Rig, animation: int, view: JsonObject, at: int | None = None) -> JsonObject:
    """An animation of the file as an action file for the model; over action at, a key that comes back as it was
    keeps its stored floats."""
    bones = _listed(view.get("bones"))
    found = _bones(rig, bones)
    moving = _moving(rig.animations[animation], found)
    times, near = key_times([channel for channels in moving.values() for channel in channels.values()])
    poses = [_poses(rig, found, moving, time, near) for time in times]
    near_place = place_tolerance(bones) if at is not None else SAME_PLACE
    entries: list[JsonValue] = []
    for index, bone in enumerate(bones):
        if not is_real(bone):
            entries.append(dict(bone) if isinstance(bone, dict) else bone)
            continue
        assert isinstance(bone, dict)
        stored = _listed(bone.get("actions"))[at] if at is not None else None
        kept = stored if isinstance(stored, dict) else None
        entries.append(_bone_entry(bone, index, [pose[index] for pose in poses], kept, near_place))
    return {
        "format": ACTION_FILE_FORMAT,
        "format_version": ACTION_FILE_VERSION,
        "keys": len(times),
        **_lock(view, at, len(times)),
        "bones": entries,
    }
