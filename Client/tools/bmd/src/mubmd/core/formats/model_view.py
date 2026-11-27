from collections.abc import Callable, Iterator
from itertools import chain
from math import isfinite
from struct import Struct
from typing import Any, Final

from mubmd.core.documents.model import (
    ENCRYPTED_VERSION,
    KEY_SIZE,
    MAX_POSED_BONE_ACTIONS,
    NAME_SIZE,
    Action,
    Bone,
    Mesh,
    Model,
)
from mubmd.core.documents.text_table import decode_text, encode_fixed_text
from mubmd.core.formats.record_fields import (
    FLOAT32,
    float_bytes,
    float_view,
    hex_bytes,
    invalid,
    read_hex,
    text_bytes,
    text_view,
    view_encoding,
)
from mubmd.core.json_types import VIEW_VERSION, JsonObject, JsonValue
from mubmd.core.ports import FieldInfo

MODEL_FORMAT_NAME: Final = "model"
REFERENCE: Final = 'research 05, "Modelos .bmd"'
INT16_MIN: Final = -(1 << 15)
INT16_MAX: Final = (1 << 15) - 1
MAX_ITEMS: Final = INT16_MAX
BYTE_MAX: Final = 0xFF
POSITION_WIDTH: Final = 3
UV_WIDTH: Final = 2
CORNERS: Final = 4

VERTEX: Final = Struct("<h2s4s4s4s")
NORMAL: Final = Struct("<h2s4s4s4sh2s")
UV: Final = Struct("<4s4s")
TRIANGLE: Final = Struct("<B1s4h4h4h38s")
KEY: Final = Struct("<4s4s4s")
RAW_0X02_SIZE: Final = 2
RAW_0X12_SIZE: Final = 2
RAW_0X01_SIZE: Final = 1
RAW_0X1A_SIZE: Final = 38

ROOT_MEMBERS: Final = frozenset(
    {"format_version", "format", "encoding", "version", "name", "meshes", "actions", "bones", "trailing"}
)
MESH_MEMBERS: Final = frozenset({"texture", "texture_name", "vertices", "normals", "uvs", "triangles"})
VERTEX_MEMBERS: Final = frozenset({"bone", "position"})
NORMAL_MEMBERS: Final = frozenset({"bone", "normal", "vertex"})
TRIANGLE_MEMBERS: Final = frozenset({"polygon", "vertices", "normals", "uvs"})
ACTION_MEMBERS: Final = frozenset({"keys", "lock"})
BONE_MEMBERS: Final = frozenset({"name", "parent", "actions"})
DUMMY_MEMBERS: Final = frozenset({"dummy"})
BONE_ACTION_MEMBERS: Final = frozenset({"positions", "rotations"})


def _show_raw(view: JsonObject, name: str, data: bytes) -> None:
    if any(data):
        view[name] = {"hex": data.hex()}


def _vector_view(parts: tuple[bytes, ...]) -> JsonValue:
    return [float_view(part) for part in parts]


def _vertex_view(bone: int, raw: bytes, *position: bytes) -> JsonObject:
    view: JsonObject = {"bone": bone, "position": _vector_view(position)}
    _show_raw(view, "raw_0x02", raw)
    return view


def _normal_view(bone: int, raw: bytes, x: bytes, y: bytes, z: bytes, vertex: int, tail: bytes) -> JsonObject:
    view: JsonObject = {"bone": bone, "normal": _vector_view((x, y, z)), "vertex": vertex}
    _show_raw(view, "raw_0x02", raw)
    _show_raw(view, "raw_0x12", tail)
    return view


def _triangle_view(polygon: int, raw: bytes, *rest: Any) -> JsonObject:
    view: JsonObject = {
        "polygon": polygon,
        "vertices": list(rest[:CORNERS]),
        "normals": list(rest[CORNERS : 2 * CORNERS]),
        "uvs": list(rest[2 * CORNERS : 3 * CORNERS]),
    }
    _show_raw(view, "raw_0x01", raw)
    _show_raw(view, "raw_0x1a", rest[3 * CORNERS])
    return view


