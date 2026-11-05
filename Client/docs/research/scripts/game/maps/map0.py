#!/usr/bin/env python3
"""Dónde el código pregunta "¿es el mapa 0?" (Lorencia): las lecturas de 0x6c9dd0 seguidas de un
`test r,r` (hasta 8 instrucciones después), los `cmp DWORD PTR ds:0x6c9dd0,0x0` y las tablas de saltos
indexadas con el mapa (`jmp DWORD PTR [r*4+T]`, o el `mov r,BYTE PTR [r+T]` de la tabla de bytes).

Da la dirección, la función y qué encontró: la lista de los casos del mapa 0 que la CL7.L2 tiene que
cubrir (la creación, el paso y el dibujo de los objetos) y que la CL7.5 E pasa a tabla. Las tablas se
leen después con tools/jt.py. Lee la caché de tools/regen_cache.py.

Uso: map0.py
"""
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402,F401  (agrega tools/ a sys.path)
import cg  # noqa: E402

MAP = '0x6c9dd0'


def main():
    g = cg.load()
    for i, (a, text) in enumerate(zip(g.addrs, g.lines)):
        if MAP not in text:
            continue
        if re.search(r'cmp\s+DWORD PTR ds:%s,0x0$' % MAP, text):
            print('%x f=%x cmp0' % (a, g.funcof(a)))
            continue
        m = re.match(r'mov\s+(\w+),(?:DWORD PTR )?ds:%s$' % MAP, text)
        if not m:
            continue
        reg = m.group(1)
        for j in range(i + 1, min(i + 10, len(g.lines))):
            t2 = g.lines[j]
            if re.match(r'test\s+%s,%s$' % (reg, reg), t2):
                print('%x f=%x test (mapa 0) en %x' % (a, g.funcof(a), g.addrs[j]))
                break
            if (re.search(r'jmp\s+DWORD PTR \[%s\*4\+0x[0-9a-f]+\]' % reg, t2)
                    or re.search(r'mov\s+\w+,BYTE PTR \[%s\+0x[0-9a-f]+\]' % reg, t2)):
                print('%x f=%x switch: %s' % (a, g.funcof(a), t2))
                break
            if re.match(r'(mov|lea|xor|pop)\s+%s,' % reg, t2) or t2.startswith(('call', 'ret')):
                break


if __name__ == '__main__':
    main()
