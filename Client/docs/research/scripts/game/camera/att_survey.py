#!/usr/bin/env python3
"""Los atributos de cada mundo que lee la cámara (0x635ae0), del .att del Data\\.

Para cada mundo: el tamaño y la cabecera del .att, cuántas celdas tienen el bit 0x80 (el zoom) y el
0x40 (la altura fija de 0x6c9d5c), con su caja, y cuántas tienen algo en el byte alto. Después, el
rectángulo del far de 3.000 (16.100–19.000 × 18.900–21.700, celdas 161..190 × 189..217), que vale en
todos los mapas: cuántas de sus celdas se pueden pisar (sin 0x04 ni 0x08) y qué atributos tiene.
Con el mundo 1 da además el atributo de la celda de la traza, (143, 120).

Uso: att_survey.py [mundo ...]      (los números de carpeta, world1 = Lorencia; por defecto 1 a 12)
"""
import collections
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402


def bbox(cells):
    if not cells:
        return None
    xs = [i % 256 for i in cells]
    ys = [i // 256 for i in cells]
    return min(xs), max(xs), min(ys), max(ys)


def main():
    worlds = [int(w) for w in sys.argv[1:]] or list(range(1, 13))
    for w in worlds:
        path = common.find('world%d' % w, 'EncTerrain%d.att' % w)
        if path is None:
            print('world%d: sin .att' % w)
            continue
        header, values = common.load_att(w)
        size = 2 if os.path.getsize(path) == 4 + 2 * 65536 else 1
        c80 = [i for i, v in enumerate(values) if v & 0x80]
        c40 = [i for i, v in enumerate(values) if v & 0x40]
        high = sum(1 for v in values if v & 0xff00)
        print('world%d celda=%d bytes cabecera=%s n80=%d caja80=%s n40=%d caja40=%s alto=%d' % (
            w, size, header.hex(), len(c80), bbox(c80), len(c40), bbox(c40), high))
        rect = collections.Counter()
        walk = 0
        for y in range(189, 218):
            for x in range(161, 191):
                v = values[y * 256 + x] & 0xff
                rect[v] += 1
                if not v & 0x0c:
                    walk += 1
        print('   rectángulo del far: pisables %d/%d atributos %s' % (walk, 29 * 30, dict(sorted(rect.items()))))
        if w == 1:
            print('   celda (143, 120) = 0x%x' % values[120 * 256 + 143])


if __name__ == '__main__':
    main()
