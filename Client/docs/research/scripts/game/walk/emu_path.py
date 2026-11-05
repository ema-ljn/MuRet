#!/usr/bin/env python3
"""El camino real del original: PathFinding2 (0x4eac00) con su A* (0x4ead70), sobre unicorn.

`PathEmu(celdas)` arma el PATH global como el juego (el constructor estático 0x4eaaa0 y 0x4eaaf0, que lo
deja de 256 × 256 con el mapa en 0x7d0d348) y escribe el mapa de paredes: un WORD por celda con el
byte bajo del .att (0x01 zona segura, 0x02 personaje, 0x04 pared, 0x08 sin dibujo, 0x20...).
`find(sx, sy, tx, ty)` llama a 0x4eac00 y devuelve el camino (hasta 15 celdas, del buffer de 0x21
bytes que llena: [2] la cantidad, [3..] las x, [0x12..] las y) y los dos primeros bytes, o None si no
hay camino. Lo usan path_limits.py, path_edges.py, wire_vs_path.py y, en la CL7.4 A,
make_path_vectors.py.

Como script, unos casos sobre el .att de Lorencia (world1): rectas, diagonales, el mismo punto, lejos,
la fila y la columna 0 (que fallan) y un caso del cable.

Uso: emu_path.py      (con MURET_MAIN_EXE y el Data\\)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402
import emulate_cloth as ec  # noqa: E402

WALL = 0x7d0d348               # el mapa de paredes vivo (TerrainWall), un WORD por celda
PATH_OBJ = 0x5767b74           # el puntero al PATH global
PATH_CTOR, PATH_INIT = 0x4eaaa0, 0x4eaaf0
PATH_FINDING2, FIND_PATH = 0x4eac00, 0x4ead70


class PathEmu:
    def __init__(self, cells, fpcw=0x27f):
        self.e = ec.ClothEmulator(common.main_exe(), fpu_control_word=fpcw)
        self.e.call(PATH_CTOR)
        self.e.call(PATH_INIT)
        self.set_cells(cells)
        self.buf = self.e.alloc(0x40)

    def set_cells(self, cells):
        self.e.uc.mem_write(WALL, struct.pack('<65536H', *cells))

    def find(self, sx, sy, tx, ty, dist=0.0):
        self.e.uc.mem_write(self.buf, b'\xcd' * 0x21)
        d = struct.unpack('<I', struct.pack('<f', dist))[0]
        al = self.e.call(PATH_FINDING2, args=(sx, sy, tx, ty, self.buf, d)) & 0xff
        b = bytes(self.e.uc.mem_read(self.buf, 0x21))
        if not al:
            return None
        n = b[2]
        return [(b[3 + i], b[0x12 + i]) for i in range(n)], b[0], b[1]

    def find_path(self, sx, sy, tx, ty, error_check, wall, flag=0, dist=0):
        """Una pasada de 0x4ead70 sola: (al, NumPath del PATH)."""
        obj = self.e.r32(PATH_OBJ)
        al = self.e.call(FIND_PATH, args=(sx, sy, tx, ty, error_check, wall, flag, dist), ecx=obj) & 0xff
        return al, self.numpath()

    def numpath(self):
        return self.e.ri32(self.e.r32(PATH_OBJ) + 0x10)


def main():
    _, cells = common.att_cells(1)
    pe = PathEmu(cells)
    cases = [(130, 130, 135, 130), (130, 130, 140, 140), (130, 130, 130, 130), (130, 130, 131, 131),
             (130, 130, 160, 160), (130, 130, 200, 50), (0, 5, 3, 5), (5, 0, 5, 3), (183, 116, 186, 116)]
    for c in cases:
        r = pe.find(*c)
        print(c, 'att(inicio)=%d att(meta)=%d' % (cells[c[1] * 256 + c[0]], cells[c[3] * 256 + c[2]]),
              'numpath(obj)=%d' % pe.numpath(), r)


if __name__ == '__main__':
    main()
