import math
from collections import Counter
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from typing import Final, Literal

from mutools.app.model_parts import NO_PARENT, tree_parents
from mutools.app.model_scene import POSITION_WIDTH, SceneMesh, angle_quaternion
from mutools.core.affine import ORIGIN, SAME_SIZE, UNTURNED, Affine, Quaternion, Vector, both_turns, vector
from mutools.core.formats.record_fields import FLOAT32, float_bytes
from mutools.core.json_types import JsonObject, JsonValue
from mutools.core.leaves import leaf_text

type ChannelPath = Literal["translation", "rotation", "scale"]
type Interpolation = Literal["STEP", "LINEAR", "CUBICSPLINE"]

MU_TO_GLTF: Final[Quaternion] = (-math.sqrt(0.5), 0.0, 0.0, math.sqrt(0.5))
FRAMES_PER_SECOND: Final = 24
QUATERNION_WIDTH: Final = 4
TRANSLATION: Final = "translation"
ROTATION: Final = "rotation"
SCALE: Final = "scale"
LINEAR: Final = "LINEAR"
JPEG: Final = "image/jpeg"
PNG: Final = "image/png"
UP: Final[Vector] = (0.0, 0.0, 1.0)
TURN_UP: Final = Affine.of(turn=MU_TO_GLTF)


@dataclass(frozen=True, slots=True)
class RigNode:
    name: str
    parent: int | None
    translation: Vector = ORIGIN
    rotation: Quaternion = UNTURNED
    scale: Vector = SAME_SIZE

    @property
    def local(self) -> Affine:
        return Affine.of(self.translation, self.rotation, self.scale)


@dataclass(frozen=True, slots=True)
class RigImage:
    data: bytes
    mime: str


@dataclass(frozen=True, slots=True)
class RigMesh:
    """Corners in glTF's space, three to a triangle, each on one joint (an index into Rig.joints)."""

    name: str
    positions: tuple[float, ...]
    normals: tuple[float, ...]
    uvs: tuple[float, ...]
    joints: tuple[int, ...]
    image: int | None


@dataclass(frozen=True, slots=True)
class RigChannel:
    node: int
    path: ChannelPath
    times: tuple[float, ...]
    values: tuple[float, ...]
    interpolation: Interpolation = LINEAR


@dataclass(frozen=True, slots=True)
class RigAnimation:
    name: str
    channels: tuple[RigChannel, ...]


@dataclass(frozen=True, slots=True)
class Rig:
    """What a .glb holds for mutools: nodes, the skin's joints and their inverse bind matrices, meshes, animations."""

    nodes: tuple[RigNode, ...]
    joints: tuple[int, ...] = ()
    inverse_binds: tuple[tuple[float, ...], ...] = ()
    meshes: tuple[RigMesh, ...] = ()
    images: tuple[RigImage, ...] = ()
    animations: tuple[RigAnimation, ...] = ()


@dataclass(frozen=True, slots=True)
class SkinnedMesh:
    """A mesh of the model or of a part that borrows its skeleton, with its texture when one was found."""

    name: str
    mesh: SceneMesh
    image: RigImage | None


@dataclass(frozen=True, slots=True)
class ChosenAction:
    index: int
    name: str


def _items(value: JsonValue) -> list[JsonValue]:
    return value if isinstance(value, list) else []


def _object(value: JsonValue) -> JsonObject:
    return value if isinstance(value, dict) else {}


def is_real(bone: JsonValue) -> bool:
    return isinstance(bone, dict) and "dummy" not in bone


def stored_number(value: JsonValue) -> float:
    number: float = FLOAT32.unpack(float_bytes(value, ""))[0]
    return number


def stored_vector(value: JsonValue) -> Vector:
    return vector(stored_number(number) for number in _items(value))


def joint_names(bones: Sequence[JsonValue]) -> dict[int, str]:
    """A real bone's joint is named after it; a name that repeats, or none, gets the bone's number after it."""
    names = {index: leaf_text(_object(bone).get("name")) or "" for index, bone in enumerate(bones) if is_real(bone)}
    counts = Counter(names.values())
    return {
        index: name if name and counts[name] == 1 else f"{name} ({index})".lstrip() for index, name in names.items()
    }


def rest_pose(bone: JsonValue) -> tuple[Vector, Quaternion]:
    """A bone at key 0 of action 0, as the studio frames a model; a bone with no key there stays where it is."""
    pose = _object(next(iter(_items(_object(bone).get("actions"))), None))
    positions, rotations = _items(pose.get("positions")), _items(pose.get("rotations"))
    if not positions or not rotations:
        return ORIGIN, UNTURNED
    return stored_vector(positions[0]), angle_quaternion(*stored_vector(rotations[0]))


