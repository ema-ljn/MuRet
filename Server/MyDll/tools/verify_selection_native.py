"""Check the native f60 and f48 boundaries of a linked selection DLL."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

import pefile

from generate_original_coverage import INSTALLED, ORIGINAL, Image

WIRE_RVA = 0x2C651
WIRE_BODY_BYTES = 56
TERRAIN_RVA = 0x28580
TERRAIN_BODY_BYTES = 649
TERRAIN_PROLOGUE_BYTES = 6
MAP_FUNCTION = re.compile(
    r"^\s+[0-9a-f]+:[0-9a-f]+\s+(\S+)\s+([0-9a-f]+)\s+f\b",
    re.MULTILINE | re.IGNORECASE,
)
MAP_TIMESTAMP = re.compile(r"^\s*Timestamp is ([0-9a-f]+)\b", re.MULTILINE | re.IGNORECASE)
MAP_BASE = re.compile(r"^\s*Preferred load address is ([0-9a-f]+)\b", re.MULTILINE | re.IGNORECASE)


def linked_window(pe: pefile.PE, link_map: str, symbol_prefix: str, size: int) -> tuple[int, bytes]:
    addresses = [address for name, address in MAP_FUNCTION.findall(link_map) if name.startswith(symbol_prefix)]
    if len(addresses) != 1:
        raise ValueError(f"Expected one linked {symbol_prefix} in the selection map")
    rva = int(addresses[0], 16) - pe.OPTIONAL_HEADER.ImageBase
    section = pe.get_section_by_rva(rva)
    if section is None or not section.Characteristics & 0x20000000:
        raise ValueError(f"{symbol_prefix} must resolve to executable bytes in the selection DLL")
    body = pe.get_data(rva, size)
    if len(body) != size:
        raise ValueError(f"Incomplete linked window for {symbol_prefix}")
    return rva, body


def read_selection_windows(dll: Path, link_map: Path) -> tuple[tuple[int, bytes], tuple[int, bytes]]:
    text = link_map.read_text(encoding="ascii")
    with pefile.PE(str(dll)) as pe:
        if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
            raise ValueError("Expected a PE32 x86 selection DLL")
        stamp, base = MAP_TIMESTAMP.search(text), MAP_BASE.search(text)
        if (stamp is None or int(stamp[1], 16) != pe.FILE_HEADER.TimeDateStamp
                or base is None or int(base[1], 16) != pe.OPTIONAL_HEADER.ImageBase):
            raise ValueError("Selection DLL and link map do not match")
        return (
            linked_window(pe, text, "?XorWire@Protocol@MyDll@@", WIRE_BODY_BYTES),
            linked_window(pe, text, "?LoadGameServerTerrain@Boot@MyDll@@", TERRAIN_PROLOGUE_BYTES),
        )


def matching_reference_body(original: Image, installed: Image, rva: int, size: int) -> bytes:
    expected = original.read(original.base + rva, size)
    if len(expected) != size or expected != installed.read(installed.base + rva, size):
        raise ValueError(f"The complete reference bodies at RVA 0x{rva:08x} must agree")
    return expected


def first_difference(expected: bytes, actual: bytes) -> int | None:
    return next((i for i, (a, b) in enumerate(zip(expected, actual)) if a != b), None)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original", type=Path)
    parser.add_argument("installed_reference", type=Path)
    parser.add_argument("selection_dll", type=Path)
    parser.add_argument("selection_map", type=Path)
    args = parser.parse_args()
    try:
        original = Image(args.original, ORIGINAL)
        installed = Image(args.installed_reference, INSTALLED)
        wire = matching_reference_body(original, installed, WIRE_RVA, WIRE_BODY_BYTES)
        terrain = matching_reference_body(original, installed, TERRAIN_RVA, TERRAIN_BODY_BYTES)
        (wire_rva, linked_wire), (terrain_rva, linked_terrain) = read_selection_windows(
            args.selection_dll, args.selection_map,
        )
    except (OSError, ValueError, pefile.PEFormatError) as error:
        parser.error(str(error))
    wire_delta = first_difference(wire, linked_wire)
    if wire_delta is not None:
        parser.exit(1, f"f60 differs at body offset 0x{wire_delta:02x} (selection RVA 0x{wire_rva:08x})\n")
    print(f"f60: {WIRE_BODY_BYTES}/{WIRE_BODY_BYTES} linked bytes match both references (RVA 0x{wire_rva:08x})")
    print(f"f48: {TERRAIN_BODY_BYTES}/{TERRAIN_BODY_BYTES} original bytes match the installed reference")
    terrain_delta = first_difference(terrain[:TERRAIN_PROLOGUE_BYTES], linked_terrain)
    if terrain_delta is not None:
        print(f"f48: selection RVA 0x{terrain_rva:08x} differs at prologue offset 0x{terrain_delta:02x}")
    else:
        print(f"f48: selection RVA 0x{terrain_rva:08x} matches the checked prologue only")


if __name__ == "__main__":
    main()
