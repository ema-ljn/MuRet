#!/usr/bin/env python3
"""Las celdas del terreno del login (06 § El terreno del login): en el rango de bloques de la cámara de
LoginCamera, cuántas tienen el bit 0x08 del .att, cuántas se dibujan, cuántas caen en el viewport,
cuántas son de agua (la capa 5) y cuántas piden el slot 0x122 (una capa 2 de 255 con alfa, Q-C19).
Uso: terrain_count.py [x0 x1 y0 y1], las celdas (por defecto 0 131 120 251)."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cam import project  # noqa: E402
from world import load_att, load_height, load_map  # noqa: E402

x0, x1, y0, y1 = (int(a) for a in sys.argv[1:5]) if len(sys.argv) > 4 else (0, 131, 120, 251)
_, _, layer1, layer2, alpha = load_map()
att = load_att()
heights = load_height()
cells = hidden = drawn = visible = water = box = 0
rows = []
for y in range(y0, y1 + 1):
    for x in range(x0, x1 + 1):
        cells += 1
        i = y * 256 + x
        if struct.unpack_from('<H', att, 4 + 2 * i)[0] & 0x08:
            hidden += 1
            continue
        drawn += 1
        corners = [(x, y), (x + 1, y), (x + 1, y + 1), (x, y + 1)]
        screen = []
        for cx, cy in corners:
            s, _ = project((cx * 100.0, cy * 100.0, heights[(cy & 255) * 256 + (cx & 255)]))
            if s is None:
                break
            screen.append(s)
        if len(screen) < 4:
            continue
        if all(p[0] < 0 for p in screen) or all(p[0] > 640 for p in screen) or all(
                p[1] < 25 for p in screen) or all(p[1] > 455 for p in screen):
            continue
        visible += 1
        rows += [p[1] for p in screen]
        opaque = all(alpha[(cy & 255) * 256 + (cx & 255)] == 255 for cx, cy in corners)
        if (layer2[i] if opaque else layer1[i]) == 5:
            water += 1
        if not opaque and layer2[i] == 255 and any(alpha[(cy & 255) * 256 + (cx & 255)] > 0 for cx, cy in corners):
            box += 1
print('celdas', cells, 'con el bit 0x08', hidden, 'se dibujan', drawn, 'en el viewport', visible)
print('de agua', water, 'con el slot 0x122', box)
if rows:
    print('en pantalla, y de %.0f a %.0f (640 x 480)' % (min(rows), max(rows)))
