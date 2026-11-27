import hashlib
from pathlib import Path
from struct import pack_into

import pytest

from mutools.core.errors import ErrorCode, MutoolsError
from mutools.core.formats.texture_images import read_ozj, read_ozt

WIDTH_OFFSET = 0x10
PIXEL = 4


def refused(error: pytest.ExceptionInfo[MutoolsError]) -> str:
    assert error.value.code is ErrorCode.CORRUPT_FILE
    return error.value.message


def with_width(data: bytes, width: int) -> bytes:
    changed = bytearray(data)
    pack_into("<h", changed, WIDTH_OFFSET, width)
    return bytes(changed)


@pytest.fixture(scope="module")
def unicon(reference_data: Path) -> bytes:
    return (reference_data / "Item" / "unicon01.OZT").read_bytes()


def test_an_ozt_gives_its_pixels_top_row_first(unicon: bytes) -> None:
    image = read_ozt(unicon)

    assert len(unicon) == 4144
    assert (image.width, image.height, len(image.pixels)) == (32, 32, 4096)
    assert tuple(image.pixels[:PIXEL]) == (226, 226, 226, 3)
    last_row = (image.height - 1) * image.width * PIXEL
    assert tuple(image.pixels[last_row : last_row + PIXEL]) == (202, 124, 56, 247)
    assert hashlib.sha256(image.pixels).hexdigest().startswith("bce5a0cba06402e6")


def test_an_ozt_that_is_not_square(reference_data: Path) -> None:
    image = read_ozt((reference_data / "NPC" / "lnn01.OZT").read_bytes())

    assert (image.width, image.height) == (64, 128)
    assert tuple(image.pixels[:PIXEL]) == (193, 193, 193, 83)


def test_an_ozt_of_24_bits_is_refused(reference_data: Path) -> None:
    with pytest.raises(MutoolsError) as error:
        read_ozt((reference_data / "Monster" / "wolfa02.OZT").read_bytes())

    assert "24" in refused(error)


@pytest.mark.parametrize("kept", [10, 4144 - 2000])
def test_an_ozt_cut_short_is_refused(unicon: bytes, kept: int) -> None:
    with pytest.raises(MutoolsError) as error:
        read_ozt(unicon[:kept])

    refused(error)


def test_an_ozt_with_a_side_over_512_is_refused(unicon: bytes) -> None:
    with pytest.raises(MutoolsError) as error:
        read_ozt(with_width(unicon, 513))

    assert "512" in refused(error)


def test_an_ozt_with_no_pixels_is_refused(unicon: bytes) -> None:
    with pytest.raises(MutoolsError) as error:
        read_ozt(with_width(unicon, 0))

    refused(error)


def test_an_ozj_gives_its_jpeg(reference_data: Path) -> None:
    data = (reference_data / "Item" / "sword02.OZJ").read_bytes()

    jpeg = read_ozj(data)

    assert (len(data), len(jpeg)) == (6316, 6292)
    assert jpeg[:4] == bytes.fromhex("ffd8ffe0")


def test_an_ozj_that_is_a_tga_is_refused(reference_data: Path) -> None:
    with pytest.raises(MutoolsError) as error:
        read_ozj((reference_data / "object35" / "ma_001.OZJ").read_bytes())

    assert "not a JPEG" in refused(error)


def test_an_ozj_shorter_than_its_prefix_is_refused() -> None:
    with pytest.raises(MutoolsError) as error:
        read_ozj(bytes(24))

    refused(error)
