#!/usr/bin/env python3
"""Cuántos rand() gasta cerrar una variable de la tabla de protección (0x57630c8), con el código real.

La tabla guarda cifradas la celda del héroe, el contador de warp 0x77f3b04, el contador de frames
0x7d3a0f0, el registro propio 0x7681eb4 y otras (06, La tabla de protección). Cada acceso que se
cierra las vuelve a cifrar con rand(). Este script corre los dos cierres que fija la decisión 3 del
plan de la CL7:
- 0x40b4d0, el del registro propio (0xf08 bytes): 3.848 rand(), uno por byte;
- 0x412890, el de una variable chica (4 bytes, Q-C125): 4 rand().

El rand() es el de la CRT (0x69fd70) desde el estado 1; la salida es lo que gastó y el estado que
quedó. Es la base de los vectores de la CL7.P (una clase de acceso por test).

Uso: protection_rand.py      (con MURET_MAIN_EXE)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402
import emulate_cloth as ec  # noqa: E402

TABLE = ec.MAP_OBJECT          # 0x57630c8
CLOSE_RECORD = 0x40b4d0        # cierra el registro propio
CLOSE_SMALL = ec.ENCRYPT       # 0x412890, cierra una variable de 4 bytes
RECORD_SIZE = 0xf08


def main():
    e = ec.ClothEmulator(common.main_exe())
    record, entry = e.alloc(RECORD_SIZE), e.alloc(RECORD_SIZE + 1)
    r = ec.Rand({'msvc_state': 1})
    e.call(CLOSE_RECORD, args=(record, entry), ecx=TABLE, rand=r)
    print('rand() en 0x40b4d0 (el registro de 0x%x bytes):' % RECORD_SIZE, r.describe())
    value, entry = e.alloc(4), e.alloc(5)
    r = ec.Rand({'msvc_state': 1})
    e.call(CLOSE_SMALL, args=(value, entry), ecx=TABLE, rand=r)
    print('rand() en 0x412890 (una variable de 4 bytes):', r.describe())


if __name__ == '__main__':
    main()