def rest_worlds(bones: Sequence[JsonValue]) -> list[Affine]:
    parents = tree_parents(bones)
    rests = [Affine.of(*rest_pose(bone)) for bone in bones]
    worlds: dict[int, Affine] = {}
    for start in range(len(bones)):
        waiting: list[int] = []
        index = start
        while index != NO_PARENT and index not in worlds:
            waiting.append(index)
            index = parents[index]
        for index in reversed(waiting):
            parent = parents[index]
            worlds[index] = rests[index] if parent == NO_PARENT else worlds[parent] @ rests[index]
    return [worlds[index] for index in range(len(bones))]


def _unit(numbers: Vector) -> Vector:
    length = math.sqrt(sum(number * number for number in numbers))
    return UP if length == 0 else vector(number / length for number in numbers)


def _rig_mesh(skinned: SkinnedMesh, binds: Sequence[Affine], joint_of: dict[int, int], image: int | None) -> RigMesh:
    mesh = skinned.mesh
    positions: list[float] = []
    normals: list[float] = []
    for corner, bone in enumerate(mesh.bones):
        start = corner * POSITION_WIDTH
        positions += binds[bone].applied(vector(mesh.positions[start : start + POSITION_WIDTH]))
        normals += _unit(binds[bone].turned(vector(mesh.normals[start : start + POSITION_WIDTH])))
    joints = tuple(joint_of[bone] for bone in mesh.bones)
    return RigMesh(skinned.name, tuple(positions), tuple(normals), mesh.uvs, joints, image)


def _continuous(turns: Iterator[Quaternion]) -> Iterator[Quaternion]:
    last: Quaternion | None = None
    for turn in turns:
        x, y, z, w = turn
        flipped = last is not None and sum(a * b for a, b in zip(turn, last, strict=True)) < 0
        last = (-x, -y, -z, -w) if flipped else turn
        yield last


def _pose(place: Vector, turn: Quaternion, root: bool) -> tuple[Vector, Quaternion]:
    return (TURN_UP.turned(place), both_turns(MU_TO_GLTF, turn)) if root else (place, turn)


def _channels(
    bone: JsonObject, node: int, root: bool, action: int, times: tuple[float, ...]
) -> tuple[RigChannel, RigChannel]:
    pose = _object(_items(bone.get("actions"))[action])
    keys = zip(_items(pose.get("positions")), _items(pose.get("rotations")), strict=True)
    poses = [_pose(stored_vector(place), angle_quaternion(*stored_vector(turn)), root) for place, turn in keys]
    turns = _continuous(turn for _, turn in poses)
    return (
        RigChannel(node, TRANSLATION, times, tuple(number for place, _ in poses for number in place)),
        RigChannel(node, ROTATION, times, tuple(number for turn in turns for number in turn)),
    )


def _animation(
    bones: Sequence[JsonValue], nodes: dict[int, int], roots: set[int], keys: int, chosen: ChosenAction
) -> RigAnimation:
    times = tuple(key / FRAMES_PER_SECOND for key in range(keys))
    channels = (
        channel
        for bone, node in nodes.items()
        for channel in _channels(_object(bones[bone]), node, bone in roots, chosen.index, times)
    )
    return RigAnimation(f"{chosen.index:03} {chosen.name}", tuple(channels))


def model_rig(view: JsonObject, meshes: Sequence[SkinnedMesh], actions: Sequence[ChosenAction]) -> Rig:
    """The model as a glTF scene: its real bones as joints in MU's own numbers, but for the roots, turned to Y up."""
    bones = _items(view.get("bones"))
    parents = tree_parents(bones)
    names = joint_names(bones)
    roots = {bone for bone in names if parents[bone] == NO_PARENT}
    node_of = {bone: node for node, bone in enumerate(names)}
    nodes = [
        RigNode(name, None if bone in roots else node_of[parents[bone]], *_pose(*rest_pose(bones[bone]), bone in roots))
        for bone, name in names.items()
    ]
    binds = [TURN_UP @ world for world in rest_worlds(bones)]
    joint_of = {bone: joint for joint, bone in enumerate(node_of)}
    images: list[RigImage] = []
    rig_meshes = []
    for skinned in meshes:
        if not skinned.mesh.bones:
            continue
        image = None
        if skinned.image is not None:
            image = len(images)
            images.append(skinned.image)
        rig_meshes.append(_rig_mesh(skinned, binds, joint_of, image))
    keys = [_object(action).get("keys") for action in _items(view.get("actions"))]
    animations = tuple(
        _animation(bones, node_of, roots, count, chosen)
        for chosen in actions
        if isinstance(count := keys[chosen.index], int) and count > 0
    )
    return Rig(
        nodes=tuple(nodes),
        joints=tuple(node_of.values()),
        inverse_binds=tuple(binds[bone].inverse().columns() for bone in node_of),
        meshes=tuple(rig_meshes),
        images=tuple(images),
        animations=animations,
    )
