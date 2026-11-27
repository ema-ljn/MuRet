import struct
import zlib
from dataclasses import dataclass
from struct import Struct
from typing import Final

from mutools.core.errors import ErrorCode, MutoolsError

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
PNG_SIGNATURE: Final = b"\x89PNG\r\n\x1a\n"
PNG_NO_FILTER: Final = b"\x00"
PNG_DEPTH: Final = 8
PNG_RGBA: Final = 6


@dataclass(frozen=True, slots=True)
class RgbaImage:
    width: int
    height: int
    pixels: bytes


def _refused(reason: str) -> MutoolsError:
    return MutoolsError(ErrorCode.CORRUPT_FILE, f"The client does not load this texture: {reason}")


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


def png_of(image: RgbaImage) -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    row = image.width * PIXEL_SIZE
    rows = b"".join(PNG_NO_FILTER + image.pixels[top : top + row] for top in range(0, row * image.height, row))
    header = struct.pack(">IIBBBBB", image.width, image.height, PNG_DEPTH, PNG_RGBA, 0, 0, 0)
    return PNG_SIGNATURE + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
