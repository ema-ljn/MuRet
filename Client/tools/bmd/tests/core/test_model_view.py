import json
import re
from collections.abc import Callable
from copy import deepcopy
from pathlib import Path

import pytest

from mubmd.core.errors import ErrorCode, MubmdError
from mubmd.core.formats.model_view import MODEL_FIELDS, model_from_view, model_view
from mubmd.core.json_types import JsonObject, JsonValue
from mubmd.core.leaves import leaves
from mubmd.core.pointer import format_pointer
from tests.conftest import MODEL_FILES
from tests.models import CODEC, at, model_file, model_of, view_of

VIEW_HEADER = {"/format_version", "/format", "/encoding"}
FIRST_POSITION = "/meshes/0/vertices/0/position/0"


def refused(view: JsonObject) -> MubmdError:
    with pytest.raises(MubmdError) as caught:
        model_from_view(view)
    return caught.value


def test_a_model_reads_as_the_file_says(reference_data: Path) -> None:
    view = view_of(reference_data, "Item/Sword01.bmd")
    assert {key: view[key] for key in ("format_version", "format", "encoding", "version", "name", "trailing")} == {
        "format_version": 1,
        "format": "model",
        "encoding": "cp949",
        "version": 10,
        "name": "Data2\\Item\\Sword\\Sword02.smd",
        "trailing": "",
    }
    assert "after_block" not in view
    (mesh,) = at(view, "/meshes")
    assert (mesh["texture"], mesh["texture_name"]) == (0, "sword02.jpg")
    assert [len(mesh[part]) for part in ("vertices", "normals", "uvs", "triangles")] == [42, 68, 42, 68]
    assert mesh["vertices"][0] == {"bone": 0, "position": [-1.417384, -1.0287832, -10.2511]}
    assert mesh["normals"][0] == {"bone": 0, "normal": [0.0, 4.3312576e-07, -1.0], "vertex": 0}
    assert mesh["uvs"][0] == [0.9868, 0.45740002]
    assert mesh["triangles"][0] == {
        "polygon": 3,
        "vertices": [0, 1, 2, -12851],
        "normals": [0, 0, 0, -12851],
        "uvs": [0, 1, 2, -12851],
        "raw_0x01": {"hex": "cd"},
        "raw_0x1a": {"hex": "cd" * 38},
    }
    assert view["actions"] == [{"keys": 1, "lock": 0}]
    assert view["bones"] == [
        {
            "name": {"text": "Box01", "tail": "cd" * 26},
            "parent": -1,
            "actions": [{"positions": [[0.049219, 0.0, -0.078563]], "rotations": [[1.570796, 0.811578, 0.0]]}],
        }
    ]


def test_zero_raw_bytes_do_not_show(reference_data: Path) -> None:
    view = view_of(reference_data, "object35/Object36.bmd")
    assert at(view, "/meshes/0/triangles/0") == {
        "polygon": 3,
        "vertices": [0, 1, 2, 0],
        "normals": [0, 1, 2, 0],
        "uvs": [0, 1, 2, 0],
        "raw_0x01": {"hex": "01"},
    }


def test_a_locked_action_shows_its_positions(reference_data: Path) -> None:
    view = view_of(reference_data, "skill/WaveForce.bmd")
    (action,) = at(view, "/actions")
    assert (action["keys"], action["lock"], len(action["positions"])) == (12, 1, 12)
    assert action["positions"][0] == [0.0, 0.0, 0.0]
    assert at(view, "/bones/1/parent") == 0


def test_a_dummy_bone_shows_its_byte(reference_data: Path) -> None:
    assert at(view_of(reference_data, "player/HelmMale01.bmd"), "/bones/9") == {"dummy": 1}


