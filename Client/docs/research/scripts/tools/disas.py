#!/usr/bin/env python3
"""Desensambla un rango del main.exe con capstone, sin la caché (lee el exe directo).

Sirve para leer una función o un tramo con las mismas direcciones que el research. Para el mismo
rango en el listado de objdump, listing.py.

Uso: disas.py inicio fin [--exe RUTA] > tramo.asm     (en hexadecimal; fin no se incluye)
"""
import argparse
import os
import sys

import capstone

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import image  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description='Desensambla [inicio, fin) del main.exe.')
    ap.add_argument('start', help='la VA de inicio, en hexadecimal')
    ap.add_argument('end', help='la VA de fin, en hexadecimal')
    ap.add_argument('--exe', help='el main.exe (por defecto, MURET_MAIN_EXE)')
    args = ap.parse_args()
    img = image.Image(args.exe)
    start, end = int(args.start, 16), int(args.end, 16)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    for i in md.disasm(img.read(start, end - start), start):
        print(hex(i.address), i.mnemonic, i.op_str)


if __name__ == '__main__':
    main()
