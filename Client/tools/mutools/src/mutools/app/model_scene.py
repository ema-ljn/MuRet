import math
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from mutools.app.model_parts import NO_PARENT, tree_parents
from mutools.core.json_types import JsonObject, JsonValue
from mutools.core.leaves import leaf_text
from mutools.core.pointer import resolve
from mutools.core.ports import FileStore

type Quaternion = tuple[float, float, float, float]
type _Point = tuple[float, float, float]
type _Corner = tuple[int, tuple[float, ...], tuple[float, ...], tuple[float, ...]]
type _Placed = tuple[Quaternion, _Point]

DONOR_NAME: Final = "player.bmd"
TRIANGLE: Final = 3
TRIANGLE_CORNERS: Final = (0, 1, 2)
QUAD_CORNERS: Final = (0, 1, 2, 0, 2, 3)
POSITION_WIDTH: Final = 3
UV_WIDTH: Final = 2
UNTURNED: Final[Quaternion] = (0.0, 0.0, 0.0, 1.0)
ORIGIN: Final[_Point] = (0.0, 0.0, 0.0)
UP: Final[_Point] = (0.0, 0.0, 1.0)
BOUNDS_DIGITS: Final = 3
STRAIGHT_UP: Final = 1e-12


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
    normals: tuple[float, ...]
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


@dataclass(frozen=True, slots=True)
class Bounds:
    low: _Point
    high: _Point


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


