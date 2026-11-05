#!/usr/bin/env python3
"""Los imports del main.exe: la VA del slot de la IAT, la DLL y el nombre.

Sirve para leer un `call DWORD PTR ds:0x6b8...` del listado (qué función de Windows o de GL es). Sin
direcciones, lista todos.

Uso: imp.py [slot ...] [--exe RUTA]       (en hexadecimal)
Ejemplo: imp.py 6b8588                    (el slot de send, el que pisa FoxAnti)
"""
import argparse
import os
import sys

import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import image  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description='Los imports del main.exe por slot de la IAT.')
    ap.add_argument('slots', nargs='*', help='VAs de slots de la IAT, en hexadecimal')
    ap.add_argument('--exe', help='el main.exe (por defecto, MURET_MAIN_EXE)')
    args = ap.parse_args()
    pe = pefile.PE(image.exe_path(args.exe))
    want = {int(a, 16) for a in args.slots}
    for entry in pe.DIRECTORY_ENTRY_IMPORT:
        for imp in entry.imports:
            if not want or imp.address in want:
                name = imp.name.decode() if imp.name else 'ord%d' % imp.ordinal
                print(hex(imp.address), entry.dll.decode(), name)


if __name__ == '__main__':
    main()
