#!/usr/bin/env python3
"""Dónde se ve el dragón en la pantalla del login (06 § El dragón de fondo): con la trayectoria de
dragon_emu.py (dragon_traj.json, en la carpeta actual), posa Dragon.bmd a escala 1,6 en el frame de
cada paso y lo proyecta con la cámara de LoginCamera. Da el rango del centro y del tamaño, en
coordenadas de 640 x 480. Uso: dragon_screen.py [cada cuántos pasos] (por defecto 5)."""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from bmd import load_bmd  # noqa: E402
from cam import project  # noqa: E402
from pose import world_verts  # noqa: E402
from world import find  # noqa: E402

every = int(sys.argv[1]) if len(sys.argv) > 1 else 5
model = load_bmd(find('object56', 'Dragon.bmd'))
trajectory = json.load(open('dragon_traj.json'))['traj']
centers_x, centers_y, widths, heights, distances = [], [], [], [], []
for state in trajectory[::every]:
    meshes = world_verts(model, state['pos'], state['ang'], 1.6, 0, int(state['frame']) % 41)
    points = []
    for mesh in meshes:
        for v in mesh:
            s, eye = project(v)
            if s is not None:
                points.append(s)
    if not points:
        continue
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    centers_x.append((min(xs) + max(xs)) / 2)
    centers_y.append((min(ys) + max(ys)) / 2)
    widths.append(max(xs) - min(xs))
    heights.append(max(ys) - min(ys))
    _, eye = project(state['pos'])
    distances.append((eye[0] ** 2 + eye[1] ** 2 + eye[2] ** 2) ** 0.5)
print('centro: x de %.0f a %.0f, y de %.0f a %.0f' % (min(centers_x), max(centers_x), min(centers_y), max(centers_y)))
print('tamaño: de %.0f a %.0f de ancho, de %.0f a %.0f de alto' % (min(widths), max(widths), min(heights), max(heights)))
print('distancia a la cámara: de %.0f a %.0f' % (min(distances), max(distances)))
