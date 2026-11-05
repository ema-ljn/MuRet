#!/usr/bin/env python3
"""Los vectores de las partículas del login (CL7.6 B2, Client/tests/vectors/world55/particles.txt): el
código real de 0x53fe70 (crear), 0x5446e0 (el paso, con el viento de 0x7609bb8..0x7609bc8) y 0x548550(0)
(el dibujo), con el aliento del caballo (el tipo 0x5f2, subtipo 0) como lo pide 0x444010 y, en los casos
del final (CL7.6 C2), la chispa del cuervo (el 0x5c6, subtipo 5, escala 0,8) como la pide 0x416a20: en
cada paso, a veces, una nueva en el primer lugar libre, y después el paso y el dibujo. De cada uno se guarda
el viento, el estado del rand() de la CRT, los 0x78 bytes de cada lugar usado de la tabla (0x743a198, en hexadecimal sin ceros a la izquierda) y
lo que el dibujo le pide a 0x621400 (que no corre: se lee y se vuelve) y a las mezclas.

Uso: make_particle_vectors.py <salida> [casos del aliento, 20 por defecto]  (con MURET_MAIN_EXE, como
emu.py). Después van los SPARK_CASES de la chispa: el test espera 20 + 8."""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ESP  # noqa: E402

CREATE, STEP, DRAW, BILLBOARD = 0x53fe70, 0x5446e0, 0x548550, 0x621400
BLENDS = {0x620a90: 'additive', 0x620a00: 'alpha', 0x620b10: 'subtractive', 0x620850: 'x850', 0x620870: 'x870'}
TABLE, SIZE, SLOTS = 0x743a198, 0x78, 24
WIND = 0x7609bb8  # bb8, bbc: el viento; bc4, bc8: lo que cambia por paso
CLOCK = 0x5767b88
TEXTURES = 0x7d3a198
BREATH = 0x5f2
SPARK = 0x5c6
STEPS = 20
# Los casos de la chispa del cuervo, después de los del aliento (que no cambian): el tipo, el subtipo, la
# escala y la luz de 0x417b43 (0,3; 0,4; 0,7).
SPARK_CASES = 8
SPARK_SCALE = struct.unpack('<f', struct.pack('<I', 0x3f4ccccd))[0]
SPARK_LIGHT = [struct.unpack('<f', struct.pack('<I', w))[0] for w in (0x3e99999a, 0x3ecccccd, 0x3f333333)]


def bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def main():
    out = sys.argv[1]
    cases = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    rng = random.Random(23)
    e = Emu()
    calls = []

    def on_billboard():
        esp = e.uc.reg_read(UC_X86_REG_ESP)
        a = [e.r32(esp + 4 + 4 * k) for k in range(10)]
        calls.append('billboard %x %s %08x %08x %s %08x %08x %08x %08x %08x' % (
            a[0], ' '.join(bits(x) for x in e.rfv(a[1], 3)), a[2], a[3], ' '.join(bits(x) for x in e.rfv(a[4], 3)),
            a[5], a[6], a[7], a[8], a[9]))
        e.ret(0)

    e.at(BILLBOARD, on_billboard)
    for address, name in BLENDS.items():
        e.at(address, lambda name=name: calls.append('blend ' + name))
    pos, ang, light = e.alloc(12), e.alloc(12), e.alloc(12)
    lines = ['particles 2', 'cases %d' % (cases + SPARK_CASES)]
    for k in range(cases + SPARK_CASES):
        kind, subtype, scale = (BREATH, 0, 1.0) if k < cases else (SPARK, 5, SPARK_SCALE)
        for slot in range(SLOTS + 1):
            e.uc.mem_write(TABLE + slot * SIZE, b'\0' * SIZE)
        wind = [f32(rng.uniform(-0.3, 0.3)) for _ in range(2)] + [f32(rng.uniform(-0.05, 0.05)) for _ in range(2)]
        if k % 5 == 0:
            wind = [0.0, 0.0, 0.0, 0.0]
        e.wfv(WIND, wind[:2])
        e.wfv(WIND + 0xc, wind[2:])
        width = rng.choice([32.0, 64.0, 128.0])
        height = rng.choice([32.0, 64.0, 128.0])
        components = 4 if k % 7 == 3 else 3
        e.wf(TEXTURES + kind * 0x38 + 0x20, width)
        e.wf(TEXTURES + kind * 0x38 + 0x24, height)
        e.w8(TEXTURES + kind * 0x38 + 0x28, components)
        seed = rng.getrandbits(32)
        e.rand_state = seed
        lines.append('case %08x %s %s %s %d %x %d %s' % (seed, ' '.join(bits(x) for x in wind), bits(width),
                                                          bits(height), components, kind, subtype, bits(scale)))
        for step in range(STEPS):
            clock = f32(rng.uniform(0, 2_000_000)) if step % 4 else f32(rng.uniform(0, 400))
            e.wf(CLOCK, clock)
            born = rng.random() < 0.4 or step == 0
            if born:
                p = [f32(rng.uniform(0, 25600)), f32(rng.uniform(0, 25600)), f32(rng.uniform(-100, 1500))]
                a = [f32(rng.uniform(-180, 180)), f32(rng.uniform(-180, 180)), f32(rng.uniform(-360, 360))]
                lt = [3.0, 3.0, 3.0] if step % 3 else [f32(rng.uniform(0, 3)) for _ in range(3)]
                if kind == SPARK and step % 3:
                    lt = SPARK_LIGHT
                e.wfv(pos, p)
                e.wfv(ang, a)
                e.wfv(light, lt)
                e.call(CREATE, [kind, pos, ang, light, subtype, e.fbits(scale), 0])
                lines.append('create %s %s %s %s' % (bits(clock), ' '.join(bits(x) for x in p),
                                                     ' '.join(bits(x) for x in a), ' '.join(bits(x) for x in lt)))
            e.call(STEP, [])
            lines.append('step %s %08x' % (' '.join(bits(x) for x in e.rfv(WIND, 2) + e.rfv(WIND + 0xc, 2)),
                                          e.rand_state))
            for slot in range(SLOTS):
                raw = bytes(e.uc.mem_read(TABLE + slot * SIZE, SIZE))
                if any(raw):
                    lines.append('slot %d %s' % (slot, ' '.join('%x' % w for w in struct.unpack('<30I', raw))))
            if e.r8(TABLE + SLOTS * SIZE) != 0:
                raise RuntimeError('más lugares que los previstos')
            calls.clear()
            e.call(DRAW, [0])
            lines.append('draw %d' % len(calls))
            lines.extend(calls)
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('casos', cases, '->', out)


if __name__ == '__main__':
    main()
