from collections import Counter
from pathlib import Path

from mubmd.core.documents.model import MODEL_MAGIC, NORMAL_SIZE, TRIANGLE_SIZE
from mubmd.core.formats.model_format import ModelFormat
from mubmd.core.formats.model_view import model_from_view, model_view
from mubmd.core.ports import CatalogFile


def model_paths(reference_data: Path) -> list[Path]:
    paths = (path for path in reference_data.rglob("*") if path.suffix.lower() == ".bmd" and path.is_file())
    return sorted(path for path in paths if path.read_bytes()[: len(MODEL_MAGIC)] == MODEL_MAGIC)


def test_every_model_round_trips(reference_data: Path) -> None:
    codec = ModelFormat()
    versions: Counter[int] = Counter()
    totals: Counter[str] = Counter()
    differing = []
    for path in model_paths(reference_data):
        data = path.read_bytes()
        model = codec.decode(data, CatalogFile(path.name, None, read_by_client=False))
        if codec.encode(model) != data:
            differing.append(path.name)
        versions[model.version] += 1
        totals["meshes"] += len(model.meshes)
        totals["triangles"] += sum(len(mesh.triangles) // TRIANGLE_SIZE for mesh in model.meshes)
        totals["normals"] += sum(len(mesh.normals) // NORMAL_SIZE for mesh in model.meshes)
        totals["actions"] += len(model.actions)
        totals["locked actions"] += sum(1 for action in model.actions if action.lock)
        totals["real bones"] += sum(1 for bone in model.bones if not bone.dummy)
        totals["dummy bones"] += sum(1 for bone in model.bones if bone.dummy)
    assert differing == []
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


def test_every_model_view_builds_the_same_model(reference_data: Path) -> None:
    codec = ModelFormat()
    differing = []
    for path in model_paths(reference_data):
        model = codec.decode(path.read_bytes(), CatalogFile(path.name, None, read_by_client=False))
        if model_from_view(model_view(model)) != model:
            differing.append(path.name)
    assert differing == []
