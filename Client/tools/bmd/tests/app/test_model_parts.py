from pathlib import Path

import pytest

from mubmd.app.model_parts import (
    Part,
    is_model,
    model_parts,
    part_facts,
    part_fields,
    part_of,
    part_title,
    tree_parents,
)
from mubmd.core.json_types import JsonObject
from tests.models import put, view_of

HELM = "player/HelmMale01.bmd"
HELM_DUMMIES = (9, 16, 21, 24, 32, 41, 52, 53, 54, 55)


def children_of(part: Part, pointer: str) -> tuple[Part, ...] | None:
    if part.pointer == pointer:
        return part.children
    found = (children_of(child, pointer) for child in part.children)
    return next((children for children in found if children is not None), None)


def pointers_under(view: JsonObject, pointer: str) -> list[str]:
    children = children_of(model_parts(view), pointer)
    assert children is not None
    return [child.pointer for child in children]


def labels_under(view: JsonObject, pointer: str) -> list[str]:
    children = children_of(model_parts(view), pointer)
    assert children is not None
    return [child.label for child in children]


def test_a_model_has_three_groups(reference_data: Path) -> None:
    root = model_parts(view_of(reference_data, HELM))
    assert (root.pointer, root.label) == ("", "Data2\\Player\\Helm\\helm_01.smd")
    assert [(group.pointer, group.label) for group in root.children] == [
        ("/meshes", "meshes (2)"),
        ("/actions", "actions (1)"),
        ("/bones", "bones (56)"),
    ]


def test_a_mesh_is_labeled_with_its_texture_and_its_counts(reference_data: Path) -> None:
    assert labels_under(view_of(reference_data, HELM), "/meshes") == [
        "0  head_02.jpg · 40 vertices · 62 triangles",
        "1  hide.jpg · 8 vertices · 6 triangles · hidden",
    ]
    assert labels_under(view_of(reference_data, "player/MaskHelmMale09.bmd"), "/meshes")[1] == (
        "1  hair_R.jpg · 25 vertices · 32 triangles · hair · R"
    )


def test_an_action_is_labeled_with_its_keys_and_its_lock(reference_data: Path) -> None:
    assert labels_under(view_of(reference_data, HELM), "/actions") == ["0  1 key"]
    assert labels_under(view_of(reference_data, "NPC/BloodCastle01.bmd"), "/actions") == ["0  12 keys"]
    locked: JsonObject = {"format": "model", "name": "", "meshes": [], "bones": [], "actions": [{"keys": 3, "lock": 1}]}
    assert labels_under(locked, "/actions") == ["0  3 keys · locked"]


def test_a_bone_hangs_from_its_parent(reference_data: Path) -> None:
    view = view_of(reference_data, HELM)
    assert pointers_under(view, "/bones")[:1] == ["/bones/0"]
    assert labels_under(view, "/bones/0") == ["1  Bip01 Footsteps", "2  Bip01 Pelvis"]
    assert labels_under(view, "/bones/2")[:1] == ["3  Bip01 L Thigh"]


def test_dummy_bones_sit_under_the_group(reference_data: Path) -> None:
    view = view_of(reference_data, HELM)
    group = children_of(model_parts(view), "/bones")
    assert group is not None
    assert [(part.pointer, part.label) for part in group[1:]] == [
        (f"/bones/{index}", f"{index}  (dummy)") for index in HELM_DUMMIES
    ]


def test_a_parent_after_its_child_still_nests(reference_data: Path) -> None:
    under = pointers_under(view_of(reference_data, "NPC/BloodCastle01.bmd"), "/bones/45")
    assert under[:2] == ["/bones/43", "/bones/44"]


