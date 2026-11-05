#!/usr/bin/env python3
"""La tabla de texturas de Lorencia en la memoria del original: el volcado lor_slots.bin (la tabla de
0x7d3a198, 1.845 slots de 0x38 bytes: el nombre en los primeros 32 y seis dwords: el ancho y el alto en
float, los canales, el nombre de textura de GL en +0x2c, ...).

Imprime cada slot con nombre: el índice, el archivo y los seis valores en hexadecimal. Es el oráculo del
golden de los slots de la CL7.L2 (0x41c..0x47e son las 99 texturas de Object1\\ en su orden; bird.tga y
fish.tga en 0x69 y 0x6a; 09 § Las texturas de las trazas ya tienen archivo).

El volcado es del 2026-10-09, con el build 2f16a9c6 (el .text es el mismo que el del c32bbd5f).

Uso: slots.py [lor_slots.bin]      (por defecto, el de MURET_CLIENT_MEMORY_DIR/2026-10-09-lorencia)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402

SLOT_SIZE = 0x38


def read_slots(path=None):
    """(índice, nombre, seis dwords) de cada slot del volcado; el cuarto es el nombre de GL (+0x2c)."""
    with open(path or common.memory_file('lor_slots.bin'), 'rb') as f:
        data = f.read()
    for i in range(len(data) // SLOT_SIZE):
        entry = data[i * SLOT_SIZE:(i + 1) * SLOT_SIZE]
        yield i, entry[:32].split(b'\0')[0].decode('latin1'), struct.unpack('<6I', entry[32:56])


def main():
    for i, name, values in read_slots(sys.argv[1] if len(sys.argv) > 1 else None):
        if name:
            print('%#05x %-40s %s' % (i, name, ' '.join('%x' % v for v in values)))


if __name__ == '__main__':
    main()