def _vectors_view(block: bytes, layout: Struct) -> list[JsonValue]:
    return [_vector_view(parts) for parts in layout.iter_unpack(block)]


def _mesh_view(mesh: Mesh, encoding: str) -> JsonObject:
    return {
        "texture": mesh.texture,
        "texture_name": text_view(mesh.texture_name, encoding),
        "vertices": [_vertex_view(*item) for item in VERTEX.iter_unpack(mesh.vertices)],
        "normals": [_normal_view(*item) for item in NORMAL.iter_unpack(mesh.normals)],
        "uvs": _vectors_view(mesh.uvs, UV),
        "triangles": [_triangle_view(*item) for item in TRIANGLE.iter_unpack(mesh.triangles)],
    }


def _action_view(action: Action) -> JsonObject:
    view: JsonObject = {"keys": action.keys, "lock": action.lock}
    if action.lock:
        view["positions"] = _vectors_view(action.positions, KEY)
    return view


def _bone_actions_view(keys: bytes, actions: tuple[Action, ...]) -> Iterator[JsonValue]:
    offset = 0
    for action in actions:
        size = action.keys * KEY_SIZE
        positions = _vectors_view(keys[offset : offset + size], KEY)
        rotations = _vectors_view(keys[offset + size : offset + 2 * size], KEY)
        yield {"positions": positions, "rotations": rotations}
        offset += 2 * size


def _bone_view(bone: Bone, actions: tuple[Action, ...], encoding: str) -> JsonObject:
    if bone.dummy:
        return {"dummy": bone.dummy}
    return {
        "name": text_view(bone.name, encoding),
        "parent": bone.parent,
        "actions": list(_bone_actions_view(bone.keys, actions)),
    }


def _name_view(name: bytes, encoding: str) -> JsonValue:
    if b"\0" in name:
        return text_view(name, encoding)
    text = decode_text(name, encoding)
    return {"hex": name.hex()} if text is None else text


def model_view(model: Model) -> JsonObject:
    view: JsonObject = {
        "format_version": VIEW_VERSION,
        "format": MODEL_FORMAT_NAME,
        "encoding": model.encoding,
        "version": model.version,
        "name": _name_view(model.name, model.encoding),
        "meshes": [_mesh_view(mesh, model.encoding) for mesh in model.meshes],
        "actions": [_action_view(action) for action in model.actions],
        "bones": [_bone_view(bone, model.actions, model.encoding) for bone in model.bones],
        "trailing": model.trailing.hex(),
    }
    if model.after_block:
        view["after_block"] = model.after_block.hex()
    return view


def _members(
    value: JsonValue, pointer: str, required: frozenset[str], optional: frozenset[str] = frozenset()
) -> JsonObject:
    if isinstance(value, dict) and required <= value.keys() <= required | optional:
        return value
    names = ", ".join(sorted(required))
    extra = f" (and, when not zero, {', '.join(sorted(optional))})" if optional else ""
    raise invalid(f"Expected an object with exactly these members: {names}{extra}", pointer)


def _items(value: JsonValue, pointer: str, count: int | None = None) -> list[JsonValue]:
    if not isinstance(value, list) or len(value) > MAX_ITEMS:
        raise invalid(f"Expected a list of at most {MAX_ITEMS:,} items", pointer)
    if count is not None and len(value) != count:
        raise invalid(f"Expected a list of {count:,} items, this one has {len(value):,}", pointer)
    return value


def _integer(value: JsonValue, lowest: int, highest: int, pointer: str) -> int:
    if type(value) is not int or not lowest <= value <= highest:
        raise invalid(f"Expected an integer from {lowest} to {highest}", pointer)
    return value


def _int16(value: JsonValue, pointer: str) -> int:
    return _integer(value, INT16_MIN, INT16_MAX, pointer)


def _raw_bytes(item: JsonObject, name: str, size: int, pointer: str) -> bytes:
    if name not in item:
        return bytes(size)
    return hex_bytes(item[name], size, f"{pointer}/{name}")