def quaternion_angles(x: float, y: float, z: float, w: float) -> tuple[float, float, float]:
    """The inverse of angle_quaternion: three angles in radians, the middle one from -pi/2 to pi/2.

    Straight up or down only the first and the last angle's difference counts, so the last one is 0 there.
    """
    size = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / size, y / size, z / size, w / size
    sine = max(-1.0, min(1.0, 2 * (w * y - x * z)))
    if 1 - sine * sine < STRAIGHT_UP:
        side = math.copysign(1.0, sine)
        return math.atan2(side * 2 * (x * y - w * z), 1 - 2 * (x * x + z * z)), side * math.pi / 2, 0.0
    return (
        math.atan2(2 * (w * x + y * z), 1 - 2 * (x * x + y * y)),
        math.asin(sine),
        math.atan2(2 * (w * z + x * y), 1 - 2 * (y * y + z * z)),
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
    normal = _object(_at(_items(mesh.get("normals")), _at(_items(triangle.get("normals")), slot))).get("normal")
    bone = _object(vertex).get("bone")
    if uv is None or not isinstance(bone, int) or not 0 <= bone < len(drawn) or not drawn[bone]:
        return None
    position = _numbers(_object(vertex).get("position"), POSITION_WIDTH)
    return bone, position, _numbers(normal, POSITION_WIDTH) if normal is not None else UP, _numbers(uv, UV_WIDTH)


def _drawn_corners(triangle: JsonObject, mesh: JsonObject, drawn: Sequence[bool]) -> list[_Corner] | None:
    corners = [_corner(triangle, slot, mesh, drawn) for slot in _slots(triangle)]
    found = [corner for corner in corners if corner is not None]
    return found if found and len(found) == len(corners) else None


def _scene_mesh(mesh: JsonObject, drawn: Sequence[bool]) -> SceneMesh:
    positions: list[float] = []
    normals: list[float] = []
    uvs: list[float] = []
    bones: list[int] = []
    skipped = 0
    for triangle in _items(mesh.get("triangles")):
        corners = _drawn_corners(_object(triangle), mesh, drawn)
        if corners is None:
            skipped += 1
            continue
        for bone, position, normal, uv in corners:
            bones.append(bone)
            positions += position
            normals += normal
            uvs += uv
    return SceneMesh(tuple(positions), tuple(normals), tuple(uvs), tuple(bones), skipped)


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


def _held_in_place(pose: BonePose) -> BonePose:
    x, y = pose.positions[:2]
    heights = pose.positions[2::POSITION_WIDTH]
    return BonePose(tuple(number for z in heights for number in (x, y, z)), pose.quaternions)


def action_poses(view: JsonObject, action: int) -> ActionPoses:
    """As the client poses it (0x4eb860): the first bone of an action with `lock` keeps the x and y of its key 0."""
    chosen = resolve(view, f"/actions/{action}")
    poses = [_bone_pose(bone, action) for bone in _items(view.get("bones"))]
    first = poses[0] if poses else None
    if _count(chosen, "lock") and first is not None and first.positions:
        poses[0] = _held_in_place(first)
    return ActionPoses(_count(chosen, "keys"), tuple(poses))


def _point(value: JsonValue) -> _Point:
    x, y, z = _numbers(value, POSITION_WIDTH)
    return x, y, z


def _turned(turn: Quaternion, point: _Point) -> _Point:
    x, y, z, w = turn
    px, py, pz = point
    tx, ty, tz = 2 * (y * pz - z * py), 2 * (z * px - x * pz), 2 * (x * py - y * px)
    return px + w * tx + (y * tz - z * ty), py + w * ty + (z * tx - x * tz), pz + w * tz + (x * ty - y * tx)


def _both_turns(first: Quaternion, second: Quaternion) -> Quaternion:
    ax, ay, az, aw = first
    bx, by, bz, bw = second
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def _placed_in(holder: _Placed, point: _Point) -> _Point:
    turn, (x, y, z) = holder
    moved_x, moved_y, moved_z = _turned(turn, point)
    return x + moved_x, y + moved_y, z + moved_z


def _first_pose(bone: JsonValue) -> _Placed:
    pose = _object(_at(_items(_object(bone).get("actions")), 0))
    position = _at(_items(pose.get("positions")), 0)
    turn = _at(_items(pose.get("rotations")), 0)
    if position is None or turn is None:
        return UNTURNED, ORIGIN
    return angle_quaternion(*_point(turn)), _point(position)


def _placed_bones(bones: Sequence[JsonValue]) -> list[_Placed]:
    parents = tree_parents(bones)
    placed: dict[int, _Placed] = {}
    for start in range(len(bones)):
        waiting: list[int] = []
        index = start
        while index != NO_PARENT and index not in placed:
            waiting.append(index)
            index = parents[index]
        for index in reversed(waiting):
            turn, place = _first_pose(bones[index])
            if parents[index] != NO_PARENT:
                holder = placed[parents[index]]
                turn, place = _both_turns(holder[0], turn), _placed_in(holder, place)
            placed[index] = turn, place
    return [placed[index] for index in range(len(bones))]


def _corner_places(meshes: Sequence[SceneMesh], placed: Sequence[_Placed]) -> Iterator[_Point]:
    for mesh in meshes:
        for corner, bone in enumerate(mesh.bones):
            x, y, z = mesh.positions[corner * POSITION_WIDTH : (corner + 1) * POSITION_WIDTH]
            yield _placed_in(placed[bone], (x, y, z))


def rest_bounds(view: JsonObject, skeleton: JsonObject | None = None) -> Bounds | None:
    bones = _items((view if skeleton is None else skeleton).get("bones"))
    placed = _placed_bones(bones)
    points = list(_corner_places(model_scene(view, skeleton).meshes, placed))
    if not points:
        points = [place for bone, (_, place) in zip(bones, placed, strict=True) if not _is_dummy(bone)]
    if not points:
        return None
    low_x, low_y, low_z = (round(min(axis), BOUNDS_DIGITS) for axis in zip(*points, strict=True))
    high_x, high_y, high_z = (round(max(axis), BOUNDS_DIGITS) for axis in zip(*points, strict=True))
    return Bounds((low_x, low_y, low_z), (high_x, high_y, high_z))


def _with_ancestors(used: set[int], bones: Sequence[SceneBone]) -> set[int]:
    needed: set[int] = set()
    for bone in used:
        index = bone
        while index != NO_PARENT and index not in needed:
            needed.add(index)
            index = bones[index].parent
    return needed


def borrows_skeleton(view: JsonObject, donor: JsonObject) -> bool:
    scene = model_scene(view)
    theirs = _scene_bones(_items(donor.get("bones")))
    used = {bone for mesh in scene.meshes for bone in mesh.bones}
    needed = _with_ancestors(used, scene.bones)
    return bool(used) and all(index < len(theirs) and scene.bones[index] == theirs[index] for index in needed)


def donor_of(store: FileStore, model: Path) -> Path | None:
    if model.name.casefold() == DONOR_NAME:
        return None
    files = store.files_in(model.absolute().parent)
    return next((path for path in files if path.name.casefold() == DONOR_NAME), None)
