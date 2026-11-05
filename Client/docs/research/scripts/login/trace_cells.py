#!/usr/bin/env python3
"""Las celdas opacas del pase de terreno de una traza de GLIntercept (el primer frame, hasta el
primer wglSwapBuffers): cada GL_TRIANGLE_FAN de 4 vértices sobre la grilla de 100 con GL_BLEND
apagado y GL_CULL_FACE prendido, con las esquinas (x,y),(x+1,y),(x+1,y+1),(x,y+1)."""
import json
import re
import sys


def read(path):
    blend = False
    cull = False
    cells = []
    verts = None
    vre = re.compile(r'glVertex3fv\(\[([-\d.]+),([-\d.]+),([-\d.]+)\]\)')
    with open(path) as f:
        for line in f:
            if line.startswith('wglSwapBuffers'):
                break
            if line.startswith('glEnable(GL_BLEND'): blend = True
            elif line.startswith('glDisable(GL_BLEND'): blend = False
            elif line.startswith('glEnable(GL_CULL_FACE'): cull = True
            elif line.startswith('glDisable(GL_CULL_FACE'): cull = False
            elif line.startswith('glBegin(GL_TRIANGLE_FAN'): verts = []
            elif line.startswith('glEnd') and verts is not None:
                v = verts; verts = None
                if len(v) != 4 or blend or not cull: continue
                if any(x % 100 or y % 100 for x, y, z in v): continue
                x0, y0 = v[0][0] // 100, v[0][1] // 100
                want = [(x0, y0), (x0 + 1, y0), (x0 + 1, y0 + 1), (x0, y0 + 1)]
                if [(x // 100, y // 100) for x, y, z in v] != want: continue
                cells.append((int(x0), int(y0)))
            elif verts is not None:
                m = vre.match(line)
                if m:
                    x, y, z = (float(g) for g in m.groups())
                    verts.append((x, y, z))
    return cells


if __name__ == '__main__':
    cells = read(sys.argv[1])
    print(len(cells), 'celdas opacas')
    blocks = []
    for c in cells:
        b = (c[1] // 4 * 4, c[0] // 4 * 4)
        if not blocks or blocks[-1] != b: blocks.append(b)
    print(len(blocks), 'bloques (y, x):', blocks)
    json.dump(cells, open(sys.argv[2], 'w')) if len(sys.argv) > 2 else None
