#!/usr/bin/env python3
"""Cuántos rand() gasta la cámara (0x635ae0) por frame dibujado, con el código real.

0x635ae0 lee la celda del héroe (+0x41c, +0x420), que pasa por la tabla de protección: al cerrarla
gasta 8 rand() por frame, también en el login (escena 2) y en la selección (escena 4) (decisión 3 del
plan de la CL7). Corre tres frames seguidos en la escena 5 y uno en cada escena, con el arnés de
../camera/emu_camera.py, y da lo que gastó cada uno desde el estado 1.

Uso: camera_rand.py      (con MURET_MAIN_EXE)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'camera'))
import emu_camera as cam  # noqa: E402

CAMERA = 0x635ae0


def main():
    c = cam.CameraEmu()
    c.hero(14350.0, 12050.0, 165.0)
    for i in range(3):
        r = cam.ec.Rand({'msvc_state': 1})
        c.emu.call(CAMERA, rand=r)
        print('escena 5, frame %d: rand() %d' % (i, r.describe()['consumed']))
    for scene, map_ in [(2, 55), (4, 54), (5, 0)]:
        c = cam.CameraEmu()
        c.emu.w32(cam.MAP, map_)
        c.emu.w32(cam.SCENE, scene)
        c.hero(11679.0, 14191.0, 333.0)
        r = cam.ec.Rand({'msvc_state': 1})
        c.emu.call(CAMERA, rand=r)
        print('escena %d, mapa %d: rand() %d' % (scene, map_, r.describe()['consumed']))


if __name__ == '__main__':
    main()
