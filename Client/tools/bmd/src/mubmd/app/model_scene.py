import math
from collections.abc import Sequence
from dataclasses import dataclass
from typing import Final

from mubmd.app.model_parts import tree_parents
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.leaves import leaf_text
from mubmd.core.pointer import resolve

type Quaternion = tuple[float, float, float, float]
type _Corner = tuple[int, tuple[float, ...], tuple[float, ...]]

TRIANGLE: Final = 3
TRIANGLE_CORNERS: Final = (0, 1, 2)
QUAD_CORNERS: Final = (0, 1, 2, 0, 2, 3)
POSITION_WIDTH: Final = 3
UV_WIDTH: Final = 2


@dataclass(frozen=True, slots=True)
class SceneBone:
    name: str
    parent: int


@dataclass(frozen=True, slots=True)
class SceneAction:
    keys: int
    lock: bool


@dataclass(frozen=True, slots=True)
class SceneMesh:
    positions: tuple[float, ...]
    uvs: tuple[float, ...]
    bones: tuple[int, ...]
    skipped: int


@dataclass(frozen=True, slots=True)
class Scene:
    bones: tuple[SceneBone, ...]
    actions: tuple[SceneAction, ...]
    meshes: tuple[SceneMesh, ...]


@dataclass(frozen=True, slots=True)
class BonePose:
    positions: tuple[float, ...]
    quaternions: tuple[float, ...]


@dataclass(frozen=True, slots=True)
class ActionPoses:
    keys: int
    bones: tuple[BonePose | None, ...]


def angle_quaternion(x: float, y: float, z: float) -> Quaternion:
    """Half-Life's, as the client's (0x5fcd20): the turn Rz · Ry · Rx of three angles in radians."""
    sin_x, cos_x = math.sin(x / 2), math.cos(x / 2)
    sin_y, cos_y = math.sin(y / 2), math.cos(y / 2)
    sin_z, cos_z = math.sin(z / 2), math.cos(z / 2)
    return (
        sin_x * cos_y * cos_z - cos_x * sin_y * sin_z,
        cos_x * sin_y * cos_z + sin_x * cos_y * sin_z,
        cos_x * cos_y * sin_z - sin_x * sin_y * cos_z,
        cos_x * cos_y * cos_z + sin_x * sin_y * sin_z,
    )


def _items(value: JsonValue) -> list[JsonValue]:
    return value if isinstance(value, list) else []


def _object(value: JsonValue) -> JsonObject:
    return value if isinstance(value, dict) else {}


def _at(items: Sequence[JsonValue], index: JsonValue) -> JsonValue:
    return items[index] if isinstance(index, int) and 0 <= index < len(items) else None


def _numbers(value: JsonValue, width: int) -> tuple[float, ...]:
    numbers = [float(item) if isinstance(item, int | float) else 0.0 for item in _items(value)[:width]]
    return (*numbers, *(0.0,) * (width - len(numbers)))


def _count(item: JsonValue, key: str) -> int:
    value = _object(item).get(key)
    return value if isinstance(value, int) else 0


def _is_dummy(bone: JsonValue) -> bool:
    return not isinstance(bone, dict) or "dummy" in bone


def _bone_name(bone: JsonValue) -> str:
    return "" if _is_dummy(bone) else leaf_text(_object(bone).get("name")) or ""


def _scene_bones(bones: Sequence[JsonValue]) -> tuple[SceneBone, ...]:
    return tuple(SceneBone(_bone_name(bone), parent) for bone, parent in zip(bones, tree_parents(bones), strict=True))


def _scene_actions(actions: Sequence[JsonValue]) -> tuple[SceneAction, ...]:
    return tuple(SceneAction(_count(action, "keys"), bool(_count(action, "lock"))) for action in actions)


def _slots(triangle: JsonObject) -> tuple[int, ...]:
    polygon = _count(triangle, "polygon")
    if polygon < TRIANGLE:
        return ()
    return TRIANGLE_CORNERS if polygon == TRIANGLE else QUAD_CORNERS


def _corner(triangle: JsonObject, slot: int, mesh: JsonObject, drawn: Sequence[bool]) -> _Corner | None:
    vertex = _at(_items(mesh.get("vertices")), _at(_items(triangle.get("vertices")), slot))
    uv = _at(_items(mesh.get("uvs")), _at(_items(triangle.get("uvs")), slot))
    bone = _object(vertex).get("bone")
    if uv is None or not isinstance(bone, int) or not 0 <= bone < len(drawn) or not drawn[bone]:
        return None
    return bone, _numbers(_object(vertex).get("position"), POSITION_WIDTH), _numbers(uv, UV_WIDTH)


def _drawn_corners(triangle: JsonObject, mesh: JsonObject, drawn: Sequence[bool]) -> list[_Corner] | None:
    corners = [_corner(triangle, slot, mesh, drawn) for slot in _slots(triangle)]
    found = [corner for corner in corners if corner is not None]
    return found if found and len(found) == len(corners) else None


def _scene_mesh(mesh: JsonObject, drawn: Sequence[bool]) -> SceneMesh:
    positions: list[float] = []
    uvs: list[float] = []
    bones: list[int] = []
    skipped = 0
    for triangle in _items(mesh.get("triangles")):
        corners = _drawn_corners(_object(triangle), mesh, drawn)
        if corners is None:
            skipped += 1
            continue
        for bone, position, uv in corners:
            bones.append(bone)
            positions += position
            uvs += uv
    return SceneMesh(tuple(positions), tuple(uvs), tuple(bones), skipped)


def model_scene(view: JsonObject, skeleton: JsonObject | None = None) -> Scene:
    posed = view if skeleton is None else skeleton
    bones = _items(posed.get("bones"))
    drawn = [not _is_dummy(bone) for bone in bones]
    meshes = tuple(_scene_mesh(_object(mesh), drawn) for mesh in _items(view.get("meshes")))
    return Scene(_scene_bones(bones), _scene_actions(_items(posed.get("actions"))), meshes)


def _bone_pose(bone: JsonValue, action: int) -> BonePose | None:
    pose = _at(_items(_object(bone).get("actions")), action)
    if not isinstance(pose, dict):
        return None
    positions = [number for key in _items(pose.get("positions")) for number in _numbers(key, POSITION_WIDTH)]
    turns = (_numbers(key, POSITION_WIDTH) for key in _items(pose.get("rotations")))
    quaternions = [number for x, y, z in turns for number in angle_quaternion(x, y, z)]
    return BonePose(tuple(positions), tuple(quaternions))


def action_poses(view: JsonObject, action: int) -> ActionPoses:
    keys = _count(resolve(view, f"/actions/{action}"), "keys")
    return ActionPoses(keys, tuple(_bone_pose(bone, action) for bone in _items(view.get("bones"))))