def _vector_parts(value: JsonValue, width: int, pointer: str) -> list[bytes]:
    if not isinstance(value, list) or len(value) != width:
        raise invalid(f"Expected a list of {width} numbers", pointer)
    return [float_bytes(number, f"{pointer}/{index}") for index, number in enumerate(value)]


def _plain_vectors(vectors: list[Any], width: int) -> bytes | None:
    if not all(type(vector) is list and len(vector) == width for vector in vectors):
        return None
    numbers = list(chain.from_iterable(vectors))
    if set(map(type, numbers)) - {float} or not all(map(isfinite, numbers)):
        return None
    try:
        return Struct(f"<{len(numbers)}f").pack(*numbers)
    except OverflowError:
        return None


def _vectors_bytes(value: JsonValue, width: int, pointer: str, count: int | None = None) -> bytes:
    vectors = _items(value, pointer, count)
    plain = _plain_vectors(vectors, width)
    if plain is not None:
        return plain
    return b"".join(
        b"".join(_vector_parts(vector, width, f"{pointer}/{index}")) for index, vector in enumerate(vectors)
    )


def _vertex_bytes(value: JsonValue, pointer: str) -> bytes:
    item = _members(value, pointer, VERTEX_MEMBERS, frozenset({"raw_0x02"}))
    return VERTEX.pack(
        _int16(item["bone"], f"{pointer}/bone"),
        _raw_bytes(item, "raw_0x02", RAW_0X02_SIZE, pointer),
        *_vector_parts(item["position"], POSITION_WIDTH, f"{pointer}/position"),
    )


def _normal_bytes(value: JsonValue, pointer: str) -> bytes:
    item = _members(value, pointer, NORMAL_MEMBERS, frozenset({"raw_0x02", "raw_0x12"}))
    return NORMAL.pack(
        _int16(item["bone"], f"{pointer}/bone"),
        _raw_bytes(item, "raw_0x02", RAW_0X02_SIZE, pointer),
        *_vector_parts(item["normal"], POSITION_WIDTH, f"{pointer}/normal"),
        _int16(item["vertex"], f"{pointer}/vertex"),
        _raw_bytes(item, "raw_0x12", RAW_0X12_SIZE, pointer),
    )


def _corners(value: JsonValue, pointer: str) -> list[int]:
    if not isinstance(value, list) or len(value) != CORNERS:
        raise invalid(f"Expected a list of {CORNERS} indices", pointer)
    return [_int16(index, f"{pointer}/{corner}") for corner, index in enumerate(value)]


def _triangle_bytes(value: JsonValue, pointer: str) -> bytes:
    item = _members(value, pointer, TRIANGLE_MEMBERS, frozenset({"raw_0x01", "raw_0x1a"}))
    return TRIANGLE.pack(
        _integer(item["polygon"], 0, BYTE_MAX, f"{pointer}/polygon"),
        _raw_bytes(item, "raw_0x01", RAW_0X01_SIZE, pointer),
        *_corners(item["vertices"], f"{pointer}/vertices"),
        *_corners(item["normals"], f"{pointer}/normals"),
        *_corners(item["uvs"], f"{pointer}/uvs"),
        _raw_bytes(item, "raw_0x1a", RAW_0X1A_SIZE, pointer),
    )


def _block(value: JsonValue, pointer: str, element: Callable[[JsonValue, str], bytes]) -> bytes:
    return b"".join(element(item, f"{pointer}/{index}") for index, item in enumerate(_items(value, pointer)))


def _mesh(value: JsonValue, encoding: str, pointer: str) -> Mesh:
    item = _members(value, pointer, MESH_MEMBERS)
    return Mesh(
        _int16(item["texture"], f"{pointer}/texture"),
        _block(item["vertices"], f"{pointer}/vertices", _vertex_bytes),
        _block(item["normals"], f"{pointer}/normals", _normal_bytes),
        _vectors_bytes(item["uvs"], UV_WIDTH, f"{pointer}/uvs"),
        _block(item["triangles"], f"{pointer}/triangles", _triangle_bytes),
        text_bytes(item["texture_name"], NAME_SIZE, encoding, f"{pointer}/texture_name"),
    )


