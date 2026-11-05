#!/usr/bin/env python3
"""El .obj de Lorencia (World1\\EncTerrain1.obj): la cabecera, los registros y cada tipo con su nombre
(lorencia_models), cuántos hay y si su .bmd está en Object1\\.

Con una ruta, escribe además los registros en JSON (i, type, pos, ang, scale), fuera del repo.

Uso: obj0.py [registros.json]      (con el Data\\)
"""
import collections
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..'))
sys.path.insert(0, HERE)
import common  # noqa: E402
import lorencia_models  # noqa: E402


def main():
    header, objs = common.load_obj(1)
    print('cabecera', header, 'registros', len(objs))
    counts = collections.Counter(o['type'] for o in objs)
    for t, n in sorted(counts.items()):
        name = lorencia_models.NAMES.get(t, '?')
        have = common.find('Object1', name + '.bmd') is not None
        print('%#04x %-18s x%-4d %s' % (t, name, n, '' if have else 'FALTA .bmd'))
    if len(sys.argv) > 1:
        with open(sys.argv[1], 'w', encoding='utf-8') as f:
            json.dump(objs, f)


if __name__ == '__main__':
    main()
