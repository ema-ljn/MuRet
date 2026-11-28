import json
import struct
from itertools import pairwise
from typing import Final

from mutools.app.gltf_rig import LINEAR, ChannelPath, Interpolation, Rig, RigAnimation, RigChannel, RigNode
from mutools.core.affine import MATRIX_SIZE, ORIGIN, SAME_SIZE, UNTURNED, Affine, normalized, vector
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.record_fields import invalid
from mutools.core.json_types import JsonObject, JsonValue

HEADER: Final = struct.Struct("<4sII")
CHUNK: Final = struct.Struct("<II")
GLB_MAGIC: Final = b"glTF"
GLB_VERSION: Final = 2
JSON_CHUNK: Final = 0x4E4F534A
BIN_CHUNK: Final = 0x004E4942
FLOAT: Final = 5126
FLOAT_SIZE: Final = 4
WIDTHS: Final = {"SCALAR": 1, "VEC3": 3, "VEC4": 4, "MAT4": MATRIX_SIZE}
TARGETS: Final[dict[str, tuple[ChannelPath, str]]] = {
    "translation": ("translation", "VEC3"),
    "rotation": ("rotation", "VEC4"),
    "scale": ("scale", "VEC3"),
}
INTERPOLATIONS: Final[dict[str, Interpolation]] = {"STEP": "STEP", "LINEAR": "LINEAR", "CUBICSPLINE": "CUBICSPLINE"}
CUBIC_PARTS: Final = 3
VECTOR_WIDTH: Final = 3
QUATERNION_WIDTH: Final = 4


def _corrupt(message: str) -> MutoolsError:
    return MutoolsError(ErrorCode.CORRUPT_FILE, message)


