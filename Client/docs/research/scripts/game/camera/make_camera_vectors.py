#!/usr/bin/env python3
"""El borrador de los vectores de la cámara del juego (Client/tests/vectors/game/camera.txt de la CL7.3 A,
tarea 2), con el código real de 0x635ae0 emulado frame por frame (emu_camera.CameraEmu).

Son 8 casos y 480 frames: el héroe al azar (y un 15 % dentro del rectángulo del far), el byte del
atributo de su celda, el temblor y el reloj, en los mapas 0, 2, 3, 5 y 0x1e de la escena 5 y en las
escenas 2 y 4. La tarea 2 pide más (unos 3.000 frames, cinco semillas, "webzen", la D y el zoom de
entrada, los bordes del rectángulo ±0,01, el nivel y la meta de la D): se agregan acá.

Formato (todo float en hexadecimal de sus bits):
    camera 1
    case <semilla> <mapa> <escena> <D inicial> <zoom inicial> <yaw>
    frame <hx> <hy> <hz> <attr> <temblor> <reloj> <px> <py> <pz> <ax> <ay> <az> <zoom> <D> <far> <altura 0x40>
    end
Uso: make_camera_vectors.py <salida>      (con MURET_MAIN_EXE)"""
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu_camera import CameraEmu, HERO, DIST, ZOOM, SHAKE, TIME, MAP, SCENE, CAM_ANG  # noqa: E402
from camera_reference import bits, f32  # noqa: E402

CASES = [
    # semilla, mapa, escena, frames
    (1, 0, 5, 80), (2, 0, 5, 80), (3, 2, 5, 60), (4, 5, 5, 60), (5, 0x1e, 5, 60), (6, 3, 5, 60),
    (7, 55, 2, 40), (8, 0, 4, 40),
]


def main():
    out = sys.argv[1]
    lines = ['camera 1']
    for seed, map_, scene, frames in CASES:
        rnd = random.Random(seed)
        c = CameraEmu()
        e = c.emu
        e.w32(MAP, map_)
        e.w32(SCENE, scene)
        d0 = f32(rnd.choice([1000.0, rnd.uniform(0.0, 3500.0), 3200.0]))
        z0 = f32(float(rnd.choice([0, 10, 200, 210])))
        e.wf(DIST, d0)
        e.wf(ZOOM, z0)
        c.hero(14350.0, 12050.0, 165.0)
        lines.append('case %d %d %d %s %s %s' % (seed, map_, scene, bits(d0), bits(z0), bits(e.rf(CAM_ANG + 8))))
        for _ in range(frames):
            hx = f32(rnd.uniform(0.0, 25599.0))
            hy = f32(rnd.uniform(0.0, 25599.0))
            if rnd.random() < 0.15:
                hx, hy = f32(rnd.uniform(16000.0, 19100.0)), f32(rnd.uniform(18800.0, 21800.0))
            hz = f32(rnd.uniform(-200.0, 1500.0))
            attr = rnd.choice([0, 0x01, 0x80, 0x81, 0x80, 0x40, 0xc0])
            shake = f32(rnd.choice([0.0, 0.0, rnd.uniform(-3, 3)]))
            clock = f32(rnd.uniform(0, 2e6))
            e.wfv(HERO + 0x10, [hx, hy, hz])
            c.att(143, 120, attr)
            e.wf(SHAKE, shake)
            e.wf(TIME, clock)
            got = c.frame()
            lines.append('frame %s %s %s %02x %s %s %s %s %s %s %s %s %s %s %s %s' % (
                bits(hx), bits(hy), bits(hz), attr, bits(shake), bits(clock),
                *[bits(v) for v in got['pos']], *[bits(v) for v in got['ang']],
                bits(got['zoom']), bits(got['dist']), bits(got['far']), bits(got['special_h'])))
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('casos', len(CASES), 'líneas', len(lines), '->', out)


if __name__ == '__main__':
    main()
