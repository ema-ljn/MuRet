#!/usr/bin/env python3
"""Los 109 tipos del .obj de Lorencia con el código real: la creación (0x606660) y un paso (0x604ab0),
en el mapa 0 y la escena 5, con un modelo de mentira (4 acciones de 10 keys) en vez del .bmd.

Para cada tipo da lo que queda al crearse (+0x58, +0x64, +0x68, +0x70, la velocidad, +0xdc, la caja, el
blanco y el alpha), si entró en la tabla de las poses (0x7d34818, la de 0x606630), lo que cambió en el
paso, y lo que el paso le pidió al mundo: las partículas vivas (0x743a198, 3.000 de 0x78 bytes) y los
floats de la luz dinámica del terreno que tocó (0x7b4ccc4), con los rand() que gastó. Son los pedidos
que la CL7.L2 deja en su lista para la L4 (la luz) y la L5 (las partículas). Usa el arnés del login
(scripts/login/emu.py).

Uso: lor_objs.py      (con MURET_MAIN_EXE y el Data\\; tarda unos segundos por tipo)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402

common.use_login_harness()
from emu import Emu  # noqa: E402

CREATE, STEP = 0x606660, 0x604ab0
MAP, SCENE, HERO = 0x6c9dd0, 0x6d5dc8, 0x743a170
POSES = 0x7d34818
PARTICLES, PARTICLE_SIZE, PARTICLE_COUNT = 0x743a198, 0x78, 3000
LIGHT, LIGHT_SIZE = 0x7b4ccc4, 0xc0000


def main():
    header, objs = common.load_obj(1)
    print('cabecera', header, 'registros', len(objs))
    types = sorted(set(o['type'] for o in objs))
    print('tipos', len(types), ' '.join(hex(t) for t in types))
    for t in types:
        e = Emu()
        e.w32(MAP, 0)
        e.w32(SCENE, 5)
        e.set_model(t, [(10, 1)] * 4)
        e.w32(HERO, e.alloc(0x438))          # un héroe vacío, para las lecturas de su posición
        o = [x for x in objs if x['type'] == t][0]
        pos = e.alloc(12)
        e.wfv(pos, list(o['pos']))
        ang = e.alloc(12)
        e.wfv(ang, list(o['ang']))
        try:
            obj = e.call(CREATE, [t, pos, ang, e.fbits(o['scale'])])
        except Exception as ex:  # noqa: BLE001
            print(hex(t), 'error al crear', ex)
            continue
        if obj == 0:
            print(hex(t), 'no se creó')
            continue

        def state():
            return dict(m58=e.ri32(obj + 0x58), f64=e.ri32(obj + 0x64), f68=round(e.rf(obj + 0x68), 3),
                        f70=round(e.rf(obj + 0x70), 4), speed=round(e.rf(obj + 0xd8), 4), dc=round(e.rf(obj + 0xdc), 1),
                        bb=[round(v, 1) for v in e.rfv(obj + 0x124, 6)], tgt=round(e.rf(obj + 0x170), 2),
                        alpha=round(e.rf(obj + 0x174), 2))
        s0 = state()
        poses = sum(1 for i in range(200) if e.r8(POSES + 0xc * i))
        light0 = bytes(e.uc.mem_read(LIGHT, LIGHT_SIZE))
        e.w8(obj + 0x16c, 1)
        err = None
        try:
            e.call(STEP, [obj])
        except Exception as ex:  # noqa: BLE001
            err = str(ex)
        s1 = state()
        parts = sum(1 for i in range(PARTICLE_COUNT) if e.r8(PARTICLES + PARTICLE_SIZE * i))
        light1 = bytes(e.uc.mem_read(LIGHT, LIGHT_SIZE))
        lit = sum(1 for i in range(0, LIGHT_SIZE, 4) if light0[i:i + 4] != light1[i:i + 4])
        diff = {k: v for k, v in s1.items() if s0[k] != v}
        print('%-5s creado %s | poses=%d | paso: %s partículas=%d luz=%d rand=%d %s' % (
            hex(t), s0, poses, diff, parts, lit, len(e.rand_log), ('ERROR ' + err) if err else ''))


if __name__ == '__main__':
    main()
