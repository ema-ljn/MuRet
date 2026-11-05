#!/usr/bin/env python3
"""Los topes de iteraciones del A* real (0x4ead70) en un mapa vacío: el de 500 de la búsqueda exacta
(la primera pasada, bErrorCheck = 1) y el de 50 de la más cercana (la segunda, bErrorCheck = 0).

Corre cada pasada sola, con una recta en +x y una diagonal de largo creciente, y da (al, NumPath) de
cada una; después el camino entero de PathFinding2 en saltos de caballo (2, 1) · k, que muestra el
corte en 15 celdas. Es lo que fija TheExactSearchGivesUpAfter500AndTheNearestAfter50 y
OnlyTheFirstFifteenCellsAreKept de la CL7.4 A.

Uso: path_limits.py      (con MURET_MAIN_EXE)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu_path import PathEmu  # noqa: E402


def main():
    pe = PathEmu([0] * 65536)

    def first(sx, sy, tx, ty):
        return pe.find_path(sx, sy, tx, ty, 1, 2)

    def second(sx, sy, tx, ty):
        return pe.find_path(sx, sy, tx, ty, 0, 2)

    print('recta en +x (al, NumPath):')
    for d in [5, 10, 20, 30, 40, 50, 60, 80, 100, 120]:
        print(' d=%d primera:' % d, first(100, 100, 100 + d, 100), 'segunda:', second(100, 100, 100 + d, 100))
    print('diagonal:')
    for d in [5, 10, 20, 30, 40, 60, 80, 100]:
        print(' d=%d primera:' % d, first(50, 50, 50 + d, 50 + d), 'segunda:', second(50, 50, 50 + d, 50 + d))
    print('caballo (2, 1) · k, el camino entero:')
    for k in [1, 3, 5, 10, 20]:
        r = pe.find(100, 100, 100 + 2 * k, 100 + k)
        print(' k=%d' % k, r[0] if r else None)


if __name__ == '__main__':
    main()
