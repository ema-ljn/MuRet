#!/usr/bin/env python3
"""Los casos del mapa 0 de los objetos, con el código real, en la escena 5: para cada tipo con caso de
Lorencia, la creación (0x606660, el switch de 0x6069a3), tres pasos (0x604ab0) y el dibujo (0x602e90,
que llama a 0x602ed0 y 0x6002e0), con las llamadas a RenderBody y RenderMesh y los rand() que gasta.

Usa el arnés del login (scripts/login/draw_emu.py: DrawEmu, con los modelos de verdad de Object1\\) con
el mapa en 0 y la escena en 5, y el primer registro del .obj de cada tipo. Imprime lo que queda al
crearse, lo que cambia en tres pasos y lo que pide cada paso y el dibujo, y escribe todo en
objs0_emu.json en la carpeta actual (fuera del repo). Es la base de make_world0_vectors.py de la CL7.L2,
como run_objs.py lo fue de make_world55_vectors.py.

Uso: run_objs0.py [tipo ...]      (en hexadecimal; por defecto los 30 con caso; con MURET_MAIN_EXE y el Data\\)
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import common  # noqa: E402
import lorencia_models  # noqa: E402

common.use_login_harness()
from draw_emu import DrawEmu  # noqa: E402

CREATE, STEP, DRAW = 0x606660, 0x604ab0, 0x602e90
MAP, SCENE = 0x6c9dd0, 0x6d5dc8
WITH_CASES = [0, 1, 6, 0x0a, 0x18, 0x32, 0x33, 0x34, 0x37, 0x38, 0x3b, 0x50, 0x5a, 0x60, 0x61, 0x62, 0x69, 0x75,
              0x76, 0x77, 0x7a, 0x7d, 0x7e, 0x82, 0x83, 0x84, 0x85, 0x91, 0x92, 0x96]


def main():
    _, objs = common.load_obj(1)
    wanted = [int(t, 16) for t in sys.argv[1:]] or WITH_CASES
    out = {}
    for t in wanted:
        e = DrawEmu()
        e.w32(MAP, 0)
        e.w32(SCENE, 5)
        name = lorencia_models.NAMES[t]
        path = common.find('Object1', name + '.bmd')
        if path:
            e.load_model(t, path)
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

        def state():
            return dict(scale=round(e.rf(obj + 0xc), 4), m58=e.ri32(obj + 0x58), blend=e.ri32(obj + 0x64),
                        blight=round(e.rf(obj + 0x68), 3), U=round(e.rf(obj + 0x6c), 3), V=round(e.rf(obj + 0x70), 3),
                        speed=round(e.rf(obj + 0xd8), 4), dc=round(e.rf(obj + 0xdc), 1), e8=e.r8(obj + 0xe8),
                        light=[round(v, 3) for v in e.rfv(obj + 0xf4, 3)],
                        bbox=[round(v, 0) for v in e.rfv(obj + 0x124, 6)], alpha=round(e.rf(obj + 0x174), 3),
                        target=round(e.rf(obj + 0x170), 3))
        s0 = state()
        e.w8(obj + 0x16c, 1)
        e.log = []
        err = None
        try:
            for _ in range(3):
                e.call(STEP, [obj])
        except Exception as ex:  # noqa: BLE001
            err = 'paso %s' % ex
        steps = list(e.log)
        s1 = state()
        e.log = []
        try:
            e.call(DRAW, [obj, 0, 0, 0])
        except Exception as ex:  # noqa: BLE001
            err = (err or '') + ' dibujo %s' % ex
        out[hex(t)] = dict(name=name, created=s0, after3={k: v for k, v in s1.items() if s0[k] != v}, step=steps,
                           draw=e.log, rand=len(e.rand_log), err=err)
        print('==', hex(t), name, 'error', err)
        print('   creado', s0)
        if out[hex(t)]['after3']:
            print('   tras 3 pasos', out[hex(t)]['after3'])
        for line in steps:
            print('   paso', line)
        for line in e.log:
            print('   dibujo', line)
    with open('objs0_emu.json', 'w', encoding='utf-8') as f:
        json.dump(out, f, indent=1, default=str)


if __name__ == '__main__':
    main()
