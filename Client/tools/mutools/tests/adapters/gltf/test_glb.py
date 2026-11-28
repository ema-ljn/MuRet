import json
import struct
from pathlib import Path
from typing import Any

import pytest

from mutools.adapters.gltf.reader import read_glb
from mutools.adapters.gltf.writer import write_glb
from mutools.app.gltf_animation import animation_action
from mutools.app.gltf_rig import ChosenAction, Rig, RigImage, SkinnedMesh, model_rig
from mutools.app.model_scene import model_scene
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.action_file import read_action_file, with_action
from mutools.core.formats.model_view import model_from_view
from mutools.core.json_types import JsonObject
from tests.models import CODEC, at, view_of

RIDER = "skill/Rider01.bmd"
PLAYER = "player/Player.bmd"
HEADER = struct.Struct("<4sII")
CHUNK = struct.Struct("<II")
JSON_CHUNK = 0x4E4F534A
BIN_CHUNK = 0x004E4942
CLOSE = 1e-6


def rig_of(view: JsonObject, *, meshes: bool = False) -> Rig:
    skinned = [
        SkinnedMesh(f"mesh {index}", mesh, RigImage(b"\x89PNG", "image/png"))
        for index, mesh in enumerate(model_scene(view).meshes)
    ]
    actions = at(view, "/actions")
    return model_rig(view, skinned if meshes else [], [ChosenAction(index, "Stop") for index in range(len(actions))])


def glb(document: dict[str, Any], blob: bytes = b"") -> bytes:
    text = json.dumps(document).encode()
    text += b" " * (-len(text) % 4)
    chunks = CHUNK.pack(len(text), JSON_CHUNK) + text
    if blob:
        chunks += CHUNK.pack(len(blob), BIN_CHUNK) + blob
    return HEADER.pack(b"glTF", 2, HEADER.size + len(chunks)) + chunks


def document_of(data: bytes) -> dict[str, Any]:
    size, _ = CHUNK.unpack_from(data, HEADER.size)
    found: dict[str, Any] = json.loads(data[HEADER.size + CHUNK.size : HEADER.size + CHUNK.size + size])
    return found


def refused(data: bytes) -> MutoolsError:
    with pytest.raises(MutoolsError) as caught:
        read_glb(data)
    return caught.value


@pytest.fixture
def rider(reference_data: Path) -> JsonObject:
    return view_of(reference_data, RIDER)


def test_a_written_glb_says_so_in_its_header(rider: JsonObject) -> None:
    data = write_glb(rig_of(rider, meshes=True))

    assert HEADER.unpack_from(data) == (b"glTF", 2, len(data))


def test_a_written_glb_skins_its_meshes(rider: JsonObject) -> None:
    document = document_of(write_glb(rig_of(rider, meshes=True)))

    skinned = [node for node in document["nodes"] if "mesh" in node]
    assert [node["skin"] for node in skinned] == [0, 0]
    assert len(document["skins"][0]["joints"]) == len(document["nodes"]) - len(skinned)


def test_a_see_through_texture_is_masked(rider: JsonObject) -> None:
    document = document_of(write_glb(rig_of(rider, meshes=True)))

    assert {material["alphaMode"] for material in document["materials"]} == {"MASK"}


def test_what_is_written_is_read_back(rider: JsonObject) -> None:
    rig = rig_of(rider)

    found = read_glb(write_glb(rig))

    assert [(node.name, node.parent) for node in found.nodes] == [(node.name, node.parent) for node in rig.nodes]
    assert found.joints == rig.joints
    assert [animation.name for animation in found.animations] == [animation.name for animation in rig.animations]
    for written, read in zip(rig.animations[2].channels, found.animations[2].channels, strict=True):
        assert (read.node, read.path, read.interpolation) == (written.node, written.path, written.interpolation)
        assert read.values == pytest.approx(written.values, abs=CLOSE)
    assert found.inverse_binds[0] == pytest.approx(rig.inverse_binds[0], abs=CLOSE)


def test_every_action_of_the_player_comes_back_byte_for_byte(reference_data: Path) -> None:
    original = (reference_data / PLAYER).read_bytes()
    view = view_of(reference_data, PLAYER)
    rig = read_glb(write_glb(rig_of(view)))

    for action in range(len(at(view, "/actions"))):
        action_file = read_action_file(animation_action(rig, action, view, action))
        assert with_action(view, action_file, action) == view, f"action {action}"
    assert CODEC.encode(model_from_view(view)) == original


def test_a_node_s_matrix_is_read_as_its_parts() -> None:
    matrix = [2.0, 0, 0, 0, 0, 2.0, 0, 0, 0, 0, 2.0, 0, 1.0, 2.0, 3.0, 1.0]

    (node,) = read_glb(glb({"asset": {"version": "2.0"}, "nodes": [{"name": "Bip01", "matrix": matrix}]})).nodes

    assert (node.translation, node.scale) == ((1.0, 2.0, 3.0), (2.0, 2.0, 2.0))


def test_a_file_that_is_not_a_glb_is_refused() -> None:
    assert refused(b'{"asset": {"version": "2.0"}}').code is ErrorCode.CORRUPT_FILE


def test_another_version_of_glb_is_refused() -> None:
    data = bytearray(glb({"asset": {"version": "2.0"}}))
    data[4] = 1

    assert refused(bytes(data)).code is ErrorCode.CORRUPT_FILE


def test_a_glb_cut_short_is_refused() -> None:
    assert refused(glb({"asset": {"version": "2.0"}})[:-4]).code is ErrorCode.CORRUPT_FILE


def test_a_node_with_two_parents_is_refused() -> None:
    nodes = [{"children": [2]}, {"children": [2]}, {"name": "Bip01"}]

    assert refused(glb({"nodes": nodes})).code is ErrorCode.CORRUPT_FILE


def test_data_outside_the_glb_is_refused() -> None:
    document = {
        "nodes": [{"name": "Bip01"}],
        "buffers": [{"uri": "data.bin", "byteLength": 4}],
        "bufferViews": [{"buffer": 0, "byteLength": 4}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 1, "type": "SCALAR"}],
        "animations": [
            {
                "channels": [{"sampler": 0, "target": {"node": 0, "path": "rotation"}}],
                "samplers": [{"input": 0, "output": 0}],
            }
        ],
    }

    assert refused(glb(document)).code is ErrorCode.INVALID_VALUE


def test_an_animation_of_whole_numbers_is_refused() -> None:
    document = {
        "nodes": [{"name": "Bip01"}],
        "buffers": [{"byteLength": 8}],
        "bufferViews": [{"buffer": 0, "byteLength": 8}],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 1, "type": "SCALAR"},
            {"bufferView": 0, "componentType": 5123, "count": 1, "type": "VEC4"},
        ],
        "animations": [
            {
                "channels": [{"sampler": 0, "target": {"node": 0, "path": "rotation"}}],
                "samplers": [{"input": 0, "output": 1}],
            }
        ],
    }

    assert refused(glb(document, b"\0" * 8)).code is ErrorCode.INVALID_VALUE


def test_a_channel_of_morph_weights_is_left_out() -> None:
    document = {
        "nodes": [{"name": "Bip01"}],
        "animations": [{"name": "Smile", "channels": [{"sampler": 0, "target": {"node": 0, "path": "weights"}}]}],
    }

    assert read_glb(glb(document)).animations[0].channels == ()