@pytest.mark.parametrize(
    ("name", "expected"),
    [
        ("object35/Object70.bmd", "Data2\\Object35\\cr_cmcenter02tr.S"),
        ("Object12/Shine01.bmd", "Data2\\Object12\\햇빛2.smd"),
        ("skill/m_waves.bmd", {"text": "Z:\\m_waves.smd", "tail": "00752050726f6a6563745c746d705c6d5f"}),
        ("player/Helper03.bmd", {"hex": "44617461325c4974656d5c48656c7065725cc0afb4cfb8aebec65fbec6c0ccc5"}),
    ],
)
def test_a_models_name_reads_by_its_bytes(reference_data: Path, name: str, expected: JsonValue) -> None:
    assert view_of(reference_data, name)["name"] == expected


@pytest.mark.parametrize("name", MODEL_FILES)
def test_a_view_builds_the_same_model(reference_data: Path, name: str) -> None:
    model = model_of(reference_data, name)
    assert model_from_view(json.loads(json.dumps(model_view(model)))) == model


def test_counts_follow_the_lists(reference_data: Path) -> None:
    data = (reference_data / "Item/Sword01.bmd").read_bytes()
    view = view_of(reference_data, "Item/Sword01.bmd")
    vertices = at(view, "/meshes/0/vertices")
    vertices.append(deepcopy(vertices[0]))
    longer = CODEC.encode(model_from_view(view))
    assert len(longer) == len(data) + 16
    assert len(CODEC.decode(longer, model_file("Sword01.bmd")).meshes[0].vertices) == 43 * 16


def eleven_keys(view: JsonObject) -> None:
    at(view, "/actions/0")["keys"] = 11


def two_actions_in_a_bone(view: JsonObject) -> None:
    actions = at(view, "/bones/0/actions")
    actions.append(deepcopy(actions[0]))


def eleven_rotations(view: JsonObject) -> None:
    at(view, "/bones/1/actions/0/rotations").pop()


def a_position_of_two(view: JsonObject) -> None:
    at(view, "/actions/0/positions")[0] = [0.0, 0.0]


def unlocked_with_positions(view: JsonObject) -> None:
    at(view, "/actions/0")["lock"] = 0


def locked_without_positions(view: JsonObject) -> None:
    del at(view, "/actions/0")["positions"]


def a_vertex_with_one_more_member(view: JsonObject) -> None:
    at(view, "/meshes/0/vertices/0")["x"] = 1


def bytes_after_a_block_that_is_not_there(view: JsonObject) -> None:
    view["after_block"] = "00"


def ninety_thousand_bones_times_actions(view: JsonObject) -> None:
    view["actions"] = [{"keys": 0, "lock": 0} for _ in range(30_000)]
    actions: list[JsonValue] = [{"positions": [], "rotations": []} for _ in range(30_000)]
    view["bones"] = [{"name": "bone", "parent": -1, "actions": actions} for _ in range(3)]


@pytest.mark.parametrize(
    ("change", "pointer"),
    [
        (eleven_keys, "/actions/0/positions"),
        (two_actions_in_a_bone, "/bones/0/actions"),
        (eleven_rotations, "/bones/1/actions/0/rotations"),
        (a_position_of_two, "/actions/0/positions/0"),
        (unlocked_with_positions, "/actions/0/positions"),
        (locked_without_positions, "/actions/0"),
        (a_vertex_with_one_more_member, "/meshes/0/vertices/0"),
        (bytes_after_a_block_that_is_not_there, "/after_block"),
        (ninety_thousand_bones_times_actions, "/bones"),
    ],
)
def test_lists_that_disagree_are_refused(
    reference_data: Path, change: Callable[[JsonObject], None], pointer: str
) -> None:
    view = view_of(reference_data, "skill/WaveForce.bmd")
    change(view)
    error = refused(view)
    assert (error.code, error.pointer) == (ErrorCode.INVALID_VALUE, pointer)


@pytest.mark.parametrize("value", [True, "1", float("nan"), float("inf"), 1e39])
def test_a_float_refuses_what_json_lets_through(reference_data: Path, value: JsonValue) -> None:
    view = view_of(reference_data, "Item/Sword01.bmd")
    at(view, "/meshes/0/vertices/0/position")[0] = value
    error = refused(view)
    assert (error.code, error.pointer) == (ErrorCode.INVALID_VALUE, FIRST_POSITION)


