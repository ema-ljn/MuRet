#!/usr/bin/env python3
"""La función que contiene cada dirección y las funciones que la llaman con un `call` directo.

Lee el grafo de la caché (regen_cache.py). Con --group, las direcciones se juntan por función: cuántas
caen en cada una y sus primeros seis llamadores (así salió el barrido de 0x6c9dd0 por función del
barrido D, desde un grep del listado). Sin direcciones en la línea de comandos, las lee de la entrada,
una por línea.

Uso:
    funcof.py dirección [dirección ...]
    grep -o '^ *[0-9a-f]*:.*0x6c9dd0' all.txt | cut -d: -f1 | funcof.py --group
"""
import argparse
import os
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cg  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description='La función de cada dirección y sus llamadores.')
    ap.add_argument('addresses', nargs='*', help='direcciones en hexadecimal (sin ellas, de la entrada)')
    ap.add_argument('--group', action='store_true', help='juntar las direcciones por función')
    args = ap.parse_args()
    raw = args.addresses or [line.strip() for line in sys.stdin if line.strip()]
    g = cg.load()
    if args.group:
        groups = OrderedDict()
        for text in raw:
            x = int(text, 16)
            groups.setdefault(g.funcof(x), []).append(x)
        for f, xs in groups.items():
            callers = sorted({g.funcof(c) for c in g.callers(f)})
            print('%x n=%d callers=%s' % (f, len(xs), ' '.join('%x' % c for c in callers[:6])))
        return
    for text in raw:
        x = int(text, 16)
        f = g.funcof(x)
        callers = sorted({g.funcof(c) for c in g.callers(f)})
        print('%x in func %x; callers: %s' % (x, f, ' '.join('%x' % c for c in callers)))


if __name__ == '__main__':
    main()
