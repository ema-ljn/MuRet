#!/usr/bin/env python3
"""Compara frustum_reference.py con la salida de frustum_emu.py, bit a bit.
Uso: frustum_emu.py <caso> emu_<caso>.json; compare_emu.py <caso>..."""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frustum_reference as R
ok = True
for case in sys.argv[1:]:
    emu = json.load(open('emu_%s.json' % case))
    c = R.CASES[case]
    fr, xs, ys, cells = R.run(c)
    checks = {
        'rango': (list(fr['range']), emu['range']),
        'puntos': ([[R.bits(v) for v in p] for p in fr['points']], emu['points_bits']),
        'poligono x': ([R.bits(v) for v in xs], emu['poly_x_bits']),
        'poligono y': ([R.bits(v) for v in ys], emu['poly_y_bits']),
        'planos': ([[R.bits(v) for v in p] for p in fr['planes']], [[R.bits(v) for v in p] for p in emu['planes']]),
        'planos d': ([R.bits(v) for v in fr['planes_d']], [R.bits(v) for v in emu['planes_d']]),
        'celdas': ([list(x) for x in cells], emu['cells']),
    }
    for k, (a, b) in checks.items():
        same = a == b
        ok &= same
        print(case, k, 'igual' if same else 'DISTINTO', '' if same else (a, b))
print('todo igual' if ok else 'hay diferencias')