def test_an_integer_is_taken_as_a_float(reference_data: Path) -> None:
    view = view_of(reference_data, "Item/Sword01.bmd")
    at(view, "/meshes/0/vertices/0/position")[0] = 1
    written = at(model_view(model_from_view(view)), FIRST_POSITION)
    assert (written, type(written)) == (1.0, float)


def test_a_float_that_is_not_a_number_keeps_its_bytes(reference_data: Path) -> None:
    view = view_of(reference_data, "Item/Sword01.bmd")
    at(view, "/meshes/0/vertices/0/position")[0] = {"f32": "0000c07f"}
    model = model_from_view(view)
    assert model.meshes[0].vertices[4:8] == bytes.fromhex("0000c07f")
    assert at(model_view(model), FIRST_POSITION) == {"f32": "0000c07f"}


def test_a_written_texture_name_fits_its_bytes(reference_data: Path) -> None:
    view = view_of(reference_data, "Object12/Shine01.bmd")
    mesh = at(view, "/meshes/0")
    assert mesh["texture_name"] == {"text": "light2_R.jpg", "tail": "00544741"}
    mesh["texture_name"] = "light2.jpg"
    assert model_from_view(view).meshes[0].texture_name == b"light2.jpg" + bytes(22)
    mesh["texture_name"] = "a" * 31
    assert model_from_view(view).meshes[0].texture_name == b"a" * 31 + b"\0"
    mesh["texture_name"] = "a" * 32
    assert (refused(view).code, refused(view).pointer) == (ErrorCode.TEXT_TOO_LONG, "/meshes/0/texture_name")
    mesh["texture_name"] = "😀.jpg"
    assert refused(view).code is ErrorCode.NOT_ENCODABLE


def test_a_written_model_name_may_fill_its_bytes(reference_data: Path) -> None:
    view = view_of(reference_data, "Object12/Shine01.bmd")
    view["name"] = "a" * 32
    assert model_from_view(view).name == b"a" * 32
    view["name"] = "a" * 33
    assert (refused(view).code, refused(view).pointer) == (ErrorCode.TEXT_TOO_LONG, "/name")


def test_the_fields_cover_every_leaf(reference_data: Path) -> None:
    fields = {field.pointer: field for field in MODEL_FIELDS}
    patterns = [
        re.compile(re.sub(r"<0\\\.\\\.(\d)>", r"[0-\1]", re.escape(pointer).replace("<N>", r"\d+")))
        for pointer in fields
    ]
    for name in ("Item/Sword01.bmd", "skill/WaveForce.bmd"):
        pointers = {format_pointer(tokens) for tokens, _ in leaves(view_of(reference_data, name))} - VIEW_HEADER
        missing = [pointer for pointer in pointers if not any(pattern.fullmatch(pointer) for pattern in patterns)]
        assert missing == []
    assert fields["/meshes/<N>/texture_name"].type == "text"
    position = fields["/meshes/<N>/vertices/<N>/position/<0..2>"]
    assert (position.type, position.offset) == ("f32", 4)


def test_a_summary_counts_the_parts(reference_data: Path) -> None:
    player = CODEC.summarize(model_of(reference_data, "player/Player.bmd"))
    assert (player.cipher, player.version, player.meshes, player.bones, player.actions) == ("K16", 12, 0, 60, 205)
    assert (player.records, player.used_records) == (None, None)
    assert CODEC.summarize(model_of(reference_data, "Item/Sword01.bmd")).cipher == "none"


def test_the_codec_shows_and_builds_through_the_view(reference_data: Path) -> None:
    model = model_of(reference_data, "Object12/Shine01.bmd")
    assert CODEC.from_view(CODEC.to_view(model)) == model
    assert CODEC.describe_fields() == MODEL_FIELDS
