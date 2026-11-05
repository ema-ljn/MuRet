#!/usr/bin/env python3
"""Las líneas del listado de objdump de la caché (all.txt) entre dos direcciones.

Es el mismo tramo que disas.py, pero con la sintaxis de objdump que leen cg.py y los barridos por
regex (game/maps/mapcmp.py, map0.py).

Uso: listing.py inicio fin       (en hexadecimal; fin no se incluye)
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cg  # noqa: E402

ADDRESS = re.compile(r'^\s*([0-9a-f]+):')


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
    path = cg.cache_file(cg.LISTING)
    if not os.path.isfile(path):
        sys.exit('no está %s: correr tools/regen_cache.py (con MURET_MAIN_EXE)' % path)
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = ADDRESS.match(line)
            if not m:
                continue
            a = int(m.group(1), 16)
            if a >= hi:
                break
            if a >= lo:
                print(line.rstrip())


if __name__ == '__main__':
    main()
