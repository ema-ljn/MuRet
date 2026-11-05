#!/usr/bin/env python3
"""La escena 4 (la selección de personaje) contra su traza y contra el código real emulado: el mismo
procedimiento que Lorencia (frustum_reference.py selection), más la emulación de frustum_emu.py.
Uso: check_selection.py [traza.txt]  (con MURET_MAIN_EXE y MURET_CLIENT_GL_TRACE_DIR)"""
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frustum_reference as R  # noqa: E402
import frustum_emu as E  # noqa: E402
import trace_cells  # noqa: E402
c = R.CASES['selection']
fr, xs, ys, cells = R.run(c)
emu = E.run('selection')
same_emu = [tuple(x) for x in cells] == [tuple(x) for x in emu['cells']] and list(fr['range']) == emu['range']
drawn = R.drop_hidden(cells, R.load_att(c['world']))
path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.environ.get('MURET_CLIENT_GL_TRACE_DIR', '.'),
                                                          R.TRACES['selection'])
trace = trace_cells.read(path)
print('rango', fr['range'], 'pasan', len(cells), 'sin 0x08', len(drawn), 'traza', len(trace),
      'emu igual' if same_emu else 'EMU DISTINTO')
print('IGUAL a la traza: mismo conjunto y mismo orden' if drawn == trace else 'DISTINTO de la traza')
