from collections import Counter
from pathlib import Path

import pytest

from mutools.core.checks.model_checks import check_model
from mutools.core.documents.model import MODEL_MAGIC, NORMAL_SIZE, TRIANGLE_SIZE, Model
from mutools.core.formats.model_textures import texture_text, texture_use
from mutools.core.formats.model_view import model_from_view, model_view
from tests.models import CODEC, model_file

type RealModel = tuple[Path, bytes, Model]


@pytest.fixture(scope="module")
def models(reference_data: Path) -> list[RealModel]:
    paths = sorted(path for path in reference_data.rglob("*") if path.suffix.lower() == ".bmd" and path.is_file())
    files = ((path, path.read_bytes()) for path in paths)
    return [
        (path, data, CODEC.decode(data, model_file(path.name))) for path, data in files if data.startswith(MODEL_MAGIC)
    ]


def test_every_model_round_trips(models: list[RealModel]) -> None:
    versions: Counter[int] = Counter()
    totals: Counter[str] = Counter()
    for _, _, model in models:
        versions[model.version] += 1
        totals["meshes"] += len(model.meshes)
        totals["triangles"] += sum(len(mesh.triangles) // TRIANGLE_SIZE for mesh in model.meshes)
        totals["normals"] += sum(len(mesh.normals) // NORMAL_SIZE for mesh in model.meshes)
        totals["actions"] += len(model.actions)
        totals["locked actions"] += sum(1 for action in model.actions if action.lock)
        totals["real bones"] += sum(1 for bone in model.bones if not bone.dummy)
        totals["dummy bones"] += sum(1 for bone in model.bones if bone.dummy)
    assert [path.name for path, data, model in models if CODEC.encode(model) != data] == []
    assert versions == {12: 816, 10: 1142, 0: 4}
    assert totals == {
        "meshes": 3421,
        "triangles": 450_338,
        "normals": 301_367,
        "actions": 3099,
        "locked actions": 186,
        "real bones": 25_526,
        "dummy bones": 2720,
    }


def test_every_model_view_builds_the_same_model(models: list[RealModel]) -> None:
    assert [path.name for path, _, model in models if model_from_view(model_view(model)) != model] == []


def test_the_real_models_have_only_their_known_findings(models: list[RealModel], reference_data: Path) -> None:
    found = [
        (path.relative_to(reference_data).as_posix(), finding.code, finding.pointer)
        for path, _, model in models
        for finding in check_model(model, None)
    ]
    assert found == [
        ("NPC/BloodCastle01.bmd", "PARENT_AFTER_CHILD", "/bones/43/parent"),
        ("NPC/BloodCastle01.bmd", "PARENT_AFTER_CHILD", "/bones/44/parent"),
        ("object35/Object36.bmd", "OUT_OF_RANGE", "/meshes/1/vertices/0/bone"),
        ("object35/Object36.bmd", "OUT_OF_RANGE", "/meshes/1/normals/0/bone"),
    ]


def test_the_real_models_have_the_scripts_the_client_counts(models: list[RealModel]) -> None:
    names = (texture_text(mesh.texture_name, model.encoding) for _, _, model in models for mesh in model.meshes)
    scripts = Counter(texture_use(name).script for name in names if name is not None)
    del scripts[""]
    assert scripts == {"R": 135, "H": 6, "S": 1}
