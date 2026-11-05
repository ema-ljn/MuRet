#!/usr/bin/env python3
"""Los caminos del cable contra el camino real emulado: cada A3 del cliente (el D3 de main.exe) se
vuelve a pedir a PathFinding2 (0x4eac00, emu_path.PathEmu) desde su celda de inicio hasta su celda
final, sobre el .att del mundo, y se comparan las direcciones paso a paso.

Se saltean los caminos que pisan una pared en el .att (eran de otro mapa) y los que tienen el blanco
al lado del final (la dirección final no es la del último paso). Con el muproxy.log y Lorencia da los
171 caminos del barrido C, con 2 distintos (una celda ocupada que el cable no muestra): es
TheWireWalksMatch de la CL7.4 A. Imprime solo celdas y direcciones.

Uso: wire_vs_path.py [mundo] [cable]      (con MURET_MAIN_EXE, el Data\\ y MURET_CLIENT_WIRE_DIR)
"""
import collections
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'wire'))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import common  # noqa: E402
import wirelib  # noqa: E402
from emu_path import PathEmu  # noqa: E402


def main():
    world = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    log = sys.argv[2] if len(sys.argv) > 2 else None
    _, cells = common.att_cells(world)
    pe = PathEmu(cells)
    result = collections.Counter()
    per_conn = collections.defaultdict(collections.Counter)
    mismatches = []
    for p in wirelib.read_log(log):
        if p.direction != 'C->S' or p.frame != 'C1' or p.data is None or p.data[2] != 0xA3:
            continue
        x, y, final, dirs = wirelib.walk_dirs(p.data)
        if not dirs:
            continue
        ex, ey = x, y
        walkable = cells[y * 256 + x] in (0, 1)
        for d in dirs:
            ex += wirelib.DIRS[d][0]
            ey += wirelib.DIRS[d][1]
            walkable = walkable and cells[ey * 256 + ex] in (0, 1)
        if not walkable:
            result['camino sobre pared en este mapa'] += 1
            per_conn[p.conn]['pared'] += 1
            continue
        if final != dirs[-1]:
            result['blanco distinto del final'] += 1
            continue
        r = pe.find(x, y, ex, ey)
        if r is None:
            result['emulado falla'] += 1
            continue
        path = r[0]
        emulated = [wirelib.DIRS.index((path[i][0] - path[i - 1][0], path[i][1] - path[i - 1][1]))
                    for i in range(1, len(path))]
        if emulated == dirs:
            result['igual'] += 1
            per_conn[p.conn]['igual'] += 1
        else:
            result['distinto'] += 1
            per_conn[p.conn]['distinto'] += 1
            mismatches.append((p.conn, p.time, (x, y), dirs, emulated))
    print('world%d' % world, dict(result))
    for c in sorted(per_conn):
        print('  conexión', c, dict(per_conn[c]))
    for m in mismatches[:12]:
        print('  distinto: conexión %d %s desde %s cable %s emulado %s' % m)


if __name__ == '__main__':
    main()