def test_bones_in_a_cycle_sit_under_the_group(reference_data: Path) -> None:
    view = view_of(reference_data, HELM)
    put(view, "/bones/1/parent", 2)
    put(view, "/bones/2/parent", 1)
    put(view, "/bones/10/parent", 99)
    put(view, "/bones/11/parent", 9)
    under = pointers_under(view, "/bones")
    assert under[:6] == ["/bones/0", "/bones/1", "/bones/2", "/bones/3", "/bones/4", "/bones/5"]
    assert {"/bones/10", "/bones/11"} <= set(under)
    assert pointers_under(view, "/bones/0") == []


def test_tree_parents_of_a_real_model(reference_data: Path) -> None:
    bones = view_of(reference_data, "Monster/Monster01.bmd")["bones"]
    assert isinstance(bones, list)
    own = [bone.get("parent", -1) if isinstance(bone, dict) else -1 for bone in bones]
    assert tree_parents(bones) == own
    assert [index for index, bone in enumerate(bones) if isinstance(bone, dict) and "dummy" in bone] == [
        9,
        16,
        21,
        32,
        41,
    ]


def test_a_part_has_the_fields_the_ui_edits(reference_data: Path) -> None:
    sword, helm = view_of(reference_data, "Item/Sword01.bmd"), view_of(reference_data, HELM)
    assert part_fields(sword, "/meshes/0") == {"texture_name": "sword02.jpg", "texture": 0}
    assert part_fields(sword, "") == {"name": "Data2\\Item\\Sword\\Sword02.smd"}
    assert part_fields(helm, "/bones/3") == {"name": {"text": "Bip01 L Thigh", "tail": "cd" * 18}, "parent": 2}
    assert part_fields(helm, "/meshes/0")["texture_name"] == {"text": "head_02.jpg", "tail": "5f30312e6a70670067"}


@pytest.mark.parametrize("pointer", ["/bones/9", "/actions/0", "/meshes", "/bones", "/bones/99", "/nothing/0"])
def test_the_other_parts_have_no_fields(reference_data: Path, pointer: str) -> None:
    assert part_fields(view_of(reference_data, HELM), pointer) == {}


def test_a_meshs_facts_say_what_its_name_does(reference_data: Path) -> None:
    facts = part_facts(view_of(reference_data, "player/MaskHelmMale09.bmd"), "/meshes/1")
    assert facts == (
        ("vertices", "25"),
        ("normals", "25"),
        ("uvs", "26"),
        ("triangles", "32"),
        ("file", "hair_R.OZJ"),
        ("slot", "hair"),
        ("script", "R"),
    )


def test_a_plain_meshs_facts_have_no_slot_and_no_script(reference_data: Path) -> None:
    facts = dict(part_facts(view_of(reference_data, "Item/Sword01.bmd"), "/meshes/0"))
    assert (facts["file"], "slot" in facts, "script" in facts) == ("sword02.OZJ", False, False)


def test_the_facts_of_the_other_parts(reference_data: Path) -> None:
    view = view_of(reference_data, HELM)
    assert part_facts(view, "") == (("version", "10"), ("meshes", "2"), ("actions", "1"), ("bones", "56"))
    assert part_facts(view, "/actions/0") == (("keys", "1"), ("lock", "0"))
    assert part_facts(view, "/bones/3") == (("actions", "1"),)
    assert part_facts(view, "/bones/9") == (("dummy", "1"),)
    assert part_facts(view, "/bones") == ()


@pytest.mark.parametrize(
    ("pointer", "part", "title"),
    [
        ("/bones/3/name", "/bones/3", "bone 3"),
        ("/meshes/0/vertices/4/bone", "/meshes/0", "mesh 0"),
        ("/actions/2", "/actions/2", "action 2"),
        ("/name", "", "model"),
        ("/bones", "/bones", "bones"),
        ("", "", "model"),
    ],
)
def test_a_pointer_belongs_to_a_part(pointer: str, part: str, title: str) -> None:
    assert (part_of(pointer), part_title(part_of(pointer))) == (part, title)


def test_only_a_model_view_is_a_model(reference_data: Path) -> None:
    assert is_model(view_of(reference_data, HELM))
    assert not is_model({"format": "text", "texts": []})
