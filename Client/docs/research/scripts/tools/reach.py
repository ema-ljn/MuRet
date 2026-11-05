#!/usr/bin/env python3
"""Si una función llega a otra por `call` directos, y por qué camino.

Lee el grafo de la caché (regen_cache.py). Recorre todo lo alcanzable desde las raíces y, para cada
destino alcanzable, imprime un camino (destino <- ... <- raíz). Un destino que no sale no se alcanza
por llamadas directas; puede llegarse igual por una tabla o un puntero (`call DWORD PTR ...`), que
este recorrido no sigue.

Uso: reach.py raíz[,raíz...] destino[,destino...]      (en hexadecimal)
Ejemplo: reach.py 6371a0,6350b0,634680 635ae0          (¿quién del dibujo llega a la cámara?)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cg  # noqa: E402


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    g = cg.load()
    calls = {}
    for a, text in zip(g.addrs, g.lines):
        m = cg.DIRECT_CALL.match(text)
        if m:
            calls.setdefault(g.funcof(a), set()).add(int(m.group(1), 16))
    roots = [int(x, 16) for x in sys.argv[1].split(',')]
    targets = [int(x, 16) for x in sys.argv[2].split(',')]
    seen = {}
    stack = [(r, None) for r in roots]
    while stack:
        f, parent = stack.pop()
        if f in seen:
            continue
        seen[f] = parent
        for c in calls.get(f, ()):
            if c not in seen:
                stack.append((c, f))
    for t in targets:
        f = g.funcof(t)
        if f in seen:
            path = [f]
            while seen[path[-1]] is not None:
                path.append(seen[path[-1]])
            print('%x (func %x) reachable: %s' % (t, f, ' <- '.join('%x' % p for p in path)))
        else:
            print('%x (func %x) no se alcanza por call directos' % (t, f))


if __name__ == '__main__':
    main()
