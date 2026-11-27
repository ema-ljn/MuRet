from struct import Struct
from typing import Final

from mubmd.core.ciphers.k16 import decrypt_k16, encrypt_k16
from mubmd.core.documents.model import (
    ENCRYPTED_VERSION,
    KEY_SIZE,
    MAX_POSED_BONE_ACTIONS,
    MODEL_MAGIC,
    NAME_SIZE,
    NORMAL_SIZE,
    TRIANGLE_SIZE,
    UV_SIZE,
    VERTEX_SIZE,
    Action,
    Bone,
    Mesh,
    Model,
)
from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.formats.model_view import MODEL_FIELDS, MODEL_FORMAT_NAME, model_from_view, model_view
from mubmd.core.json_types import JsonObject
from mubmd.core.ports import CatalogFile, FieldInfo, Summary

MODEL_ENCODING: Final = "cp949"
HEADER_SIZE: Final = len(MODEL_MAGIC) + 1
SIZE_WORD: Final = Struct("<I")
COUNTS: Final = Struct("<3h")
MESH_HEADER: Final = Struct("<5h")
ACTION_HEADER: Final = Struct("<hB")
PARENT: Final = Struct("<h")
DUMMY_SIZE: Final = 1
SMALLEST_MESH: Final = MESH_HEADER.size + NAME_SIZE
KEYS_PER_ACTION_KEY: Final = 2 * KEY_SIZE


def _corrupt(file_name: str, message: str) -> MubmdError:
    return MubmdError(ErrorCode.CORRUPT_FILE, f"{file_name}: {message}")


class _Reader:
    def __init__(self, data: bytes, file_name: str) -> None:
        self._data = data
        self._offset = 0
        self._file_name = file_name

    @property
    def left(self) -> int:
        return len(self._data) - self._offset

    def corrupt(self, message: str) -> MubmdError:
        return _corrupt(self._file_name, message)

    def take(self, size: int, what: str) -> bytes:
        if size > self.left:
            raise self.corrupt(f"{size:,} bytes are needed for {what} and {self.left:,} are left")
        start = self._offset
        self._offset += size
        return self._data[start : self._offset]

    def unpack(self, layout: Struct, what: str) -> tuple[int, ...]:
        return layout.unpack(self.take(layout.size, what))

    def fits(self, count: int, size: int, owner: str, what: str) -> None:
        if count < 0:
            raise self.corrupt(f"{owner} has a negative count of {what}")
        if count * size > self.left:
            raise self.corrupt(f"{owner} asks for {count:,} {what} and {self.left:,} bytes are left")

    def block(self, count: int, size: int, owner: str, what: str) -> bytes:
        self.fits(count, size, owner, what)
        return self.take(count * size, what)

    def rest(self) -> bytes:
        return self.take(self.left, "the rest")


def _content(data: bytes, version: int, file_name: str) -> tuple[bytes, bytes]:
    if version != ENCRYPTED_VERSION:
        return data[HEADER_SIZE:], b""
    start = HEADER_SIZE + SIZE_WORD.size
    if len(data) < start:
        raise _corrupt(file_name, "a version 12 model has a size word after its version")
    (size,) = SIZE_WORD.unpack_from(data, HEADER_SIZE)
    if size > len(data) - start:
        raise _corrupt(file_name, f"the size word says {size:,} bytes and {len(data) - start:,} follow it")
    return decrypt_k16(data[start : start + size]), data[start + size :]


def _read_mesh(reader: _Reader, index: int) -> Mesh:
    owner = f"mesh {index}"
    vertices, normals, uvs, triangles, texture = reader.unpack(MESH_HEADER, f"{owner}'s counts")
    return Mesh(
        texture,
        reader.block(vertices, VERTEX_SIZE, owner, "vertices"),
        reader.block(normals, NORMAL_SIZE, owner, "normals"),
        reader.block(uvs, UV_SIZE, owner, "uvs"),
        reader.block(triangles, TRIANGLE_SIZE, owner, "triangles"),
        reader.take(NAME_SIZE, f"{owner}'s texture name"),
    )


def _read_action(reader: _Reader, index: int) -> Action:
    owner = f"action {index}"
    keys, lock = reader.unpack(ACTION_HEADER, f"{owner}'s header")
    reader.fits(keys, KEY_SIZE if lock else 0, owner, "keys")
    return Action(keys, lock, reader.take(keys * KEY_SIZE, f"{owner}'s positions") if lock else b"")


