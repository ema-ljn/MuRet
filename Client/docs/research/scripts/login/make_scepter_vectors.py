#!/usr/bin/env python3
"""Los vectores del brillo del cetro (CL7.6 B1, Client/tests/vectors/world55/scepter.txt): el código real
del caso 0x620 de 0x509340 (0x50a169..0x50a398), con un hueso 0 al azar en el buffer de la pose
(0x68b181c, 3 × 4 por filas), la escala y el origen del cuerpo del modelo (+0x68 y +0x6c), y el rand()
de la CRT desde una semilla al azar. Corre 0x4ec110 y 0x5491e0 reales y lee los cinco sprites que deja
en la tabla de 0x760f990, con cada float por sus bits, y el estado del rand() al final.

Uso: make_scepter_vectors.py <salida> [casos, 300 por defecto]  (con MURET_MAIN_EXE, como emu.py)"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu, SENTINEL, STACK_BASE, STACK_SIZE  # noqa: E402
from unicorn import UcError  # noqa: E402
from unicorn.x86_const import (UC_X86_REG_DS, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_EIP,  # noqa: E402
                               UC_X86_REG_ES, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPCW, UC_X86_REG_FS,
                               UC_X86_REG_SS)

CASE_START = 0x50a169
BONES = 0x68b181c
SPRITES = 0x760f990
SPRITE_SIZE = 0x1d4
COUNT = 5


def bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bone_matrix(rng):
    ax, ay, az = (math.radians(rng.uniform(-180, 180)) for _ in range(3))
    cx, sx, cy, sy, cz, sz = math.cos(ax), math.sin(ax), math.cos(ay), math.sin(ay), math.cos(az), math.sin(az)
    rows = [[cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz],
            [cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz],
            [-sy, sx * cy, cx * cy]]
    t = [rng.uniform(-150, 150) for _ in range(3)]
    m = []
    for i in range(3):
        m += [f32(rows[i][0]), f32(rows[i][1]), f32(rows[i][2]), f32(t[i])]
    return m


def run_case(e, model, owner):
    uc = e.uc
    uc.reg_write(UC_X86_REG_FPCW, e.fpcw)
    uc.reg_write(UC_X86_REG_SS, 2 << 3)
    uc.reg_write(UC_X86_REG_DS, 2 << 3)
    uc.reg_write(UC_X86_REG_ES, 2 << 3)
    uc.reg_write(UC_X86_REG_FS, (3 << 3) | 3)
    # El marco de 0x509340: [ebp] el ebp guardado, [ebp + 4] la vuelta; las variables van hasta ebp − 0x278
    # y sus argumentos de ebp + 8 a ebp + 0x28, que el caso usa de borrador.
    ebp = STACK_BASE + STACK_SIZE - 0x8000
    e.w32(ebp, 0)
    e.w32(ebp + 4, SENTINEL)
    uc.reg_write(UC_X86_REG_EBP, ebp)
    uc.reg_write(UC_X86_REG_ESP, ebp - 0x278 - 12)
    uc.reg_write(UC_X86_REG_ESI, model)
    uc.reg_write(UC_X86_REG_EBX, owner)
    e.error = None
    try:
        uc.emu_start(CASE_START, SENTINEL, count=200_000)
    except UcError as error:
        if e.error is None:
            e.error = '%s at %s' % (error, hex(uc.reg_read(UC_X86_REG_EIP)))
    if e.error:
        raise RuntimeError(e.error)
    if uc.reg_read(UC_X86_REG_EIP) != SENTINEL:
        raise RuntimeError('did not return: eip %s' % hex(uc.reg_read(UC_X86_REG_EIP)))


def main():
    out = sys.argv[1]
    cases = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    rng = random.Random(11)
    e = Emu()
    model = e.alloc(0xe0)
    owner = e.alloc(0x400)
    lines = ['scepter 1', 'cases %d' % cases]
    for k in range(cases):
        bone = bone_matrix(rng)
        e.wfv(BONES, bone)
        scale = 1.0 if k % 4 == 0 else f32(rng.uniform(0.5, 1.6))
        origin = [f32(rng.uniform(0, 25600)), f32(rng.uniform(0, 25600)), f32(rng.uniform(-200, 800))]
        e.wf(model + 0x68, scale)
        e.wfv(model + 0x6c, origin)
        seed = rng.getrandbits(32)
        e.rand_state = seed
        for slot in range(COUNT + 1):
            e.w8(SPRITES + slot * SPRITE_SIZE, 0)
        run_case(e, model, owner)
        lines.append('case %08x %s %s %s' % (seed, ' '.join(bits(x) for x in bone), bits(scale),
                                             ' '.join(bits(x) for x in origin)))
        for slot in range(COUNT):
            s = SPRITES + slot * SPRITE_SIZE
            if e.r8(s) != 1 or e.r32(s + 0x108) != owner:
                raise RuntimeError('el sprite %d no quedó' % slot)
            lines.append('sprite %x %d %s %s %s %s %s' % (
                e.r16(s + 2) & 0xffff, e.ri32(s + 4), bits(e.rf(s + 0xc)),
                ' '.join(bits(x) for x in e.rfv(s + 0x10, 3)), ' '.join(bits(x) for x in e.rfv(s + 0xf4, 3)),
                bits(e.rf(s + 0x24)), bits(e.rf(s + 0x114))))
        if e.r8(SPRITES + COUNT * SPRITE_SIZE) != 0:
            raise RuntimeError('un sexto sprite')
        lines.append('after %08x' % e.rand_state)
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('casos', cases, '->', out)


if __name__ == '__main__':
    main()
