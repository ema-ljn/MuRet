#!/usr/bin/env python3
"""El grafo de llamadas del main.exe, leído de la caché que arma regen_cache.py.

La caché vive en MURET_RESEARCH_CACHE (por defecto .research-cache/ en la raíz del repo, fuera de
Git) y tiene:
- all.txt: el listado de `objdump -d -M intel --no-show-raw-insn` de la sección .text;
- cg.pkl: la dirección y el texto de cada instrucción del listado, y los comienzos de función;
- source.txt: el SHA-256 del exe del que salió.

Los comienzos de función son los destinos de cada `call 0x...` directo más cada `push ebp; mov
ebp,esp` que sigue a un `nop`, `ret`, `int3` o `jmp` (las funciones que solo se llaman por tabla). Es
una aproximación: una función sin marco y sin `call` directo queda pegada a la anterior.

Como módulo lo usan funcof.py, reach.py y los scripts de game/maps/. Como script, recorre las
llamadas directas desde una función hasta una profundidad y avisa cada llamada a uno de los destinos:

    cg.py raíz profundidad destino [destino ...]      (direcciones en hexadecimal)
"""
import bisect
import os
import pickle
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..', '..', '..'))
DEFAULT_CACHE = os.path.join(REPO, '.research-cache')
LISTING, GRAPH, SOURCE = 'all.txt', 'cg.pkl', 'source.txt'
LINE = re.compile(r'^\s+([0-9a-f]+):\s+(.*)$')
DIRECT_CALL = re.compile(r'call\s+0x([0-9a-f]+)$')


def cache_dir():
    return os.environ.get('MURET_RESEARCH_CACHE') or DEFAULT_CACHE


def cache_file(name):
    return os.path.join(cache_dir(), name)


def parse_listing(path):
    """Las instrucciones del listado de objdump y los comienzos de función."""
    addrs, lines = [], []
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            m = LINE.match(raw)
            if m:
                addrs.append(int(m.group(1), 16))
                lines.append(m.group(2))
    starts = set()
    for text in lines:
        m = DIRECT_CALL.match(text)
        if m:
            starts.add(int(m.group(1), 16))
    for i, text in enumerate(lines):
        if (i > 0 and i + 1 < len(lines) and text.startswith('push   ebp')
                and lines[i + 1].startswith('mov    ebp,esp')
                and lines[i - 1].startswith(('nop', 'ret', 'int3', 'jmp'))):
            starts.add(addrs[i])
    return addrs, lines, sorted(starts)


class CallGraph:
    def __init__(self, addrs, lines, starts):
        self.addrs, self.lines, self.starts = addrs, lines, starts
        self._callers = None

    def funcof(self, va):
        """El comienzo de la función que contiene `va` (None antes de la primera)."""
        j = bisect.bisect_right(self.starts, va) - 1
        return self.starts[j] if j >= 0 else None

    def body(self, start):
        """Las instrucciones (dirección, texto) desde `start` hasta el comienzo siguiente."""
        i = bisect.bisect_left(self.addrs, start)
        j = bisect.bisect_right(self.starts, start)
        end = self.starts[j] if j < len(self.starts) else 1 << 40
        out = []
        while i < len(self.addrs) and self.addrs[i] < end:
            out.append((self.addrs[i], self.lines[i]))
            i += 1
        return out

    def calls(self, start):
        """Las llamadas de una función: (dirección, destino) si es directa, (dirección, texto) si no."""
        out = []
        for a, text in self.body(start):
            m = DIRECT_CALL.match(text)
            if m:
                out.append((a, int(m.group(1), 16)))
            elif text.startswith('call'):
                out.append((a, text))
        return out

    def callers(self, target):
        """Las direcciones de los `call` directos a `target`."""
        if self._callers is None:
            self._callers = {}
            for a, text in zip(self.addrs, self.lines):
                m = DIRECT_CALL.match(text)
                if m:
                    self._callers.setdefault(int(m.group(1), 16), []).append(a)
        return self._callers.get(target, [])


def load():
    path = cache_file(GRAPH)
    if not os.path.isfile(path):
        sys.exit('no está la caché %s: correr tools/regen_cache.py (con MURET_MAIN_EXE)' % path)
    exe = os.environ.get('MURET_MAIN_EXE')
    source = cache_file(SOURCE)
    if exe and os.path.isfile(exe) and os.path.isfile(source):
        sys.path.insert(0, HERE)
        import image  # noqa: E402
        with open(source, encoding='utf-8') as f:
            cached = f.readline().split()[0]
        if cached != image.sha256(exe):
            print('aviso: la caché salió de otro exe (%s…); correr regen_cache.py' % cached[:8], file=sys.stderr)
    with open(path, 'rb') as f:
        return CallGraph(*pickle.load(f))


def main():
    if len(sys.argv) < 4:
        sys.exit(__doc__)
    g = load()
    root, depth = int(sys.argv[1], 16), int(sys.argv[2])
    targets = {int(x, 16) for x in sys.argv[3:]}
    seen = set()

    def walk(f, d, path):
        if f in seen:
            return
        seen.add(f)
        for a, c in g.calls(f):
            if isinstance(c, int):
                if c in targets:
                    print('HIT', ' > '.join(hex(p) for p in path + [f]), hex(a), '->', hex(c))
                elif d > 0:
                    walk(c, d - 1, path + [f])
    walk(root, depth, [])
    print('visitadas', len(seen))


if __name__ == '__main__':
    main()
