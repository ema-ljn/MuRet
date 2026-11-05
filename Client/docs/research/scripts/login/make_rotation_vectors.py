#!/usr/bin/env python3
"""Los vectores del giro del original (CL7.6 A, Client/tests/vectors/world55/rotation.txt): 0x5fc8f0
(AngleMatrix: tres ángulos en grados a una matriz de 3 × 4) y 0x5fcbd0 (VectorRotate: un vector por el
giro), del código real del main.exe, con ángulos y vectores al azar (de −720 a 720 grados, con negativos,
múltiplos de 45 y ceros) y floats por sus bits.

Uso: make_rotation_vectors.py <salida> [casos, 400 por defecto]  (con MURET_MAIN_EXE, como emu.py)"""
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu  # noqa: E402


def bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def main():
    out = sys.argv[1]
    cases = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    rng = random.Random(55)
    e = Emu()
    angles = e.alloc(12)
    matrix = e.alloc(48)
    vector = e.alloc(12)
    rotated = e.alloc(12)
    lines = ['rotation 1', 'cases %d' % cases]
    for k in range(cases):
        choice = k % 4
        if choice == 0:
            a = [rng.uniform(-720, 720) for _ in range(3)]
        elif choice == 1:
            a = [rng.choice([-90, -45, 0, 45, 90, 180, 360]) * 1.0 for _ in range(3)]
        elif choice == 2:
            a = [0.0, rng.uniform(-60, 60), rng.uniform(-360, 360)]
        else:
            a = [rng.uniform(-180, 180), 0.0, rng.uniform(-180, 180)]
        v = [rng.uniform(-5000, 5000) for _ in range(3)]
        e.wfv(angles, a)
        e.wfv(vector, v)
        e.call(0x5fc8f0, [angles, matrix])
        e.call(0x5fcbd0, [vector, matrix, rotated])
        a = e.rfv(angles, 3)
        v = e.rfv(vector, 3)
        m = ['%08x' % e.r32(matrix + 4 * i) for i in range(12)]
        r = ['%08x' % e.r32(rotated + 4 * i) for i in range(3)]
        lines.append('case %s %s %s %s' % (' '.join(bits(x) for x in a), ' '.join(bits(x) for x in v), ' '.join(m),
                                             ' '.join(r)))
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('casos', cases, '->', out)


if __name__ == '__main__':
    main()
