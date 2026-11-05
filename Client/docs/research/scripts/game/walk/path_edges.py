#!/usr/bin/env python3
"""Los casos de borde del camino real (PathFinding2, 0x4eac00), en mapas armados a mano y en Lorencia:

1. un pasillo cortado por una columna de personajes (0x02), dentro y fuera de la zona segura (0x01):
   en la zona segura el segundo intento pasa por encima de los personajes;
2. la meta ocupada por otro (0x02), y con 0x20 (0x22, 0x24): dónde termina el camino;
3. Lorencia: clics sobre una pared cerca de la plaza y sobre la misma celda.

Son los de InTheSafeZoneTheSecondTryWalksThroughCharacters y AnOccupiedGoalEndsOnTheDiagonalByTheTieOrder
de la CL7.4 A.

Uso: path_edges.py      (con MURET_MAIN_EXE y el Data\\)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402
from emu_path import PathEmu  # noqa: E402


def main():
    for base, label in [(1, 'zona segura (1)'), (0, 'sin zona segura (0)')]:
        cells = [4] * 65536
        for x in range(90, 111):
            for y in range(95, 106):
                cells[y * 256 + x] = base
        for y in range(95, 106):
            cells[y * 256 + 100] |= 2          # una fila de personajes que corta el pasillo
        r = PathEmu(cells).find(95, 100, 105, 100)
        print(label, 'de (95,100) a (105,100) con la columna x=100 ocupada:', r[0] if r else None)
    cells = [0] * 65536
    pe = PathEmu(cells)
    for value in (0x02, 0x22, 0x24):
        cells[100 * 256 + 105] = value
        pe.set_cells(cells)
        r = pe.find(100, 100, 105, 100)
        print('meta con 0x%02x:' % value, r[0] if r else None)
    _, cells = common.att_cells(1)
    pe = PathEmu(cells)
    for s, t in [((135, 125), (140, 125)), ((135, 125), (120, 110)), ((135, 125), (135, 125))]:
        r = pe.find(*s, *t)
        print('Lorencia', s, '->', t, 'att', cells[t[1] * 256 + t[0]], ':', r[0] if r else None)


if __name__ == '__main__':
    main()
