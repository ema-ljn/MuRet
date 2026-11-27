import struct
from pathlib import Path

import pytest

from mutools.core.documents.model import Model
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.model_format import ModelFormat
from mutools.core.ports import CatalogFile
from tests.conftest import MODEL_FILES

CODEC = ModelFormat()
MESH_COUNT_OFFSET = 36
VERTEX_COUNT_OFFSET = 42
SIZE_WORD_OFFSET = 4
EMPTY_ACTION = struct.pack("<hB", 0, 0)
BONE_WITHOUT_KEYS = b"\0" + bytes(32) + struct.pack("<h", -1)
LIES = (
    "only the magic",
    "no size word",
    "a size word past the file",
    "cut inside the last bone",
    "cut inside the first mesh",
    "cut inside the counts",
    "a negative count of meshes",
    "more vertices than bytes",
    "90,000 bones times actions",
)


def decode(data: bytes, name: str = "model.bmd") -> Model:
    return CODEC.decode(data, CatalogFile(name, None, read_by_client=False))


def model_of(reference_data: Path, name: str) -> Model:
    return decode((reference_data / name).read_bytes(), Path(name).name)


def patched(data: bytes, offset: int, patch: bytes) -> bytes:
    return data[:offset] + patch + data[offset + len(patch) :]


def empty_actions(real_bones: int, actions: int) -> bytes:
    counts = struct.pack("<3h", 0, real_bones, actions)
    return b"BMD\x0a" + bytes(32) + counts + EMPTY_ACTION * actions + BONE_WITHOUT_KEYS * real_bones


def lying_file(reference_data: Path, lie: str) -> bytes:
    shine = (reference_data / "Object12/Shine01.bmd").read_bytes()
    object70 = (reference_data / "object35/Object70.bmd").read_bytes()
    return {
        "only the magic": b"BMD",
        "no size word": b"BMD\x0c\x01",
        "a size word past the file": patched(object70, SIZE_WORD_OFFSET, struct.pack("<I", 299)),
        "cut inside the last bone": shine[:389],
        "cut inside the first mesh": shine[:50],
        "cut inside the counts": shine[:40],
        "a negative count of meshes": patched(shine, MESH_COUNT_OFFSET, b"\xff\xff"),
        "more vertices than bytes": patched(shine, VERTEX_COUNT_OFFSET, b"\xff\x7f"),
        "90,000 bones times actions": empty_actions(3, 30_000),
    }[lie]


def test_a_plain_model_reads_its_parts(reference_data: Path) -> None:
    data = (reference_data / "Object12/Shine01.bmd").read_bytes()
    model = decode(data)
    assert (len(data), model.version, model.encoding) == (390, 10, "cp949")
    (mesh,) = model.meshes
    assert mesh.texture == 0
    assert [len(block) for block in (mesh.vertices, mesh.normals, mesh.uvs, mesh.triangles)] == [64, 20, 32, 128]
    assert mesh.texture_name.startswith(b"light2_R.jpg\0")
    (action,) = model.actions
    assert (action.keys, action.lock, action.positions) == (1, 0, b"")
    (bone,) = model.bones
    assert (bone.dummy, bone.parent, len(bone.keys)) == (0, -1, 24)
    assert bone.name.startswith(b"Box01\0")
    assert (model.trailing, model.after_block) == (b"", b"")


def test_an_encrypted_model_reads_its_parts(reference_data: Path) -> None:
    data = (reference_data / "object35/Object70.bmd").read_bytes()
    model = decode(data)
    assert (len(data), struct.unpack_from("<I", data, SIZE_WORD_OFFSET)) == (306, (298,))
    assert model.version == 12
    assert model.name == b"Data2\\Object35\\cr_cmcenter02tr.S"
    (mesh,) = model.meshes
    assert (len(mesh.vertices) // 16, len(mesh.triangles) // 64) == (3, 1)


def test_a_model_with_no_parts(reference_data: Path) -> None:
    data = (reference_data / "Object3/Object25.bmd").read_bytes()
    model = decode(data)
    assert (len(data), model.version) == (42, 0)
    assert (model.meshes, model.actions, model.bones) == ((), (), ())


def test_a_locked_action_keeps_its_positions(reference_data: Path) -> None:
    model = model_of(reference_data, "skill/WaveForce.bmd")
    (action,) = model.actions
    assert (action.keys, action.lock, len(action.positions)) == (12, 1, 144)
    assert [len(bone.keys) for bone in model.bones] == [288, 288]


def test_a_dummy_bone_is_one_byte(reference_data: Path) -> None:
    model = model_of(reference_data, "player/HelmMale01.bmd")
    assert len(model.bones) == 56
    dummies = [index for index, bone in enumerate(model.bones) if bone.dummy]
    assert dummies == [9, 16, 21, 24, 32, 41, 52, 53, 54, 55]
    assert all(model.bones[index].name == b"" for index in dummies)


@pytest.mark.parametrize("name", MODEL_FILES)
def test_a_model_encodes_to_its_bytes(reference_data: Path, name: str) -> None:
    data = (reference_data / name).read_bytes()
    assert CODEC.encode(decode(data)) == data


def test_bytes_after_the_last_bone_are_kept(reference_data: Path) -> None:
    data = (reference_data / "Object12/Shine01.bmd").read_bytes() + b"\xaa\xbb"
    model = decode(data)
    assert model.trailing == b"\xaa\xbb"
    assert CODEC.encode(model) == data


def test_bytes_after_the_encrypted_block_are_kept(reference_data: Path) -> None:
    data = (reference_data / "object35/Object70.bmd").read_bytes() + b"\x01\x02"
    model = decode(data)
    assert (model.after_block, model.trailing) == (b"\x01\x02", b"")
    assert CODEC.encode(model) == data


@pytest.mark.parametrize("lie", LIES)
def test_a_file_that_lies_is_corrupt(reference_data: Path, lie: str) -> None:
    with pytest.raises(MutoolsError) as caught:
        decode(lying_file(reference_data, lie))
    assert caught.value.code is ErrorCode.CORRUPT_FILE


def test_a_corrupt_file_names_the_part_and_what_is_left(reference_data: Path) -> None:
    with pytest.raises(MutoolsError) as caught:
        decode(lying_file(reference_data, "more vertices than bytes"), "Shine01.bmd")
    assert caught.value.message == "Shine01.bmd: mesh 0 asks for 32,767 vertices and 338 bytes are left"


def test_empty_actions_read_while_bones_times_actions_fit() -> None:
    model = decode(empty_actions(2, 30_000))
    assert (len(model.actions), len(model.bones)) == (30_000, 2)
