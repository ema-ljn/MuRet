import struct
from collections.abc import Sequence
from typing import Final

from pygltflib import (
    ARRAY_BUFFER,
    FLOAT,
    GLTF2,
    MASK,
    OPAQUE,
    TRIANGLES,
    UNSIGNED_SHORT,
    Accessor,
    Animation,
    AnimationChannel,
    AnimationChannelTarget,
    AnimationSampler,
    Asset,
    Attributes,
    Buffer,
    BufferView,
    Image,
    Material,
    Mesh,
    Node,
    PbrMetallicRoughness,
    Primitive,
    Scene,
    Skin,
    Texture,
    TextureInfo,
)

from mutools import __version__
from mutools.app.gltf_rig import PNG, Rig, RigAnimation, RigMesh, RigNode
from mutools.core.affine import SAME_SIZE

ALIGNMENT: Final = 4
GENERATOR: Final = f"mutools {__version__}"
ALPHA_CUTOFF: Final = 0.25
WIDTHS: Final = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4, "MAT4": 16}
CHANNEL_TYPES: Final = {"translation": "VEC3", "rotation": "VEC4", "scale": "VEC3"}
CORNER_JOINT: Final = (0, 0, 0)
CORNER_WEIGHT: Final = (1.0, 0.0, 0.0, 0.0)


class _Buffer:
    def __init__(self, gltf: GLTF2) -> None:
        self._gltf = gltf
        self._data = bytearray()

    def view(self, data: bytes, target: int | None = None) -> int:
        self._data += b"\0" * (-len(self._data) % ALIGNMENT)
        self._gltf.bufferViews.append(
            BufferView(buffer=0, byteOffset=len(self._data), byteLength=len(data), target=target)
        )
        self._data += data
        return len(self._gltf.bufferViews) - 1

    def floats(self, values: Sequence[float], kind: str, *, vertices: bool = False, bounds: bool = False) -> int:
        width = WIDTHS[kind]
        count = len(values) // width
        data = struct.pack(f"<{len(values)}f", *values)
        accessor = Accessor(
            bufferView=self.view(data, ARRAY_BUFFER if vertices else None),
            componentType=FLOAT,
            count=count,
            type=kind,
        )
        if bounds:
            columns = [values[axis::width] for axis in range(width)]
            accessor.min = [min(column) for column in columns]
            accessor.max = [max(column) for column in columns]
        self._gltf.accessors.append(accessor)
        return len(self._gltf.accessors) - 1

    def shorts(self, values: Sequence[int], kind: str) -> int:
        data = struct.pack(f"<{len(values)}H", *values)
        view = self.view(data, ARRAY_BUFFER)
        accessor = Accessor(bufferView=view, componentType=UNSIGNED_SHORT, count=len(values) // WIDTHS[kind], type=kind)
        self._gltf.accessors.append(accessor)
        return len(self._gltf.accessors) - 1

    def blob(self) -> bytes:
        self._data += b"\0" * (-len(self._data) % ALIGNMENT)
        return bytes(self._data)


def _node(node: RigNode, children: list[int]) -> Node:
    return Node(
        name=node.name,
        translation=list(node.translation),
        rotation=list(node.rotation),
        scale=None if node.scale == SAME_SIZE else list(node.scale),
        children=children,
    )


def _material(gltf: GLTF2, rig: Rig, image: int) -> int:
    see_through = rig.images[image].mime == PNG
    gltf.materials.append(
        Material(
            pbrMetallicRoughness=PbrMetallicRoughness(
                baseColorTexture=TextureInfo(index=image), metallicFactor=0.0, roughnessFactor=1.0
            ),
            alphaMode=MASK if see_through else OPAQUE,
            alphaCutoff=ALPHA_CUTOFF if see_through else None,
            doubleSided=True,
        )
    )
    return len(gltf.materials) - 1


def _mesh(gltf: GLTF2, buffer: _Buffer, rig: Rig, mesh: RigMesh) -> int:
    corners = len(mesh.joints)
    attributes = Attributes(
        POSITION=buffer.floats(mesh.positions, "VEC3", vertices=True, bounds=True),
        NORMAL=buffer.floats(mesh.normals, "VEC3", vertices=True),
        TEXCOORD_0=buffer.floats(mesh.uvs, "VEC2", vertices=True),
        JOINTS_0=buffer.shorts([part for joint in mesh.joints for part in (joint, *CORNER_JOINT)], "VEC4"),
        WEIGHTS_0=buffer.floats(CORNER_WEIGHT * corners, "VEC4", vertices=True),
    )
    material = None if mesh.image is None else _material(gltf, rig, mesh.image)
    gltf.meshes.append(
        Mesh(name=mesh.name, primitives=[Primitive(attributes=attributes, material=material, mode=TRIANGLES)])
    )
    return len(gltf.meshes) - 1


def _animation(buffer: _Buffer, animation: RigAnimation) -> Animation:
    written = Animation(name=animation.name)
    inputs: dict[tuple[float, ...], int] = {}
    for channel in animation.channels:
        if channel.times not in inputs:
            inputs[channel.times] = buffer.floats(channel.times, "SCALAR", bounds=True)
        output = buffer.floats(channel.values, CHANNEL_TYPES[channel.path])
        written.samplers.append(
            AnimationSampler(input=inputs[channel.times], output=output, interpolation=channel.interpolation)
        )
        target = AnimationChannelTarget(node=channel.node, path=channel.path)
        written.channels.append(AnimationChannel(sampler=len(written.samplers) - 1, target=target))
    return written


def write_glb(rig: Rig) -> bytes:
    gltf = GLTF2(asset=Asset(generator=GENERATOR), scene=0)
    buffer = _Buffer(gltf)
    children: list[list[int]] = [[] for _ in rig.nodes]
    for index, node in enumerate(rig.nodes):
        if node.parent is not None:
            children[node.parent].append(index)
    gltf.nodes = [_node(node, children[index]) for index, node in enumerate(rig.nodes)]
    roots = [index for index, node in enumerate(rig.nodes) if node.parent is None]
    for image in rig.images:
        view = buffer.view(image.data)
        gltf.images.append(Image(bufferView=view, mimeType=image.mime))
        gltf.textures.append(Texture(source=len(gltf.images) - 1))
    if rig.joints:
        binds = buffer.floats([number for matrix in rig.inverse_binds for number in matrix], "MAT4")
        gltf.skins.append(Skin(inverseBindMatrices=binds, joints=list(rig.joints), skeleton=roots[0]))
    for mesh in rig.meshes:
        index = _mesh(gltf, buffer, rig, mesh)
        roots.append(len(gltf.nodes))
        gltf.nodes.append(Node(name=mesh.name, mesh=index, skin=0 if rig.joints else None))
    gltf.animations = [_animation(buffer, animation) for animation in rig.animations]
    gltf.scenes = [Scene(nodes=roots)]
    blob = buffer.blob()
    gltf.buffers = [Buffer(byteLength=len(blob))]
    gltf.set_binary_blob(blob)
    return b"".join(gltf.save_to_bytes())
