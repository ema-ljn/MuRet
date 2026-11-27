from pathlib import Path

import pytest

from mubmd.adapters.storage import LocalFileStore
from mubmd.app.checks import TextureFiles, findings_of
from mubmd.app.documents import decode_document
from mubmd.core.findings import Finding, Severity
from mubmd.core.formats.model_view import model_from_view, model_view
from tests.app.fake_store import FakeStore
from tests.models import put

WORK = Path("/work/Data")
SWORD = WORK / "Item" / "Sword01.bmd"
HELPER = WORK / "player" / "Helper03.bmd"
TEXTURE_NAME = "/meshes/0/texture_name"


class CountingStore(FakeStore):
    def __init__(self, files: dict[Path, bytes]) -> None:
        super().__init__(files)
        self.listed: list[Path] = []

    def files_in(self, folder: Path) -> tuple[Path, ...]:
        self.listed.append(folder)
        return super().files_in(folder)


@pytest.fixture
def files(reference_data: Path) -> dict[Path, bytes]:
    models = ("Item/Sword01.bmd", "Item/waterring.bmd", "player/Helper03.bmd")
    return {WORK / name: (reference_data / name).read_bytes() for name in models}


def findings(store: FakeStore, path: Path, textures: TextureFiles | None = None) -> tuple[Finding, ...]:
    decoded = decode_document(store, path)
    return findings_of(store, path, decoded.identified, decoded.document, None, textures)


def renamed(store: FakeStore, path: Path, name: str) -> tuple[Finding, ...]:
    decoded = decode_document(store, path)
    view = model_view(decoded.document)
    put(view, TEXTURE_NAME, name)
    return findings_of(store, path, decoded.identified, model_from_view(view), decoded.document)


def test_a_texture_in_the_models_folder_is_found(files: dict[Path, bytes]) -> None:
    store = FakeStore({**files, WORK / "Item" / "sword02.OZJ": b""})
    assert findings(store, SWORD) == ()


def test_a_texture_in_a_folder_beside_is_found_without_case(files: dict[Path, bytes]) -> None:
    store = FakeStore({**files, WORK / "ITEM" / "UNICON.ozj": b"", WORK / "ITEM" / "UNICON01.ozt": b""})
    assert findings(store, HELPER) == ()


def test_a_texture_nobody_has_is_a_warning(files: dict[Path, bytes]) -> None:
    (finding,) = findings(FakeStore({**files, WORK / "skill" / "other.OZJ": b""}), SWORD)
    assert (finding.code, finding.severity, finding.pointer) == ("TEXTURE_NOT_FOUND", Severity.WARNING, TEXTURE_NAME)
    assert "sword02.OZJ" in finding.message


def test_only_an_edit_reports_a_texture_elsewhere(files: dict[Path, bytes]) -> None:
    store = FakeStore({**files, WORK / "Item" / "sword02.OZJ": b"", WORK / "skill" / "GroundEff.OZJ": b""})
    (finding,) = renamed(store, SWORD, "GroundEff.jpg")
    assert (finding.code, finding.severity, finding.pointer) == ("TEXTURE_ELSEWHERE", Severity.WARNING, TEXTURE_NAME)
    assert "skill" in finding.message
    assert renamed(store, SWORD, "sword02.jpg") == ()


def test_a_folder_is_listed_once(files: dict[Path, bytes]) -> None:
    store = CountingStore({**files, WORK / "Item" / "sword02.OZJ": b"", WORK / "Item" / "etcring.OZJ": b""})
    textures = TextureFiles(store)
    findings(store, SWORD, textures)
    findings(store, WORK / "Item" / "waterring.bmd", textures)
    assert store.listed.count(WORK / "Item") == 1


def test_find_gives_the_file_with_its_own_case(reference_data: Path) -> None:
    textures = TextureFiles(LocalFileStore())
    assert (
        textures.find(reference_data / "Item" / "Sword01.bmd", "SWORD02.ozj") == reference_data / "Item" / "sword02.OZJ"
    )


def test_find_looks_in_the_folders_beside(reference_data: Path) -> None:
    textures = TextureFiles(LocalFileStore())
    assert (
        textures.find(reference_data / "player" / "Helper03.bmd", "unicon.OZJ")
        == reference_data / "Item" / "unicon.OZJ"
    )
    assert textures.find(reference_data / "Item" / "waterring.bmd", "waterring.OZJ") is None
