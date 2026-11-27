from dataclasses import dataclass
from typing import Final

MODEL_MAGIC: Final = b"BMD"
ENCRYPTED_VERSION: Final = 0x0C
NAME_SIZE: Final = 32
VERTEX_SIZE: Final = 16
NORMAL_SIZE: Final = 20
UV_SIZE: Final = 8
TRIANGLE_SIZE: Final = 64
KEY_SIZE: Final = 12
MAX_POSED_BONE_ACTIONS: Final = 1 << 16
MAX_BONES: Final = 200


@dataclass(frozen=True, slots=True)
class Mesh:
    texture: int
    vertices: bytes
    normals: bytes
    uvs: bytes
    triangles: bytes
    texture_name: bytes


@dataclass(frozen=True, slots=True)
class Action:
    keys: int
    lock: int
    positions: bytes = b""


@dataclass(frozen=True, slots=True)
class Bone:
    dummy: int
    name: bytes = b""
    parent: int = -1
    # For each action in order: its positions, then its rotations.
    keys: bytes = b""


@dataclass(frozen=True, slots=True)
class Model:
    version: int
    name: bytes
    meshes: tuple[Mesh, ...]
    actions: tuple[Action, ...]
    bones: tuple[Bone, ...]
    trailing: bytes
    after_block: bytes
    encoding: str
