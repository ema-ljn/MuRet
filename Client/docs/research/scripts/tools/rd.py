#!/usr/bin/env python3
"""Lee constantes de la imagen del main.exe: floats, doubles, enteros, strings y punteros a string.

Cada argumento es una VA en hexadecimal con su tipo después de dos puntos (float si no lo lleva):
- `f`: el float y sus bits; `d`: el double; `i`: el int32 con signo y en hexadecimal;
- `s`: la string de la dirección; `p`: el puntero de la dirección y la string a la que apunta.

Sirve para dar una constante "por sus bits", como piden los planes (0x6b87b4, el 0,4 de los árboles).
Lee los bytes del archivo: un global que el juego escribe al arrancar vale acá lo de la .data.

Uso: rd.py dirección[:tipo] [...] [--exe RUTA]
Ejemplo: rd.py 6ba360 6c9e0c:i 6d2aec:s
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import image  # noqa: E402


def show(img, va, kind):
    raw = img.read(va, 8)
    if kind == 'f':
        return '%s f %r %s' % (hex(va), img.f32(va), raw[:4].hex())
    if kind == 'd':
        return '%s d %r' % (hex(va), img.f64(va))
    if kind == 'i':
        v = img.i32(va)
        return '%s i %d %s' % (hex(va), v, hex(v & 0xffffffff))
    if kind == 's':
        return '%s s %r' % (hex(va), img.cstr(va, 64))
    if kind == 'p':
        p = img.u32(va)
        return '%s p %s %r' % (hex(va), hex(p), img.cstr(p, 64) if img.contains(p) else b'?')
    raise SystemExit('tipo desconocido: %s (f, d, i, s o p)' % kind)


def main():
    ap = argparse.ArgumentParser(description='Lee constantes de la imagen del main.exe.')
    ap.add_argument('values', nargs='+', help='VA[:tipo], con tipo f, d, i, s o p')
    ap.add_argument('--exe', help='el main.exe (por defecto, MURET_MAIN_EXE)')
    args = ap.parse_args()
    img = image.Image(args.exe)
    for text in args.values:
        va, _, kind = text.partition(':')
        print(show(img, int(va, 16), kind or 'f'))


if __name__ == '__main__':
    main()
