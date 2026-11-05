#!/usr/bin/env python3
"""Los vectores del billboard del original (CL7.6 B1, Client/tests/vectors/world55/billboard.txt): el
código real de 0x621400(textura, posición, ancho, alto, luz, rotación, u, v, du, dv), el cuadrado de los
sprites y las partículas, con la vista de la cámara (0x7d366f0, 3 × 4 por filas) y los componentes de
la textura (0x7d3a1c0 + slot · 0x38) al azar, y las llamadas a GL que hace (glColor, glTexCoord2f y
glVertex3fv), con cada float por sus bits.

Uso: make_billboard_vectors.py <salida> [casos, 300 por defecto]  (con MURET_MAIN_EXE, como emu.py)"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu  # noqa: E402

VIEW = 0x7d366f0
TEXTURES = 0x7d3a198
COMPONENTS = 0x28  # 0x7d3a1c0 - 0x7d3a198
SLOT_SIZE = 0x38


def bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def view_matrix(rng):
    yaw, pitch = math.radians(rng.uniform(-180, 180)), math.radians(rng.uniform(-100, -40))
    cy, sy, cp, sp = math.cos(yaw), math.sin(yaw), math.cos(pitch), math.sin(pitch)
    rows = [[cy, -sy, 0.0], [sy * cp, cy * cp, -sp], [sy * sp, cy * sp, cp]]
    t = [rng.uniform(-20000, 20000) for _ in range(3)]
    m = []
    for i in range(3):
        m += [f32(rows[i][0]), f32(rows[i][1]), f32(rows[i][2]), f32(t[i])]
    return m


def main():
    out = sys.argv[1]
    cases = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    rng = random.Random(7)
    e = Emu()
    pos = e.alloc(12)
    light = e.alloc(12)
    lines = ['billboard 1', 'cases %d' % cases]
    for k in range(cases):
        view = view_matrix(rng)
        e.wfv(VIEW, view)
        slot = rng.choice([0x5ac, 0x5fd, 0x5f2, 0x635, 0x5e4, 0x622])
        components = 3 if k % 3 else 4
        e.w8(TEXTURES + slot * SLOT_SIZE + COMPONENTS, components)
        p = [f32(rng.uniform(5000, 20000)), f32(rng.uniform(5000, 25000)), f32(rng.uniform(0, 4000))]
        e.wfv(pos, p)
        lt = [f32(rng.uniform(0, 3)) for _ in range(3)]
        e.wfv(light, lt)
        w, h = f32(rng.uniform(5, 300)), f32(rng.uniform(5, 300))
        rot = 0.0 if k % 2 == 0 else f32(rng.uniform(-360, 360))
        u, v = (0.0, 0.0) if k % 4 else (f32(rng.choice([0.0, 0.33, 0.66])), f32(rng.choice([0.0, 0.33, 0.66])))
        du, dv = (1.0, 1.0) if k % 4 else (f32(0.33), f32(0.33))
        e.events = []
        e.call(0x621400, [slot, pos, e.fbits(w), e.fbits(h), light, e.fbits(rot), e.fbits(u), e.fbits(v),
                          e.fbits(du), e.fbits(dv)])
        gl = [ev for ev in e.events if ev[0] == 'gl' and ev[1] in ('glColor3fv', 'glColor4f', 'glTexCoord2f',
                                                                  'glVertex3fv', 'glBegin')]
        head = 'case %d %d %s %s %s %s %s %s %s %s %s %s' % (
            slot, components, ' '.join(bits(x) for x in view), ' '.join(bits(x) for x in p), bits(w), bits(h),
            ' '.join(bits(x) for x in lt), bits(rot), bits(u), bits(v), bits(du), bits(dv))
        lines.append(head)
        lines.append('gl %d' % len(gl))
        for _, name, args in gl:
            values = []
            for a in args:
                if isinstance(a, list):
                    values += [bits(x) for x in a]
                elif isinstance(a, float):
                    values.append(bits(a))
                else:
                    values.append(str(a))
            lines.append('%s %s' % (name, ' '.join(values)))
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('casos', cases, '->', out)


if __name__ == '__main__':
    main()