def _action(value: JsonValue, pointer: str) -> Action:
    item = _members(value, pointer, ACTION_MEMBERS, frozenset({"positions"}))
    keys = _integer(item["keys"], 0, MAX_ITEMS, f"{pointer}/keys")
    lock = _integer(item["lock"], 0, BYTE_MAX, f"{pointer}/lock")
    if not lock:
        if "positions" in item:
            raise invalid("An action has positions only when its lock is not 0", f"{pointer}/positions")
        return Action(keys, lock)
    if "positions" not in item:
        raise invalid("An action whose lock is not 0 has positions", pointer)
    return Action(keys, lock, _vectors_bytes(item["positions"], POSITION_WIDTH, f"{pointer}/positions", keys))


def _bone_keys(value: JsonValue, actions: tuple[Action, ...], pointer: str) -> bytes:
    parts: list[bytes] = []
    for index, (item, action) in enumerate(zip(_items(value, pointer, len(actions)), actions, strict=True)):
        place = f"{pointer}/{index}"
        lists = _members(item, place, BONE_ACTION_MEMBERS)
        parts.append(_vectors_bytes(lists["positions"], POSITION_WIDTH, f"{place}/positions", action.keys))
        parts.append(_vectors_bytes(lists["rotations"], POSITION_WIDTH, f"{place}/rotations", action.keys))
    return b"".join(parts)


def _bone(value: JsonValue, actions: tuple[Action, ...], encoding: str, pointer: str) -> Bone:
    if isinstance(value, dict) and value.keys() == DUMMY_MEMBERS:
        return Bone(_integer(value["dummy"], 1, BYTE_MAX, f"{pointer}/dummy"))
    item = _members(value, pointer, BONE_MEMBERS)
    return Bone(
        0,
        text_bytes(item["name"], NAME_SIZE, encoding, f"{pointer}/name"),
        _int16(item["parent"], f"{pointer}/parent"),
        _bone_keys(item["actions"], actions, f"{pointer}/actions"),
    )


def _bones(value: JsonValue, actions: tuple[Action, ...], encoding: str) -> tuple[Bone, ...]:
    items = _items(value, "/bones")
    real_bones = sum(1 for item in items if not (isinstance(item, dict) and "dummy" in item))
    if real_bones * len(actions) > MAX_POSED_BONE_ACTIONS:
        raise invalid(
            f"{real_bones:,} bones times {len(actions):,} actions pass the {MAX_POSED_BONE_ACTIONS:,} the client poses",
            "/bones",
        )
    return tuple(_bone(item, actions, encoding, f"/bones/{index}") for index, item in enumerate(items))


def _name_bytes(value: JsonValue, encoding: str) -> bytes:
    if isinstance(value, str):
        return encode_fixed_text(value, encoding, NAME_SIZE, "/name", "name")
    return text_bytes(value, NAME_SIZE, encoding, "/name")


def _after_block(view: JsonObject, version: int) -> bytes:
    after_block = read_hex(view.get("after_block", ""), "/after_block")
    if after_block and version != ENCRYPTED_VERSION:
        raise invalid(f"Only a version {ENCRYPTED_VERSION} model has bytes after its block", "/after_block")
    return after_block


def model_from_view(view: JsonObject) -> Model:
    encoding = view_encoding(view, MODEL_FORMAT_NAME)
    root = _members(view, "", ROOT_MEMBERS, frozenset({"after_block"}))
    version = _integer(root["version"], 0, BYTE_MAX, "/version")
    meshes = tuple(
        _mesh(item, encoding, f"/meshes/{index}") for index, item in enumerate(_items(root["meshes"], "/meshes"))
    )
    actions = tuple(
        _action(item, f"/actions/{index}") for index, item in enumerate(_items(root["actions"], "/actions"))
    )
    return Model(
        version,
        _name_bytes(root["name"], encoding),
        meshes,
        actions,
        _bones(root["bones"], actions, encoding),
        read_hex(root["trailing"], "/trailing"),
        _after_block(root, version),
        encoding,
    )


