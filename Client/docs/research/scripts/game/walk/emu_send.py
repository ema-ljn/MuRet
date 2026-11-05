#!/usr/bin/env python3
"""El paquete de caminar real: SendMove (0x56acd0), con `send` enganchado para ver los bytes que salen.

`SendEmu.send_move(camino, blanco, ángulo)` escribe el camino en el personaje (+0x3ea la cantidad,
+0x3eb.. las x, +0x3fa.. las y), el ángulo (+0x24) y el blanco (0x77e1ba8, 0x77e1bac), corre 0x56acd0 y
devuelve lo que pasó a `send` (el C1 con XOR32, sin la capa de FoxAnti), el +0x378 que queda en el
personaje y MouseUpdateTimeMax (0x6c8dcc). Los paneles que podrían frenarlo (0x659290, 0x65cae0)
devuelven 0, y el socket (0x6c82a0) es uno de mentira que no está cerrado.

Como script: el caso del cable (b6,73) con tres pasos, 15 celdas que salen como 14, la dirección por el
ángulo con una celda, el blanco al lado del final, PathNum 0 y el bit 0x20 de +0x78. Es la base de
make_walk_packet_vectors.py de la CL7.4 B.

Uso: emu_send.py      (con MURET_MAIN_EXE)
"""
import os
import sys

from unicorn import UC_HOOK_CODE

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402
import emulate_cloth as ec  # noqa: E402

SEND_MOVE = 0x56acd0
XOR32 = bytes.fromhex('e76d3a89bcb29f7323a8feb6495d395d8acb638dea7d2b5fc3b1e9832951e856')
DIRS = [(-1, -1), (0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0)]


def dexor32(p):
    """El C1 en claro: deshace el XOR32 del envío (03, Envío) desde el byte 3."""
    out = bytearray(p)
    for i in range(len(p) - 1, 2, -1):
        out[i] = p[i] ^ XOR32[i & 31] ^ p[i - 1]
    return bytes(out)


class SendEmu:
    def __init__(self):
        self.sent = []
        e = self.e = ec.ClothEmulator(common.main_exe(), stub_overrides={'send': self._on_send})

        def returns_zero(n):
            def hook(_uc, _a, _s, _d):
                e._return(0, pop_args=n)
            return hook
        e.uc.hook_add(UC_HOOK_CODE, returns_zero(1), begin=0x659290, end=0x659290)   # ¿panel abierto? no
        e.uc.hook_add(UC_HOOK_CODE, returns_zero(2), begin=0x65cae0, end=0x65cae0)
        self.sock = e.alloc(0x4020)        # PTR_DAT_006c82a0: +0xc != -1 (conectado)
        e.w32(0x6c82a0, self.sock)
        e.w32(self.sock + 0xc, 0x123)
        e.w32(self.sock + 0x4018, 0)
        self.c = e.alloc(0x1000)

    def _on_send(self, esp):
        e = self.e
        buf, n = e.r32(esp + 8), e.r32(esp + 12)
        self.sent.append(bytes(e.uc.mem_read(buf, n)))
        e._return(n, pop_args=4)

    def send_move(self, path, target, angle=0.0, flags78=0):
        e, c = self.e, self.c
        e.uc.mem_write(c, b'\0' * 0x1000)
        e.w8(c + 0x3e8, 0)
        e.w8(c + 0x3e9, 0)
        e.w8(c + 0x3ea, len(path))
        for i, (x, y) in enumerate(path[:15]):
            e.w8(c + 0x3eb + i, x)
            e.w8(c + 0x3fa + i, y)
        e.w32(c + 0x78, flags78)
        e.wf(c + 0x24, angle)
        e.w32(0x77e1ba8, target[0])
        e.w32(0x77e1bac, target[1])
        e.w32(0x7d3a10c, 0)
        e.w32(0x78abb00, 0)
        self.sent = []
        e.call(SEND_MOVE, args=(c, c))
        return self.sent, e.r8(c + 0x378), e.ri32(0x6c8dcc)


def walk(x, y, dirs):
    p = [(x, y)]
    for d in dirs:
        x += DIRS[d][0]
        y += DIRS[d][1]
        p.append((x, y))
    return p


def main():
    s = SendEmu()
    # El caso del cable: c1 08 a3 b6 73 73 76 70 en claro (el cable lleva A3 por FoxAnti).
    p = walk(0xb6, 0x73, [7, 6, 7])
    sent, mv, maxt = s.send_move(p, p[-1])
    for pk in sent:
        print('emulado:', pk.hex(' '), '-> en claro', dexor32(pk).hex(' '), 'Movement', mv, 'MouseUpdateTimeMax', maxt)
    p = walk(100, 100, [3] * 14)
    sent, mv, maxt = s.send_move(p, p[-1])
    for pk in sent:
        print('15 celdas:', dexor32(pk).hex(' '), 'Movement', mv, 'Max', maxt)
    for a in [0.0, 22.4, 22.5, 45.0, 90.0, 180.0, 270.0, 337.5, 359.0, -10.0]:
        sent, mv, maxt = s.send_move([(50, 60)], (50, 60), angle=a)
        print('ángulo %6.1f' % a, dexor32(sent[0]).hex(' '), 'Max', maxt)
    p = walk(0xa9, 0x7e, [3])
    sent, mv, maxt = s.send_move(p, (0xab, 0x7d))
    print('blanco al lado:', dexor32(sent[0]).hex(' '))
    sent, mv, maxt = s.send_move([], (1, 1))
    print('PathNum 0:', sent, 'Movement', mv, 'Max', maxt)
    sent, mv, maxt = s.send_move(walk(10, 10, [3]), (11, 10), flags78=0x20)
    print('bit 0x20:', sent, 'Movement', mv)


if __name__ == '__main__':
    main()