def _read_bones(reader: _Reader, count: int, actions: tuple[Action, ...]) -> tuple[Bone, ...]:
    keys_size = KEYS_PER_ACTION_KEY * sum(action.keys for action in actions)
    bones: list[Bone] = []
    real_bones = 0
    for index in range(count):
        owner = f"bone {index}"
        (dummy,) = reader.take(DUMMY_SIZE, owner)
        if dummy:
            bones.append(Bone(dummy))
            continue
        real_bones += 1
        if real_bones * len(actions) > MAX_POSED_BONE_ACTIONS:
            raise reader.corrupt(
                f"{owner} takes the model past {MAX_POSED_BONE_ACTIONS:,} bones times actions "
                f"({len(actions):,} actions)"
            )
        name = reader.take(NAME_SIZE, f"{owner}'s name")
        (parent,) = reader.unpack(PARENT, f"{owner}'s parent")
        bones.append(Bone(dummy, name, parent, reader.take(keys_size, f"{owner}'s keys")))
    return tuple(bones)


def _mesh_bytes(mesh: Mesh) -> bytes:
    counts = MESH_HEADER.pack(
        len(mesh.vertices) // VERTEX_SIZE,
        len(mesh.normals) // NORMAL_SIZE,
        len(mesh.uvs) // UV_SIZE,
        len(mesh.triangles) // TRIANGLE_SIZE,
        mesh.texture,
    )
    return counts + mesh.vertices + mesh.normals + mesh.uvs + mesh.triangles + mesh.texture_name


def _bone_bytes(bone: Bone) -> bytes:
    if bone.dummy:
        return bytes((bone.dummy,))
    return bytes((bone.dummy,)) + bone.name + PARENT.pack(bone.parent) + bone.keys


def _content_bytes(model: Model) -> bytes:
    parts = [model.name, COUNTS.pack(len(model.meshes), len(model.bones), len(model.actions))]
    parts.extend(_mesh_bytes(mesh) for mesh in model.meshes)
    parts.extend(ACTION_HEADER.pack(action.keys, action.lock) + action.positions for action in model.actions)
    parts.extend(_bone_bytes(bone) for bone in model.bones)
    parts.append(model.trailing)
    return b"".join(parts)


class ModelFormat:
    name = MODEL_FORMAT_NAME

    def decode(self, data: bytes, file: CatalogFile) -> Model:
        if len(data) < HEADER_SIZE or not data.startswith(MODEL_MAGIC):
            raise _corrupt(file.name, "a model starts with BMD and a version byte")
        version = data[len(MODEL_MAGIC)]
        content, after_block = _content(data, version, file.name)
        reader = _Reader(content, file.name)
        name = reader.take(NAME_SIZE, "the model's name")
        mesh_count, bone_count, action_count = reader.unpack(COUNTS, "the model's counts")
        reader.fits(mesh_count, SMALLEST_MESH, "the model", "meshes")
        meshes = tuple(_read_mesh(reader, index) for index in range(mesh_count))
        reader.fits(action_count, ACTION_HEADER.size, "the model", "actions")
        actions = tuple(_read_action(reader, index) for index in range(action_count))
        reader.fits(bone_count, DUMMY_SIZE, "the model", "bones")
        bones = _read_bones(reader, bone_count, actions)
        encoding = file.encoding or MODEL_ENCODING
        return Model(version, name, meshes, actions, bones, reader.rest(), after_block, encoding)

    def encode(self, document: Model) -> bytes:
        header = MODEL_MAGIC + bytes((document.version,))
        content = _content_bytes(document)
        if document.version != ENCRYPTED_VERSION:
            return header + content
        return header + SIZE_WORD.pack(len(content)) + encrypt_k16(content) + document.after_block

    def to_view(self, document: Model) -> JsonObject:
        return model_view(document)

    def from_view(self, view: JsonObject) -> Model:
        return model_from_view(view)

    def describe_fields(self) -> tuple[FieldInfo, ...]:
        return MODEL_FIELDS

    def summarize(self, document: Model) -> Summary:
        return Summary(
            "K16" if document.version == ENCRYPTED_VERSION else "none",
            None,
            None,
            version=document.version,
            meshes=len(document.meshes),
            bones=len(document.bones),
            actions=len(document.actions),
        )