def _leaf(pointer: str, kind: str, size: int, offset: int = 0, note: str = "") -> FieldInfo:
    return FieldInfo(pointer, kind, f"{size} bytes{note}", REFERENCE, offset)


def _vector_leaf(pointer: str, width: int, offset: int = 0) -> FieldInfo:
    return _leaf(f"{pointer}/<0..{width - 1}>", "f32", FLOAT32.size, offset)


def _corner_leaf(pointer: str, offset: int) -> FieldInfo:
    return _leaf(f"{pointer}/<0..{CORNERS - 1}>", "int", 2, offset)


_VERTEX: Final = "/meshes/<N>/vertices/<N>"
_NORMAL: Final = "/meshes/<N>/normals/<N>"
_TRIANGLE: Final = "/meshes/<N>/triangles/<N>"
_BONE_ACTION: Final = "/bones/<N>/actions/<N>"

MODEL_FIELDS: Final[tuple[FieldInfo, ...]] = (
    _leaf("/version", "uint", 1, 3, f"; {ENCRYPTED_VERSION} is K16, any other is plain; only build changes it"),
    _leaf("/name", "text", NAME_SIZE, 0, "; may fill them with no 0"),
    _leaf("/meshes/<N>/texture", "int", 2, 8),
    _leaf("/meshes/<N>/texture_name", "text", NAME_SIZE),
    _leaf(f"{_VERTEX}/bone", "int", 2, 0),
    _leaf(f"{_VERTEX}/raw_0x02", "bytes", RAW_0X02_SIZE, 2, "; shown when not zero"),
    _vector_leaf(f"{_VERTEX}/position", POSITION_WIDTH, 4),
    _leaf(f"{_NORMAL}/bone", "int", 2, 0),
    _leaf(f"{_NORMAL}/raw_0x02", "bytes", RAW_0X02_SIZE, 2, "; shown when not zero"),
    _vector_leaf(f"{_NORMAL}/normal", POSITION_WIDTH, 4),
    _leaf(f"{_NORMAL}/vertex", "int", 2, 16),
    _leaf(f"{_NORMAL}/raw_0x12", "bytes", RAW_0X12_SIZE, 18, "; shown when not zero"),
    _vector_leaf("/meshes/<N>/uvs/<N>", UV_WIDTH),
    _leaf(f"{_TRIANGLE}/polygon", "uint", 1, 0),
    _leaf(f"{_TRIANGLE}/raw_0x01", "bytes", RAW_0X01_SIZE, 1, "; shown when not zero"),
    _corner_leaf(f"{_TRIANGLE}/vertices", 2),
    _corner_leaf(f"{_TRIANGLE}/normals", 10),
    _corner_leaf(f"{_TRIANGLE}/uvs", 18),
    _leaf(f"{_TRIANGLE}/raw_0x1a", "bytes", RAW_0X1A_SIZE, 26, "; shown when not zero"),
    _leaf("/actions/<N>/keys", "int", 2, 0, f"; 0 to {MAX_ITEMS}"),
    _leaf("/actions/<N>/lock", "uint", 1, 2, "; positions follow when it is not 0"),
    _vector_leaf("/actions/<N>/positions/<N>", POSITION_WIDTH),
    _leaf("/bones/<N>/dummy", "uint", 1, 0, "; 1 to 255, a bone with no other member"),
    _leaf("/bones/<N>/name", "text", NAME_SIZE, 1),
    _leaf("/bones/<N>/parent", "int", 2, 33),
    _vector_leaf(f"{_BONE_ACTION}/positions/<N>", POSITION_WIDTH),
    _vector_leaf(f"{_BONE_ACTION}/rotations/<N>", POSITION_WIDTH),
    FieldInfo("/trailing", "bytes", "any length, after the last bone", REFERENCE),
    FieldInfo("/after_block", "bytes", f"any length, only with version {ENCRYPTED_VERSION}", REFERENCE),
)
