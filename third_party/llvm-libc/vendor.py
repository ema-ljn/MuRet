#!/usr/bin/env python3
"""Copies the LLVM-libc headers that the client's math needs from an llvm-project checkout.

    python3 -I third_party/llvm-libc/vendor.py <llvm-project checkout at the pinned tag>

The copy is the closure of the quoted includes of ENTRY_HEADERS, whatever the preprocessor would keep, so the
same files serve every compiler and system. It replaces the libc/ folder next to this script.
"""
import pathlib
import re
import shutil
import sys

PINNED_TAG = "llvmorg-23.1.6"
ENTRY_HEADERS = [
    "shared/math/acos.h",
    "shared/math/acosf.h",
    "shared/math/cos.h",
    "shared/math/cosf.h",
    "shared/math/log10.h",
    "shared/math/sin.h",
    "shared/math/sinf.h",
    "shared/math/tan.h",
    "shared/math/tanf.h",
]
# Quoted includes with no file in the checkout: a system header, and FreeBSD variants llvm-project does not ship.
UNRESOLVED = {"stddef.h", "i386/syscall.h", "aarch64/syscall.h", "arm/syscall.h", "riscv/syscall.h"}
QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"', re.MULTILINE)


def closure(libc_root: pathlib.Path) -> list[pathlib.Path]:
    pending = [libc_root / header for header in ENTRY_HEADERS]
    seen: set[pathlib.Path] = set()
    while pending:
        header = pending.pop()
        if header in seen:
            continue
        if not header.is_file():
            raise SystemExit(f"missing header: {header}")
        seen.add(header)
        for name in QUOTED_INCLUDE.findall(header.read_text(encoding="utf-8")):
            beside = (header.parent / name).resolve()
            from_root = (libc_root / name).resolve()
            if beside.is_file():
                pending.append(beside)
            elif from_root.is_file():
                pending.append(from_root)
            elif name not in UNRESOLVED:
                raise SystemExit(f"{header}: cannot resolve \"{name}\"")
    return sorted(seen)


def main() -> None:
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    checkout = pathlib.Path(sys.argv[1]).resolve()
    libc_root = checkout / "libc"
    here = pathlib.Path(__file__).resolve().parent
    target = here / "libc"
    headers = closure(libc_root)
    if target.exists():
        shutil.rmtree(target)
    for header in headers:
        destination = target / header.relative_to(libc_root)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(header, destination)
    shutil.copyfile(libc_root / "LICENSE.TXT", here / "LICENSE.TXT")
    size = sum(header.stat().st_size for header in headers)
    print(f"{PINNED_TAG}: {len(headers)} headers, {size} bytes")


if __name__ == "__main__":
    main()