def _document(chunk: bytes) -> JsonObject:
    try:
        document = json.loads(chunk.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as error:
        raise _corrupt(f"The .glb's JSON chunk is not JSON: {error}") from error
    if not isinstance(document, dict):
        raise _corrupt("The .glb's JSON chunk is not an object")
    return document


def _chunks(data: bytes) -> tuple[JsonObject, bytes]:
    if len(data) < HEADER.size or not data.startswith(GLB_MAGIC):
        raise _corrupt("Not a glTF binary file: a .glb starts with glTF")
    _, version, length = HEADER.unpack_from(data)
    if version != GLB_VERSION:
        raise _corrupt(f"This is version {version} of glTF's binary files, and mutools reads version {GLB_VERSION}")
    document: JsonObject | None = None
    blob = b""
    offset = HEADER.size
    end = min(length, len(data))
    while offset + CHUNK.size <= end:
        size, kind = CHUNK.unpack_from(data, offset)
        chunk = data[offset + CHUNK.size : offset + CHUNK.size + size]
        if len(chunk) != size:
            raise _corrupt("The .glb ends inside one of its chunks")
        if kind == JSON_CHUNK and document is None:
            document = _document(chunk)
        elif kind == BIN_CHUNK and not blob:
            blob = chunk
        offset += CHUNK.size + size
    if document is None:
        raise _corrupt("The .glb has no JSON chunk")
    return document, blob


def _list(owner: JsonObject, key: str) -> list[JsonValue]:
    value = owner.get(key, [])
    if not isinstance(value, list):
        raise _corrupt(f"The file's {key} is not a list")
    return value


def _item(items: list[JsonValue], index: JsonValue, what: str) -> JsonObject:
    found = items[index] if type(index) is int and 0 <= index < len(items) else None
    if not isinstance(found, dict):
        raise _corrupt(f"The file names {what} {index}, which it does not have")
    return found


def _whole(owner: JsonObject, key: str, default: int | None = None) -> int:
    value = owner.get(key, default)
    if type(value) is not int or value < 0:
        raise _corrupt(f"The file's {key} {value} is not a count")
    return value


def _numbers(owner: JsonObject, key: str, width: int) -> tuple[float, ...] | None:
    value = owner.get(key)
    if value is None:
        return None
    if not isinstance(value, list) or len(value) != width or not all(type(n) in (int, float) for n in value):
        raise _corrupt(f"A node's {key} is not {width} numbers")
    return tuple(float(number) for number in value if isinstance(number, int | float))


class _Data:
    def __init__(self, document: JsonObject, blob: bytes) -> None:
        self._document = document
        self._blob = blob

    def floats(self, index: JsonValue, kind: str) -> tuple[float, ...]:
        accessor = _item(_list(self._document, "accessors"), index, "accessor")
        if accessor.get("type") != kind or accessor.get("componentType") != FLOAT or "sparse" in accessor:
            raise invalid(f"Accessor {index} does not hold {kind} of 32-bit floats, as mutools reads them here", "")
        view = _item(_list(self._document, "bufferViews"), accessor.get("bufferView"), "buffer view")
        buffer = _item(_list(self._document, "buffers"), view.get("buffer"), "buffer")
        if view.get("buffer") != 0 or "uri" in buffer:
            raise invalid("The file keeps its data outside the .glb: export it from Blender as glTF Binary (.glb)", "")
        width = WIDTHS[kind]
        size = width * FLOAT_SIZE
        count = _whole(accessor, "count")
        stride = _whole(view, "byteStride", 0) or size
        start = _whole(view, "byteOffset", 0) + _whole(accessor, "byteOffset", 0)
        end = start + stride * (count - 1) + size if count else start
        if end > len(self._blob) or end > _whole(view, "byteOffset", 0) + _whole(view, "byteLength"):
            raise _corrupt(f"Accessor {index} reaches past the end of its data")
        if stride == size:
            return struct.unpack_from(f"<{count * width}f", self._blob, start)
        rows = (struct.unpack_from(f"<{width}f", self._blob, start + item * stride) for item in range(count))
        return tuple(number for row in rows for number in row)


def _node(node: JsonObject, parent: int | None) -> RigNode:
    name = node.get("name", "")
    text = name if isinstance(name, str) else ""
    matrix = _numbers(node, "matrix", MATRIX_SIZE)
    if matrix is not None:
        placed = Affine.from_columns(matrix)
        return RigNode(text, parent, placed.place, placed.turn(), placed.scale())
    translation = _numbers(node, "translation", VECTOR_WIDTH)
    rotation = _numbers(node, "rotation", QUATERNION_WIDTH)
    scale = _numbers(node, "scale", VECTOR_WIDTH)
    return RigNode(
        text,
        parent,
        ORIGIN if translation is None else vector(translation),
        UNTURNED if rotation is None else normalized(rotation),
        SAME_SIZE if scale is None else vector(scale),
    )


def _nodes(document: JsonObject) -> tuple[RigNode, ...]:
    listed = _list(document, "nodes")
    nodes = [_item(listed, index, "node") for index in range(len(listed))]
    parents: list[int | None] = [None] * len(nodes)
    for index, node in enumerate(nodes):
        for child in _list(node, "children"):
            if type(child) is not int or not 0 <= child < len(nodes):
                raise _corrupt(f"Node {index} names node {child} as a child, which the file does not have")
            if parents[child] is not None or child == index:
                raise _corrupt(f"Node {child} hangs from more than one node")
            parents[child] = index
    return tuple(_node(node, parent) for node, parent in zip(nodes, parents, strict=True))


def _joints(skin: JsonObject, nodes: int) -> tuple[int, ...]:
    joints = tuple(joint for joint in _list(skin, "joints") if type(joint) is int and 0 <= joint < nodes)
    if len(joints) != len(_list(skin, "joints")):
        raise _corrupt("The file's skin names a joint that is not one of its nodes")
    return joints


def _channel(data: _Data, animation: JsonObject, channel: JsonValue, name: str) -> RigChannel | None:
    found = channel if isinstance(channel, dict) else {}
    target = found.get("target")
    if not isinstance(target, dict) or type(target.get("node")) is not int or target.get("path") not in TARGETS:
        return None
    node = target["node"]
    assert isinstance(node, int)
    path, kind = TARGETS[str(target["path"])]
    sampler = _item(_list(animation, "samplers"), found.get("sampler"), "sampler")
    interpolation = INTERPOLATIONS.get(str(sampler.get("interpolation", LINEAR)))
    if interpolation is None:
        raise invalid(f"Animation {name!r} interpolates with {sampler.get('interpolation')}, which glTF lacks", "")
    times = data.floats(sampler.get("input"), "SCALAR")
    values = data.floats(sampler.get("output"), kind)
    parts = CUBIC_PARTS if interpolation == "CUBICSPLINE" else 1
    if not times or len(values) != len(times) * WIDTHS[kind] * parts:
        raise _corrupt(f"A channel of animation {name!r} has {len(times)} times, and its values do not match them")
    if any(later < earlier for earlier, later in pairwise(times)):
        raise _corrupt(f"A channel of animation {name!r} goes back in time")
    return RigChannel(node, path, times, values, interpolation)


def _animation(data: _Data, value: JsonValue, index: int) -> RigAnimation:
    animation = value if isinstance(value, dict) else {}
    name = animation.get("name")
    text = name if isinstance(name, str) else f"animation {index}"
    channels = (_channel(data, animation, channel, text) for channel in _list(animation, "channels"))
    return RigAnimation(text, tuple(channel for channel in channels if channel is not None))


def read_glb(data: bytes) -> Rig:
    """What mutools needs of a .glb: its nodes, its first skin and its animations; meshes and images are left out."""
    document, blob = _chunks(data)
    found = _Data(document, blob)
    nodes = _nodes(document)
    skins = _list(document, "skins")
    skin = _item(skins, 0, "skin") if skins else {}
    binds: tuple[tuple[float, ...], ...] = ()
    if "inverseBindMatrices" in skin:
        numbers = found.floats(skin["inverseBindMatrices"], "MAT4")
        binds = tuple(numbers[start : start + MATRIX_SIZE] for start in range(0, len(numbers), MATRIX_SIZE))
    animations = tuple(
        _animation(found, animation, index) for index, animation in enumerate(_list(document, "animations"))
    )
    return Rig(nodes, _joints(skin, len(nodes)), binds, animations=animations)
