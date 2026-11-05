#!/usr/bin/env python3
"""Dónde la traza de Lorencia tiene el terreno más brillante que su luz sombreada: la luz dinámica.

Lee el color de cada esquina de celda del terreno opaco del primer frame de la traza (las líneas 1 a
7.868 del pase del mundo, common.trace_frame) y lo compara con el rojo de World1\\TerrainLight.OZJ en
esa celda. Las esquinas más de un 5 % (y 0,01) más brillantes son las que subió la luz dinámica
(0x5fbf60, 0x5f9780): da cuántas son, a qué objeto con luz quedan a menos de 5 celdas (o al héroe en
(143,5; 120,5)) y el factor de cada una. Con la traza del laboratorio: 27 esquinas, 23 alrededor del
FireLight01 #1933 y 4 en la fila 136, que salen junto al StreetLight01 #1744 pero caen fuera de su
cuadrado de ±3: sin identificar (la CL7.L4).

Necesita Pillow, para el JPEG del OZJ (requirements.txt).

Uso: brighter.py      (con MURET_CLIENT_GL_TRACE_DIR y el Data\\)
"""
import collections
import io
import math
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import common  # noqa: E402
import lorencia_models  # noqa: E402

TERRAIN_LINES = 7868         # el terreno opaco del primer frame de lorencia-Frame_016215.txt
HERO_CELL = (143.5, 120.5)
LIGHT_TYPES = (0x5a, 0x96, 0x32, 0x33, 0x34, 0x37, 0x50, 0x82, 0x83, 0x84)
VERTEX = re.compile(r'glVertex3fv\(\[([-\d.]+),([-\d.]+),([-\d.]+)\]\)')
COLOR = re.compile(r'glColor[34]f[v]?\(\[?([-\d.]+),([-\d.]+),([-\d.]+)')


def main():
    with open(common.need('world1', 'TerrainLight.OZJ'), 'rb') as f:
        light = Image.open(io.BytesIO(f.read()[24:])).convert('RGB').load()
    color, seen = None, {}
    for line in common.trace_frame(common.gl_trace())[:TERRAIN_LINES]:
        if line.startswith('glColor'):
            color = tuple(float(g) for g in COLOR.match(line).groups())
        elif line.startswith('glVertex'):
            x, y, _ = (float(g) for g in VERTEX.match(line).groups())
            if x % 100 or y % 100:
                continue
            seen.setdefault((int(x) // 100, int(y) // 100), []).append(color)
    brighter = []
    for (cx, cy), colors in seen.items():
        shaded = light[cx, 255 - cy][0] / 255.0
        red = max(c[0] for c in colors)
        if shaded > 8 / 255 and red > shaded * 1.05 + 0.01:
            brighter.append((cx, cy, round(red / shaded, 2)))
    print(len(seen), 'vértices distintos;', len(brighter), 'más brillantes')
    _, objs = common.load_obj(1)
    lights = [o for o in objs if o['type'] in LIGHT_TYPES]
    near = collections.Counter()
    for cx, cy, _ in brighter:
        o = min(lights, key=lambda o: math.hypot(o['pos'][0] / 100 - cx, o['pos'][1] / 100 - cy))
        d = math.hypot(o['pos'][0] / 100 - cx, o['pos'][1] / 100 - cy)
        if d < 5:
            tag = '%s#%d' % (lorencia_models.NAMES[o['type']], o['i'])
        elif math.hypot(HERO_CELL[0] - cx, HERO_CELL[1] - cy) < 5:
            tag = 'héroe'
        else:
            tag = 'otro(%.1f)' % d
        near[tag] += 1
    print(near.most_common())
    for i in (1933, 1744):
        o = objs[i]
        print(i, hex(o['type']), [round(v / 100, 2) for v in o['pos']], 'escala', round(o['scale'], 2))
    print('; '.join('%d %d %s' % b for b in sorted(brighter)))


if __name__ == '__main__':
    main()
