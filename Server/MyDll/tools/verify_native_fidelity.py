"""Verify exact native bodies in the linked DLL and update the port ledger."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

import pefile

ORIGINAL = "910966f738797b0508483c12c9619346c8760243df24d6db3d36780b43870fa1"
INSTALLED = "8b39364e6720632b308e48246043668d6a0db0d74f05260763ce8d40919d16e9"
NATIVE = {
    0x10004B4F: ("?mydll_util_bswap64@@YA_K_K@Z", 107),
    0x10004BBA: ("?mydll_util_bswap64_inplace@@YAXPAE@Z", 84),
    0x10004C0E: ("?mydll_util_bswap32_inplace@@YAXPAE@Z", 84),
    0x1000E2F1: ("_mydll_cmd_is_slash@12", 39),
    0x1002C651: ("?mydll_net_xor_aa@@YGXPAEH@Z", 56),
}
LEDGER_STATES = {
    0x10004B4F: ("exact", "not_applicable", "passed", "linked_exact"),
    0x10004BBA: ("exact", "not_applicable", "passed", "linked_exact"),
    0x10004C0E: ("exact", "not_applicable", "passed", "linked_exact"),
    0x1000E2F1: ("exact", "not_applicable", "passed", "linked_exact"),
    0x1002C651: ("exact", "not_applicable", "passed", "linked_exact"),
}


class FidelityError(ValueError):
    pass


def read_reference(path: Path, expected: str) -> bytes:
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != expected:
        raise FidelityError(f"Unexpected reference hash: {path}")
    return data


def read_map(path: Path) -> tuple[dict[str, int], int, int]:
    text = path.read_text(encoding="ascii")
    base = re.search(r"Preferred load address is ([0-9a-f]+)", text)
    stamp = re.search(r"Timestamp is ([0-9a-f]+)", text)
    if base is None or stamp is None:
        raise FidelityError("The map has no preferred base or timestamp")
    symbols = {
        name: int(address, 16) - int(base[1], 16)
        for name, address in re.findall(
            r"^\s+[0-9a-f]+:[0-9a-f]+\s+(\S+)\s+([0-9a-f]+)\s+f\b", text, re.MULTILINE | re.IGNORECASE,
        )
    }
    return symbols, int(base[1], 16), int(stamp[1], 16)


def linked_window(dll: Path, symbols: dict[str, int]) -> dict[int, bytes]:
    with pefile.PE(str(dll)) as pe:
        if pe.FILE_HEADER.Machine != 0x14C or pe.OPTIONAL_HEADER.Magic != 0x10B:
            raise FidelityError("Expected a PE32 x86 DLL")
        result = {}
        for va, (name, size) in NATIVE.items():
            if name not in symbols:
                raise FidelityError(f"Missing linked symbol {name}")
            section = pe.get_section_by_rva(symbols[name])
            body = pe.get_data(symbols[name], size)
            if section is None or not section.Characteristics & 0x20000000 or len(body) != size:
                raise FidelityError(f"Incomplete linked body {name}")
            result[va] = body
        exports = {(export.name, export.ordinal) for export in pe.DIRECTORY_ENTRY_EXPORT.symbols}
        if exports != {(b"Init", 1)}:
            raise FidelityError(f"Unexpected exports: {exports!r}")
    return result


def compare(dll: Path, link_map: Path, original: Path, installed: Path) -> list[dict[str, object]]:
    symbols, base, timestamp = read_map(link_map)
    with pefile.PE(str(dll)) as pe:
        if pe.OPTIONAL_HEADER.ImageBase != base or pe.FILE_HEADER.TimeDateStamp != timestamp:
            raise FidelityError("DLL and map do not agree")
    linked = linked_window(dll, symbols)
    original_bytes = read_reference(original, ORIGINAL)
    installed_bytes = read_reference(installed, INSTALLED)
    result = []
    for va, (_, size) in sorted(NATIVE.items()):
        expected = original_bytes[va - 0x10000000:va - 0x10000000 + size]
        if expected != installed_bytes[va - 0x10000000:va - 0x10000000 + size]:
            raise FidelityError(f"Reference DLLs differ at {va:#x}")
        if linked[va] != expected:
            raise FidelityError(f"Linked bytes differ at {va:#x}")
        result.append({"va": hex(va), "bytes": size, "instruction": "exact", "linked": "exact"})
    return result


def check_ledger(path: Path) -> None:
    lines = path.read_text(encoding="utf-8").splitlines()
    expected_header = (
        "original_va", "original_body_bytes", "original_symbol", "muret_source", "body_mode",
        "instruction_fidelity", "data_fidelity", "behavior_test", "link_status", "notes",
    )
    if not lines or tuple(lines[0].split("\t")) != expected_header:
        raise FidelityError("Unexpected ledger schema")
    seen = set()
    for line in lines[1:]:
        row = line.split("\t")
        if len(row) != len(expected_header):
            raise FidelityError("Malformed ledger row")
        va = int(row[0], 16)
        if va not in LEDGER_STATES or va in seen:
            raise FidelityError(f"Unknown or duplicated ledger row {va:#x}")
        if tuple(row[5:9]) != LEDGER_STATES[va]:
            raise FidelityError(f"Ledger status differs at {va:#x}")
        seen.add(va)
    if seen != set(LEDGER_STATES):
        raise FidelityError("The ledger does not contain every scoped function")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--installed", type=Path, required=True)
    parser.add_argument("--dll", type=Path, required=True)
    parser.add_argument("--map", dest="link_map", type=Path, required=True)
    parser.add_argument("--ledger", type=Path, required=True)
    args = parser.parse_args()
    try:
        functions = compare(args.dll, args.link_map, args.original, args.installed)
        check_ledger(args.ledger)
    except (OSError, ValueError, pefile.PEFormatError) as error:
        print(json.dumps({"schema": 1, "ok": False, "error": str(error)}), file=sys.stderr)
        return 1
    print(json.dumps({"schema": 1, "ok": True, "functions": functions}))
    return 0


if __name__ == "__main__":
    sys.exit(main())
