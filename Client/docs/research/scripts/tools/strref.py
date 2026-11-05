#!/usr/bin/env python3
"""Dónde está un texto en la imagen del main.exe y qué `push` lo usan.

Para cada vez que el texto aparece, da la string entera que lo contiene, su VA y las primeras seis
instrucciones `push imm32` con esa VA (así se cargan los nombres de archivo: `push "Data\\..."`). Una
string que se pasa por `mov` o por una tabla no sale en los `push`: buscarla en el listado.

Uso: strref.py texto [texto ...] [--exe RUTA]
Ejemplo: strref.py FireLight Gate.bmd    (los modelos van sin el número: "FireLight" y el 01 se arma
         al cargar, en 0x619ae3; Gate.bmd, en 0x61ea90 y 0x61eb46)
"""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import image  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description='Dónde está un texto en el main.exe y qué push lo usan.')
    ap.add_argument('texts', nargs='+')
    ap.add_argument('--exe', help='el main.exe (por defecto, MURET_MAIN_EXE)')
    args = ap.parse_args()
    img = image.Image(args.exe)
    data = img.bytes
    for text in args.texts:
        needle = text.encode('latin1')
        i = data.find(needle)
        while i != -1:
            s = i
            while s > 0 and data[s - 1] != 0:
                s -= 1
            whole = data[s:data.find(b'\0', s)].decode('latin1')
            va = img.base + s
            refs = []
            pattern = b'\x68' + struct.pack('<I', va)
            j = data.find(pattern)
            while j != -1 and len(refs) < 6:
                refs.append(hex(img.base + j))
                j = data.find(pattern, j + 1)
            print(text, hex(va), repr(whole), refs)
            i = data.find(needle, i + 1)


if __name__ == '__main__':
    main()
