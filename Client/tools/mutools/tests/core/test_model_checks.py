from pathlib import Path

import pytest

from mutools.core.checks.model_checks import check_model
from mutools.core.documents.model import Bone, Model
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.findings import Finding, Severity
from mutools.core.formats.model_view import model_from_view
from mutools.core.json_types import JsonValue
from tests.models import CODEC, at, model_of, put, view_of

TRIANGLE = "/meshes/0/triangles/0"
TEXTURE_NAME = "/meshes/0/texture_name"


def findings_after(reference_data: Path, name: str, pointer: str, value: JsonValue) -> tuple[Finding, ...]:
    view = view_of(reference_data, name)
    put(view, pointer, value)
    return check_model(model_from_view(view), model_of(reference_data, name))


def places(findings: tuple[Finding, ...]) -> list[tuple[str, Severity, str]]:
    return [(finding.code, finding.severity, finding.pointer) for finding in findings]


@pytest.mark.parametrize(
    ("pointer", "value", "place"),
    [
        ("/meshes/0/vertices/0/bone", 1, "/meshes/0/vertices/0/bone"),
        ("/meshes/0/normals/0/bone", -1, "/meshes/0/normals/0/bone"),
        ("/meshes/0/normals/0/vertex", 42, "/meshes/0/normals/0/vertex"),
        (f"{TRIANGLE}/vertices", [42, 1, 2, 0], f"{TRIANGLE}/vertices/0"),
        (f"{TRIANGLE}/normals", [0, 68, 0, 0], f"{TRIANGLE}/normals/1"),
        (f"{TRIANGLE}/uvs", [0, 1, -1, 0], f"{TRIANGLE}/uvs/2"),
        ("/meshes/0/texture", 1, "/meshes/0/texture"),
    ],
)
def test_an_index_outside_its_list_is_out_of_range(
    reference_data: Path, pointer: str, value: JsonValue, place: str
) -> None:
    findings = findings_after(reference_data, "Item/Sword01.bmd", pointer, value)
    assert places(findings) == [("OUT_OF_RANGE", Severity.ERROR, place)]


def test_many_bad_indices_of_a_mesh_are_one_finding(reference_data: Path) -> None:
    view = view_of(reference_data, "Item/Sword01.bmd")
    for triangle in at(view, "/meshes/0/triangles"):
        triangle["vertices"][0] = 9999
    (finding,) = check_model(model_from_view(view), None)
    assert (finding.code, finding.pointer) == ("OUT_OF_RANGE", f"{TRIANGLE}/vertices/0")
    assert "68" in finding.message


def test_the_fourth_index_counts_only_for_a_polygon_of_four(reference_data: Path) -> None:
    assert at(view_of(reference_data, "Item/Sword01.bmd"), f"{TRIANGLE}/vertices/3") == -12851
    assert check_model(model_of(reference_data, "Item/Sword01.bmd"), None) == ()
    findings = findings_after(reference_data, "Item/Sword01.bmd", f"{TRIANGLE}/polygon", 4)
    assert [finding.pointer for finding in findings] == [
        f"{TRIANGLE}/vertices/3",
        f"{TRIANGLE}/normals/3",
        f"{TRIANGLE}/uvs/3",
    ]


@pytest.mark.parametrize("parent", [56, -2])
def test_a_parent_outside_the_bones_is_out_of_range(reference_data: Path, parent: int) -> None:
    findings = findings_after(reference_data, "player/HelmMale01.bmd", "/bones/0/parent", parent)
    assert places(findings) == [("OUT_OF_RANGE", Severity.ERROR, "/bones/0/parent")]


@pytest.mark.parametrize("parent", [5, 1])
def test_a_parent_after_its_child_is_a_warning(reference_data: Path, parent: int) -> None:
    findings = findings_after(reference_data, "player/HelmMale01.bmd", "/bones/1/parent", parent)
    assert places(findings) == [("PARENT_AFTER_CHILD", Severity.WARNING, "/bones/1/parent")]


def test_more_than_200_bones_is_out_of_range() -> None:
    def with_bones(count: int) -> Model:
        return Model(10, bytes(32), (), (), tuple(Bone(1) for _ in range(count)), b"", b"", "cp949")

    assert check_model(with_bones(200), None) == ()
    assert places(check_model(with_bones(201), None)) == [("OUT_OF_RANGE", Severity.ERROR, "/bones")]


