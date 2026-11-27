from collections.abc import Iterable, Iterator
from struct import Struct
from typing import Final

from mutools.core.documents.model import MAX_BONES, NAME_SIZE, NORMAL_SIZE, UV_SIZE, VERTEX_SIZE, Mesh, Model
from mutools.core.findings import Finding, Severity
from mutools.core.formats.model_textures import texture_text, texture_use
from mutools.core.formats.record_fields import text_bytes
from mutools.core.json_types import JsonValue
from mutools.core.pointer import parse_pointer

VERTEX_BONE: Final = Struct("<h14x")
NORMAL_BONE_AND_VERTEX: Final = Struct("<h14xh2x")
TRIANGLE_CORNERS: Final = Struct("<Bx4h4h4h38x")
CORNERS: Final = 4
CORNER_KINDS: Final = ("vertices", "normals", "uvs")


def _out_of_range(pointer: str, message: str) -> Finding:
    return Finding("OUT_OF_RANGE", Severity.ERROR, pointer, message)


def _outside(values: Iterable[int], size: int) -> tuple[int, int, int] | None:
    bad = [(index, value) for index, value in enumerate(values) if not 0 <= value < size]
    return (len(bad), *bad[0]) if bad else None


def _index_finding(place: str, member: str, what: str, values: Iterable[int], size: int) -> Iterator[Finding]:
    outside = _outside(values, size)
    if outside is not None:
        count, index, value = outside
        message = f"{count:,} {what} outside 0 to {size - 1}; the first is {value}"
        yield _out_of_range(f"{place}/{index}/{member}", message)


def _corner_findings(place: str, mesh: Mesh) -> Iterator[Finding]:
    sizes = (len(mesh.vertices) // VERTEX_SIZE, len(mesh.normals) // NORMAL_SIZE, len(mesh.uvs) // UV_SIZE)
    counts = [0] * len(CORNER_KINDS)
    firsts: list[tuple[int, int, int] | None] = [None] * len(CORNER_KINDS)
    for index, (polygon, *corners) in enumerate(TRIANGLE_CORNERS.iter_unpack(mesh.triangles)):
        used = min(polygon, CORNERS)
        for kind, size in enumerate(sizes):
            start = kind * CORNERS
            for corner, value in enumerate(corners[start : start + used]):
                if not 0 <= value < size:
                    counts[kind] += 1
                    firsts[kind] = firsts[kind] or (index, corner, value)
                    break
    for kind, first in enumerate(firsts):
        if first is not None:
            index, corner, value = first
            name = CORNER_KINDS[kind]
            message = (
                f"{counts[kind]:,} triangles have an index into the mesh's {name} outside 0 to {sizes[kind] - 1}; "
                f"the first is {value}"
            )
            yield _out_of_range(f"{place}/triangles/{index}/{name}/{corner}", message)


def texture_index_finding(mesh_index: int, texture: int, meshes: int) -> Finding | None:
    if 0 <= texture < meshes:
        return None
    return _out_of_range(f"/meshes/{mesh_index}/texture", f"Texture {texture} is outside the meshes, 0 to {meshes - 1}")


def role_finding(mesh_index: int, name: bytes, before: bytes, encoding: str) -> Finding | None:
    new_text, old_text = texture_text(name, encoding), texture_text(before, encoding)
    if new_text is None or old_text is None:
        return None
    new, old = texture_use(new_text), texture_use(old_text)
    changes = []
    if new.loader != old.loader:
        changes.append(f"the client opens {new.file} where it opened {old.file}")
    if new.slot != old.slot:
        changes.append(f"the slot was {old.slot or 'none'} and is {new.slot or 'none'}")
    if new.script != old.script:
        changes.append(f"the script was {old.script or 'none'} and is {new.script or 'none'}")
    if not changes:
        return None
    message = f"{new_text} changes what the client does with the mesh: {'; '.join(changes)}"
    return Finding("TEXTURE_ROLE_CHANGED", Severity.WARNING, f"/meshes/{mesh_index}/texture_name", message)


def parent_finding(index: int, parent: int, bones: int) -> Finding | None:
    pointer = f"/bones/{index}/parent"
    if not -1 <= parent < bones:
        return _out_of_range(pointer, f"Parent {parent} is outside -1 to {bones - 1}")
    if parent >= index:
        message = f"Bone {index} hangs from bone {parent}, and the client poses a bone after its parent"
        return Finding("PARENT_AFTER_CHILD", Severity.WARNING, pointer, message)
    return None


def _mesh_findings(index: int, mesh: Mesh, model: Model, original: Model | None) -> Iterator[Finding]:
    place = f"/meshes/{index}"
    bones = len(model.bones)
    vertices = len(mesh.vertices) // VERTEX_SIZE
    vertex_bones = (bone for (bone,) in VERTEX_BONE.iter_unpack(mesh.vertices))
    yield from _index_finding(f"{place}/vertices", "bone", "vertices name a bone", vertex_bones, bones)
    normals = list(NORMAL_BONE_AND_VERTEX.iter_unpack(mesh.normals))
    normal_bones = (bone for bone, _ in normals)
    yield from _index_finding(f"{place}/normals", "bone", "normals name a bone", normal_bones, bones)
    normal_vertices = (vertex for _, vertex in normals)
    yield from _index_finding(f"{place}/normals", "vertex", "normals name a vertex", normal_vertices, vertices)
    yield from _corner_findings(place, mesh)
    findings = [texture_index_finding(index, mesh.texture, len(model.meshes))]
    if (
        original is not None
        and index < len(original.meshes)
        and mesh.texture_name != original.meshes[index].texture_name
    ):
        findings.append(role_finding(index, mesh.texture_name, original.meshes[index].texture_name, model.encoding))
    yield from (finding for finding in findings if finding is not None)


def _bone_findings(model: Model) -> Iterator[Finding]:
    bones = len(model.bones)
    if bones > MAX_BONES:
        yield _out_of_range("/bones", f"{bones} bones, and the client poses at most {MAX_BONES}")
    for index, bone in enumerate(model.bones):
        finding = None if bone.dummy else parent_finding(index, bone.parent, bones)
        if finding is not None:
            yield finding


def check_model(model: Model, original: Model | None) -> tuple[Finding, ...]:
    findings = [
        finding for index, mesh in enumerate(model.meshes) for finding in _mesh_findings(index, mesh, model, original)
    ]
    return (*findings, *_bone_findings(model))


def check_model_value(model: Model, pointer: str, value: JsonValue) -> tuple[Finding, ...]:
    finding: Finding | None = None
    match parse_pointer(pointer):
        case ("meshes", mesh, "texture") if type(value) is int:
            finding = texture_index_finding(int(mesh), value, len(model.meshes))
        case ("meshes", mesh, "texture_name"):
            name = text_bytes(value, NAME_SIZE, model.encoding, pointer)
            finding = role_finding(int(mesh), name, model.meshes[int(mesh)].texture_name, model.encoding)
        case ("bones", bone, "parent") if type(value) is int:
            finding = parent_finding(int(bone), value, len(model.bones))
    return () if finding is None else (finding,)
