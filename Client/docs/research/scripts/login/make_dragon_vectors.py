#!/usr/bin/env python3
"""Los vectores del dragón del login (CL7.6 A, Client/tests/vectors/world55/dragon.txt): el efecto 0xb4
que crea el código real de 0x518cf0 en la escena 2 del mapa 0x37, con el objeto dueño en
(5.600; 23.000; 0), y lo que deja cada paso de 0x5208a0, también real (como dragon_emu.py). El modelo
es uno de mentira con la acción de Dragon.bmd (41 keys, sin lock_positions), 3 mallas y 93 huesos.
Todos los floats van por sus bits.

Uso: make_dragon_vectors.py <salida> [pasos, 1500 por defecto]  (con MURET_MAIN_EXE, como emu.py)"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu  # noqa: E402

FX = 0x7491fd8


def state(e):
    def raw(address):
        return '%08x' % e.r32(address)

    fields = [str(e.r8(FX)), str(e.r16(FX + 2)), raw(FX + 0xc)]
    fields += [raw(FX + 0x10 + 4 * k) for k in range(3)]
    fields += [raw(FX + 0x1c + 4 * k) for k in range(3)]
    fields += [str(e.ri32(FX + 0x60))]
    fields += [raw(FX + 0xcc + 4 * k) for k in range(3)]
    fields += [raw(FX + 0xd8), raw(FX + 0xdc), raw(FX + 0xe4), raw(FX + 0xec), str(e.r8(FX + 0x88)), raw(FX + 0x84)]
    fields += [raw(FX + 0x114), raw(FX + 0x118), str(e.r8(FX + 0x111))]
    fields += [raw(FX + 0xf4 + 4 * k) for k in range(3)]
    fields += [raw(FX + 0x174), str(e.ri32(FX + 0x58)), str(e.ri32(FX + 0x64)), raw(FX + 0x68), str(e.r8(FX + 0xe8))]
    fields += [raw(FX + 0x17c + 4 * k) for k in range(3)]
    return ' '.join(fields)


def main():
    out = sys.argv[1]
    steps = int(sys.argv[2]) if len(sys.argv) > 2 else 1500
    e = Emu()
    e.w32(0x6c9dd0, 0x37)
    e.w32(0x6d5dc8, 2)
    e.set_model(0xb4, [(41, 0)], meshes=3, bones=93)
    owner = e.alloc(0x1d4)
    pos = e.alloc(12)
    e.wfv(pos, [5600.0, 23000.0, 0.0])
    ang = e.alloc(12)
    e.wfv(ang, [0, 0, 0])
    light = e.alloc(12)
    e.wfv(light, [1, 1, 1])
    e.call(0x518cf0, [0xb4, pos, ang, light, 0, owner, 0xffffffff, 0, 0, 0, e.fbits(1.6)])
    lines = ['dragon-steps 1', 'created ' + state(e), 'steps %d' % steps]
    for k in range(steps):
        e.call(0x5208a0, [FX, 0])
        lines.append('step %d %s' % (k + 1, state(e)))
    lines.append('rand %d' % len(e.rand_log))
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('pasos', steps, 'rand()', len(e.rand_log), '->', out)


if __name__ == '__main__':
    main()
