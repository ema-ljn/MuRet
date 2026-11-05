#!/usr/bin/env python3
"""Cada lectura del mapa actual (0x6c9dd0) en el listado, agrupada por función, con los inmediatos con
que se compara: un `cmp DWORD PTR ds:0x6c9dd0,N`, o un `mov r, ds:0x6c9dd0` y los `cmp r,N` de las 12
instrucciones que siguen (hasta que r cambia, un call o un ret).

Es el barrido de "qué hace cada mapa distinto" del barrido D (mapcmp.txt): de ahí salen la tabla por
mapa de la CL7.5 E (el fondo de 0x637700, el far, la niebla del 30...) y los casos del mapa 0 de la L2.
Lee la caché de tools/regen_cache.py.

Uso: mapcmp.py [variable]      (por defecto 6c9dd0, en hexadecimal; sirve para cualquier global)
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402,F401  (agrega tools/ a sys.path)
import cg  # noqa: E402


def main():
    var = '0x%x' % int(sys.argv[1] if len(sys.argv) > 1 else '6c9dd0', 16)
    g = cg.load()
    load = re.compile(r'mov\s+(\w+),(?:DWORD PTR )?ds:%s$' % var)
    direct = re.compile(r'cmp\s+DWORD PTR ds:%s,(0x[0-9a-f]+)' % var)
    current = None
    for i, (a, text) in enumerate(zip(g.addrs, g.lines)):
        if var not in text:
            continue
        f = g.funcof(a)
        if f != current:
            print('== func %x' % f)
            current = f
        imms = []
        m = direct.search(text)
        if m:
            imms.append(int(m.group(1), 16))
        m = load.match(text)
        if m:
            reg = m.group(1)
            for t2 in g.lines[i + 1:i + 12]:
                c = re.match(r'cmp\s+%s,(0x[0-9a-f]+|\d+)$' % reg, t2)
                if c:
                    imms.append(int(c.group(1), 16 if c.group(1).startswith('0x') else 10))
                if re.match(r'(mov|lea|xor|pop|add|sub)\s+%s,' % reg, t2) or t2.startswith(('call', 'ret')):
                    break
        print('  %x: %-50s %s' % (a, text, ' '.join(str(v) for v in imms)))


if __name__ == '__main__':
    main()
