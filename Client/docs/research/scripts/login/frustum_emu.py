#!/usr/bin/env python3
"""Corre el código real de main.exe (c32bbd5f) sobre unicorn: 0x5fbb00 (el frustum y los bloques),
que llama a 0x5fb570 (el polígono 2D), y después 0x5fc190(0) (los bloques de 4x4 con 0x5fc0e0 y
0x5fba90), con 0x5fac90 enganchado: cada llamada que llega a 0x5fac90 es una celda que pasó los dos
tests, en el orden del original (el bit 0x08 del .att lo mira 0x5fac90 adentro; acá se aplica aparte).

Lo que no es del binario:
  - glGetFloatv(GL_MODELVIEW_MATRIX): devuelve la matriz que arman glLoadIdentity, glRotatef(Y),
    glRotatef(X), glRotatef(Z) y glTranslatef de 0x620db0, calculada en float como Mesa
    (s, c = (float) sin/cos en double; productos en float). El driver real puede diferir en el ulp;
  - 0x5bb8d0 (el ancho de pantalla en 640 que deja la UI): devuelve 640 (sin ventanas abiertas).
Uso: frustum_emu.py lorencia|selection|login [json de salida]  (con MURET_MAIN_EXE, como emu.py)
"""
import json, math, os, struct, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from emu import Emu  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ESP  # noqa: E402
from frustum_reference import gl_modelview, CASES  # noqa: E402


class FrustumEmu(Emu):
    matrix = None

    def _on_stub(self, uc, address, s, d):
        dll, name = self.stubs.get(address, ('?', '?'))
        if name == 'glGetFloatv':
            esp = self.uc.reg_read(UC_X86_REG_ESP)
            pname, ptr = self.r32(esp + 4), self.r32(esp + 8)
            assert pname == 0xba6, hex(pname)
            self.uc.mem_write(ptr, struct.pack('<16I', *self.matrix))
            self.events.append(('gl', name, [pname]))
            self.ret(0, 2)
            return
        super()._on_stub(uc, address, s, d)


def run(case):
    e = FrustumEmu()
    c = CASES[case]
    e.matrix = gl_modelview(c['angles'], c['camera'])          # 16 floats (bits), column-major
    e.wf(0x6d5d48, c['fov']); e.wf(0x6d5d44, c['far'])
    e.wfv(0x7d36868, c['angles']); e.wfv(0x7d36884, c['camera'])
    e.w32(0x6d5dc8, c['scene']); e.w32(0x6c9dd0, c['map'])
    hero = e.alloc(0x600); e.w32(0x743a170, hero); e.wfv(hero + 0x10, c['hero'])
    e.wf(0x7d36ba4, 1000.0); e.wf(0x6d5ee0, 1000.0); e.w16(0x7d3a0ec, c.get('level', 0))  # nivel de cámara
    if c.get('webzen'):
        e.uc.mem_write(hero + 0x1d9, b'webzen\0')
    e.at(0x5bb8d0, lambda: e.ret(640))
    pos = e.alloc(12); e.wfv(pos, c['hero'])
    # el llamador: fild(0x5bb8d0()) * 0.0015625f, como float
    e.call(0x5fbb00, [e.fbits(640 * 0.0015625000232830644), pos])
    out = {'case': case}
    out['range'] = [e.ri32(0x7d2d364), e.ri32(0x7d2d368), e.ri32(0x6c9d68), e.ri32(0x6c9d6c)]
    out['points'] = [e.rfv(0x78acb88 + 12 * i, 3) for i in range(5)]
    out['points_bits'] = [[e.r32(0x78acb88 + 12 * i + 4 * k) for k in range(3)] for i in range(5)]
    out['planes'] = [e.rfv(0x7d0ce9c + 12 * i, 3) for i in range(5)]
    out['planes_d'] = e.rfv(0x78ecbd8, 5)
    out['poly_x_bits'] = [e.r32(0x78ecc00 + 4 * i) for i in range(4)]
    out['poly_y_bits'] = [e.r32(0x78ecbf0 + 4 * i) for i in range(4)]
    out['poly_x'] = e.rfv(0x78ecc00, 4); out['poly_y'] = e.rfv(0x78ecbf0, 4)
    cells = []

    def on_cell():
        esp = e.uc.reg_read(UC_X86_REG_ESP)
        cells.append((e.ri32(esp + 12), e.ri32(esp + 16)))
        e.ret(0)
    e.at(0x5fac90, on_cell)
    e.call(0x5fc190, [0])
    out['cells'] = cells
    return out


if __name__ == '__main__':
    case = sys.argv[1] if len(sys.argv) > 1 else 'lorencia'
    out = run(case)
    print('rango (x0, y0, x1, y1):', out['range'])
    print('polígono x:', out['poly_x'], 'y:', out['poly_y'])
    print('celdas que llegan a 0x5fac90:', len(out['cells']))
    if len(sys.argv) > 2:
        json.dump(out, open(sys.argv[2], 'w'))
