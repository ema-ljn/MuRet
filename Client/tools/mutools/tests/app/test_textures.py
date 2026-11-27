from pathlib import Path

import pytest

from mutools.adapters.storage import LocalFileStore
from mutools.app.textures import mesh_texture
from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.texture_images import RgbaImage
from tests.app.fake_store import FakeStore
from tests.models import view_of

WORK = Path("/work/Data")


def test_a_jpeg_texture(reference_data: Path) -> None:
    name = "Item/Sword01.bmd"

    texture = mesh_texture(LocalFileStore(), reference_data / name, view_of(reference_data, name), 0)

    assert texture.file == reference_data / "Item" / "sword02.OZJ"
    assert isinstance(texture.content, bytes)
    assert len(texture.content) == 6292


def test_a_tga_texture(reference_data: Path) -> None:
    name = "player/Helper03.bmd"

    texture = mesh_texture(LocalFileStore(), reference_data / name, view_of(reference_data, name), 1)

    assert texture.file == reference_data / "Item" / "unicon01.OZT"
    assert isinstance(texture.content, RgbaImage)
    assert (texture.content.width, texture.content.height) == (32, 32)


def test_a_texture_no_folder_has(reference_data: Path) -> None:
    name = "Item/waterring.bmd"

    with pytest.raises(MutoolsError) as error:
        mesh_texture(LocalFileStore(), reference_data / name, view_of(reference_data, name), 1)

    assert error.value.code is ErrorCode.FILE_NOT_FOUND
    assert "waterring.OZJ" in error.value.message


def test_a_mesh_the_model_does_not_have(reference_data: Path) -> None:
    name = "Item/Sword01.bmd"

    with pytest.raises(MutoolsError) as error:
        mesh_texture(LocalFileStore(), reference_data / name, view_of(reference_data, name), 1)

    assert (error.value.code, error.value.pointer) == (ErrorCode.POINTER_NOT_FOUND, "/meshes/1")


def test_a_texture_the_client_refuses(reference_data: Path) -> None:
    name = "Item/Sword01.bmd"
    tga = (reference_data / "object35" / "ma_001.OZJ").read_bytes()
    store = FakeStore({WORK / name: (reference_data / name).read_bytes(), WORK / "Item" / "sword02.OZJ": tga})

    with pytest.raises(MutoolsError) as error:
        mesh_texture(store, WORK / name, view_of(reference_data, name), 0)

    assert error.value.code is ErrorCode.CORRUPT_FILE
