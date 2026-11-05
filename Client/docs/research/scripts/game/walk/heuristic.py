#!/usr/bin/env python3
"""La h del A* real: el tramo 0x4eb2a1..0x4eb301 de 0x4ead70, que da la prioridad de un vecino.

Corre el tramo solo, con el vecino en EAX/EBX, la meta en [EBP+0x10]/[EBP+0x14] y el costo del padre en
0, para varias distancias (dx, dy) a la meta. El tramo deja en EAX el numerador, 15 · ||dx| − |dy|| +
21 · min(|dx|, |dy|) + 3, y el `sar eax,2` de 0x4eb301 lo divide por 4: la h. Muestra Q-C159:
con dx = dy = 1, 0x4eb2c2 pone la dy en 0 y la diagonal de al lado de la meta da h = 18 / 4 = 4, lo
mismo que el vecino de al lado (TheDiagonalNextToTheGoalCostsLikeTheSide de la CL7.4 A).

Uso: heuristic.py      (con MURET_MAIN_EXE)
"""
import os
import sys

from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ESI,
                               UC_X86_REG_ESP)

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402
import emulate_cloth as ec  # noqa: E402

H_START, H_END = 0x4eb2a1, 0x4eb301


def main():
    e = ec.ClothEmulator(common.main_exe())
    uc = e.uc
    frame = e.alloc(0x100) + 0x80

    def h(nx, ny, tx, ty):
        uc.reg_write(UC_X86_REG_EBP, frame)
        uc.reg_write(UC_X86_REG_ESP, frame - 0x40)
        e.w32(frame + 0x10, tx)
        e.w32(frame + 0x14, ty)
        uc.reg_write(UC_X86_REG_EAX, nx)
        uc.reg_write(UC_X86_REG_EBX, ny)
        esi = e.alloc(0x500)                 # el PATH: +0x408 es el arreglo de costos
        uc.reg_write(UC_X86_REG_ESI, esi)
        e.w32(esi + 0x408, e.alloc(0x10))
        e.w32(frame - 0x2c, 0)               # el índice del padre: su costo es cost[0]
        uc.emu_start(H_START, H_END)
        return uc.reg_read(UC_X86_REG_EAX) & 0xffffffff

    for d in [(0, 0), (1, 0), (0, 1), (1, 1), (2, 1), (2, 2), (3, 0), (3, 3), (5, 2)]:
        n = h(100 + d[0], 100 + d[1], 100, 100)
        print('(dx, dy) =', d, 'numerador', n, 'h', n >> 2)


if __name__ == '__main__':
    main()
