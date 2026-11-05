#!/usr/bin/env python3
"""Los vectores de la cabeza del personaje (cierre del login, Client/tests/vectors/world55/head.txt): el
código real del tramo de 0x505de0 que mueve la cabeza (0x5068eb..0x5069ab), con 0x4e96f0 (la distancia de
dos ángulos) y 0x4e9560 (el giro de un ángulo hacia otro).

El tramo corre con el personaje del login (el modelo 0x20a, que no es el héroe de 0x743a170 ni se está
muriendo, +0x389): con rand() % 32 en 0 (0x5068fd) sortea el blanco nuevo, +0x34 = rand() % 128 − 64 y
+0x38 = rand() % 32 − 16, con 360 más si da negativo; después, cada ángulo de la cabeza (+0x28 y +0x2c) gira
hacia su blanco a lo sumo el 20 % de la distancia. Con `hero` el personaje es el héroe: no sortea, solo
gira.

Dos clases de casos. Los 'walk' corren pasos seguidos desde la cabeza en (a, b) y el blanco en (c, d),
como en el login. Los 'pairs' ponen en cada paso una cabeza y un blanco nuevos (de un LCG propio, con
negativos, el 0, el 180 y el 360 cerca) y el rand() de la CRT sigue; así se cubren las ramas de los dos
ángulos. De cada paso se guarda el estado del rand(), la cabeza y el blanco que entraron (en 'pairs') y
los que quedaron, en hexadecimal.

Uso: make_head_vectors.py <salida>  (con MURET_MAIN_EXE)"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import emulate_cloth as ec  # noqa: E402

EXE = os.environ['MURET_MAIN_EXE']
BLOCK_START, BLOCK_END = 0x5068eb, 0x5069ab
HERO = 0x743a170
HEAD, TARGET = 0x28, 0x34
DYING = 0x389

# (semilla, clase, pasos, el héroe, la cabeza y el blanco del principio)
CASES = [
    (0x1234, 'walk', 600, False, (0.0, 0.0, 0.0, 0.0)),
    (0xbeef, 'walk', 600, False, (0.0, 0.0, 0.0, 0.0)),
    (0x5eed, 'walk', 400, False, (350.0, 10.0, 20.0, 340.0)),
    (0x77, 'walk', 200, True, (300.0, 5.0, 10.0, 200.0)),
    (0x99, 'pairs', 800, False, None),
    (0x4242, 'pairs', 800, True, None),
]


def bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


class Lcg:
    """Un LCG propio para las cabezas y los blancos de los 'pairs' (el test no lo repite: lee la entrada)."""

    def __init__(self, seed):
        self.state = seed & 0xffffffff

    def next(self):
        self.state = (self.state * 1103515245 + 12345) & 0xffffffff
        return self.state >> 8

    def angle(self):
        kind = self.next() % 8
        if kind == 0:
            return f32([0.0, 180.0, 360.0, -0.0, 179.99998, 180.00002, 359.99997, 0.00001][self.next() % 8])
        if kind == 1:
            return f32(-(self.next() % 36000) / 100.0)
        return f32((self.next() % 72000) / 200.0)


def run_case(seed, kind, steps, hero, start, lines):
    emu = ec.ClothEmulator(EXE)
    uc = emu.uc
    emu.rand_main = ec.Rand({'msvc_state': seed})
    emu.construct_statics()
    character = ec.CHAR_BASE
    uc.mem_write(character, b'\0' * 0x438)
    emu.w8(character, 1)
    emu.w16(character + 2, 0x20a)
    emu.w8(character + DYING, 0)
    emu.w32(HERO, character if hero else 0)
    if start is not None:
        emu.wfv(character + HEAD, [start[0], start[1]])
        emu.wfv(character + TARGET, [start[2], start[3]])
    lines.append('case %08x %s %d %d' % (seed, kind, steps, 1 if hero else 0))
    lcg = Lcg(seed ^ 0x5a5a5a5a)
    rand = emu.rand_main
    for _ in range(steps):
        given = ''
        if kind == 'pairs':
            entered = [lcg.angle(), lcg.angle(), lcg.angle(), lcg.angle()]
            emu.wfv(character + HEAD, entered[:2])
            emu.wfv(character + TARGET, entered[2:])
            given = ' ' + ' '.join(bits(v) for v in entered)
        emu._reset_cpu()
        ebp = ec.STACK_BASE + ec.STACK_SIZE - 0x8000
        uc.mem_write(ebp - 0x400, b'\0' * 0x420)
        emu.w32(ebp + 8, character)
        emu.w32(ebp + 0xc, character)
        uc.reg_write(ec.UC_X86_REG_EBP, ebp)
        uc.reg_write(ec.UC_X86_REG_ESP, ebp - 0x100)
        uc.reg_write(ec.UC_X86_REG_EBX, character)
        uc.reg_write(ec.UC_X86_REG_ESI, character)
        uc.reg_write(ec.UC_X86_REG_EDI, 0)
        previous = emu.rand_current
        emu.rand_current = rand
        emu._emulate(BLOCK_START, BLOCK_END)
        emu.rand_current = previous
        lines.append('step %08x%s %s %s' % (rand.state, given,
                                            ' '.join(bits(v) for v in emu.rfv(character + HEAD, 2)),
                                            ' '.join(bits(v) for v in emu.rfv(character + TARGET, 2))))


def main():
    out = sys.argv[1]
    lines = ['head 1']
    for case in CASES:
        run_case(*case, lines)
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('casos', len(CASES), '->', out)


if __name__ == '__main__':
    main()