@pytest.mark.parametrize("name", ["sword02.tga", "sword02_R.jpg", "hair.jpg"])
def test_a_rename_that_changes_what_the_client_does_warns(reference_data: Path, name: str) -> None:
    findings = findings_after(reference_data, "Item/Sword01.bmd", TEXTURE_NAME, name)
    assert places(findings) == [("TEXTURE_ROLE_CHANGED", Severity.WARNING, TEXTURE_NAME)]


def test_a_role_change_says_what_changed(reference_data: Path) -> None:
    (finding,) = findings_after(reference_data, "Item/Sword01.bmd", TEXTURE_NAME, "sword02.tga")
    assert "sword02.OZJ" in finding.message
    assert "sword02.OZT" in finding.message


def test_a_rename_that_keeps_the_role_has_no_finding(reference_data: Path) -> None:
    assert findings_after(reference_data, "Item/Sword01.bmd", TEXTURE_NAME, "blade.jpg") == ()


def test_a_role_change_needs_the_file_as_it_was(reference_data: Path) -> None:
    view = view_of(reference_data, "Item/Sword01.bmd")
    put(view, TEXTURE_NAME, "sword02.tga")
    assert check_model(model_from_view(view), None) == ()


@pytest.mark.parametrize(
    ("pointer", "value", "limit", "codes"),
    [
        (TEXTURE_NAME, "head_02.tga", 31, ["TEXTURE_ROLE_CHANGED"]),
        (TEXTURE_NAME, "head_03.jpg", 31, []),
        ("/name", "a" * 32, 32, []),
        ("/bones/1/parent", 5, None, ["PARENT_AFTER_CHILD"]),
        ("/bones/1/parent", 99, None, ["OUT_OF_RANGE"]),
        ("/meshes/0/texture", 7, None, ["OUT_OF_RANGE"]),
        ("/meshes/0/vertices/0/position/0", 1.5, None, []),
    ],
)
def test_check_value_gives_a_leafs_limit_and_findings(
    reference_data: Path, pointer: str, value: JsonValue, limit: int | None, codes: list[str]
) -> None:
    check = CODEC.check_value(pointer, value, model_of(reference_data, "player/HelmMale01.bmd"))
    assert check.limit == limit
    assert [(finding.code, finding.pointer) for finding in check.warnings] == [(code, pointer) for code in codes]


@pytest.mark.parametrize(
    ("pointer", "value", "code"),
    [
        (TEXTURE_NAME, "a" * 32, ErrorCode.TEXT_TOO_LONG),
        ("/meshes/0/vertices/0/position/0", "x", ErrorCode.INVALID_VALUE),
        ("/bones/1/parent", 40_000, ErrorCode.INVALID_VALUE),
        ("/actions/0/keys", 2, ErrorCode.INVALID_VALUE),
        ("/meshes/9/texture_name", "x.jpg", ErrorCode.POINTER_NOT_FOUND),
        ("/bones/9/name", "Bip01", ErrorCode.POINTER_NOT_FOUND),
        ("/meshes/0/vertices", [], ErrorCode.POINTER_NOT_FOUND),
        ("/meshes/0/vertices/0", {}, ErrorCode.POINTER_NOT_FOUND),
        ("/after_block", "00", ErrorCode.POINTER_NOT_FOUND),
        ("/nothing", 1, ErrorCode.POINTER_NOT_FOUND),
    ],
)
def test_check_value_raises_what_writing_the_value_would(
    reference_data: Path, pointer: str, value: JsonValue, code: ErrorCode
) -> None:
    with pytest.raises(MutoolsError) as caught:
        CODEC.check_value(pointer, value, model_of(reference_data, "player/HelmMale01.bmd"))
    assert caught.value.code is code


def test_the_codec_checks_a_whole_model(reference_data: Path) -> None:
    model = model_of(reference_data, "NPC/BloodCastle01.bmd")
    assert [finding.pointer for finding in CODEC.check(model, None)] == ["/bones/43/parent", "/bones/44/parent"]
