#!/usr/bin/env python3
"""Los bloques del pase del mundo de la traza de Lorencia con su slot de textura y su mezcla.

Recorre el pase del mundo del primer frame (common.trace_frame: la línea 1 es su glClearColor) hasta
el 2D (el primer glOrtho), junta los glBegin seguidos con la misma textura, el mismo modo y la misma
mezcla, y da de cada grupo las líneas, cuántos son, el modo, la textura de GL, el slot y el archivo
(por slots.read_slots) y la mezcla. Saltea los GL_TRIANGLE_FAN (el terreno).

La textura de GL de la traza y la del volcado difieren en un corrimiento fijo, porque son dos sesiones
distintas: 595 con la traza y el volcado del laboratorio (el slot es el de GL + 595). Es lo que lee el
test de los 120 bloques de objetos de la CL7.L2 (el slot, la mezcla y el orden).

Uso: trace_tex.py [corrimiento]      (con MURET_CLIENT_GL_TRACE_DIR y MURET_CLIENT_MEMORY_DIR)
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import common  # noqa: E402
import slots  # noqa: E402


def main():
    offset = int(sys.argv[1]) if len(sys.argv) > 1 else 595
    by_gl = {}
    for i, name, (_, _, _, gl, _, p) in slots.read_slots():
        if p:
            by_gl[gl] = (i, name)
    tex, blend, seq = None, 'opaque', []
    for n, line in enumerate(common.trace_frame(common.gl_trace()), start=1):
        if line.startswith('glOrtho'):
            break
        if line.startswith('glBindTexture'):
            tex = int(line.split(',')[1].rstrip(')'))
        elif line.startswith('glBlendFunc'):
            blend = line[12:-1]
        elif line.startswith('glDisable(GL_BLEND)'):
            blend = 'opaque'
        elif line.startswith('glBegin'):
            mode = line[8:-1]
            seq.append((n, mode, tex, by_gl.get(tex + offset) if tex else None, blend))
    groups = []
    for n, mode, gl, slot, bl in seq:
        key = (mode, gl, bl)
        if groups and groups[-1][0] == key:
            groups[-1][2] += 1
            groups[-1][3] = n
        else:
            groups.append([key, n, 1, n, slot])
    for (mode, gl, bl), first, count, last, slot in groups:
        if mode == 'GL_TRIANGLE_FAN':
            continue
        print(first, last, 'x%d' % count, mode[3:], gl, '%#x %s' % slot if slot else '?', bl)


if __name__ == '__main__':
    main()
