from dataclasses import dataclass
from struct import Struct
from typing import Final

from mubmd.core.errors import ErrorCode, MubmdError

OZJ_PREFIX: Final = 24
OZT_PREFIX: Final = 4
MAX_SIDE: Final = 512
JPEG_START: Final = b"\xff\xd8"
OZT_BITS: Final = 32
OZT_SIZE: Final = Struct("<hhB")
OZT_SIZE_OFFSET: Final = OZT_PREFIX + 12
OZT_PIXELS_OFFSET: Final = OZT_PREFIX + 18
PIXEL_SIZE: Final = 4
RED: Final = 0
BLUE: Final = 2


@dataclass(frozen=True, slots=True)
class RgbaImage:
    width: int
    height: int
    pixels: bytes


def _refused(reason: str) -> MubmdError:
    return MubmdError(ErrorCode.CORRUPT_FILE, f"The client does not load this texture: {reason}")


def read_ozt(data: bytes) -> RgbaImage:
    """As the client's loader (0x63bd00): of the TGA header only width, height and bits; rows flipped; top row first."""
    if len(data) < OZT_SIZE_OFFSET + OZT_SIZE.size:
        raise _refused("the file ends before its TGA header")
    width, height, bits = OZT_SIZE.unpack_from(data, OZT_SIZE_OFFSET)
    if bits != OZT_BITS:
        raise _refused(f"{bits} bits per pixel, and it reads only {OZT_BITS}")
    if width > MAX_SIDE or height > MAX_SIDE:
        raise _refused(f"{width} x {height}, and no side may pass {MAX_SIDE}")
    if width <= 0 or height <= 0:
        raise _refused(f"{width} x {height} has no pixels")
    row = width * PIXEL_SIZE
    end = OZT_PIXELS_OFFSET + row * height
    if len(data) < end:
        raise _refused("the file ends before its last pixel")
    pixels = bytearray().join(data[start : start + row] for start in range(end - row, OZT_PIXELS_OFFSET - 1, -row))
    pixels[RED::PIXEL_SIZE], pixels[BLUE::PIXEL_SIZE] = pixels[BLUE::PIXEL_SIZE], pixels[RED::PIXEL_SIZE]
    return RgbaImage(width, height, bytes(pixels))


def read_ozj(data: bytes) -> bytes:
    """The JPEG after the 24 bytes the client skips (0x63b870) before it hands the file to libjpeg."""
    jpeg = data[OZJ_PREFIX:]
    if not jpeg:
        raise _refused("the file ends before its JPEG")
    if not jpeg.startswith(JPEG_START):
        raise _refused("it is not a JPEG")
    return jpeg
