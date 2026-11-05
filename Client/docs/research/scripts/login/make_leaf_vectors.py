#!/usr/bin/env python3
"""Los vectores de las brasas del login (CL7.6 B3, Client/tests/vectors/world55/leaves.txt): el código real
de 0x530490 (el paso de las hojas: las 80 del mapa 55 que nacen con 0x42b1e0 y se mueven con 0x5303a0,
que pide la altura del terreno a 0x5f93c0) y de 0x5306a0 (su dibujo), en el mapa 0x37, con un mapa de
alturas de una fórmula entera (0x7accca4, 256 × 256 floats), sus atributos (0x7d0d348, con algunos
0x40), el héroe
(0x743a170) y la y de la cámara (0x7d36888).

De cada paso se guarda el estado del rand() de la CRT y el CRC32 de los 80 lugares de 0x75e34b8 (0x78
bytes cada uno); cada 30 pasos, los lugares enteros (en hexadecimal sin ceros a la izquierda); cada 75,
lo que el dibujo le pide a GL (glColor3f, glTranslatef, glBegin, glTexCoord2f, glVertex3fv, glEnd) y la
textura de 0x620780. También se guardan unas alturas sueltas de 0x5f93c0, como las devuelve en la x87
(los 8 bytes del double), para probar la altura sola.

Uso: make_leaf_vectors.py <salida> [casos, 3 por defecto]  (con MURET_MAIN_EXE, como emu.py)"""
import os
import random
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from emu import Emu  # noqa: E402
from unicorn.x86_const import UC_X86_REG_ESP  # noqa: E402

STEP, DRAW, HEIGHT, BIND = 0x530490, 0x5306a0, 0x5f93c0, 0x620780
MAP, SCENE, HERO, CAMERA_Y, CLOCK = 0x6c9dd0, 0x6d5dc8, 0x743a170, 0x7d36888, 0x5767b88
HEIGHTS, ATTRIBUTES = 0x7accca4, 0x7d0d348
TABLE, SIZE, COUNT = 0x75e34b8, 0x78, 80
STEPS = 150
# Los bordes de 0x5f93c0: la x o la y en 0; las y múltiplos de 100 (la fila sale del producto en 53 bits:
# 20.000 · 0,01f es 199,9999955, en float 200); la fila 255 con la vuelta a la 0 (y 25.600); la columna 255
# con la vuelta (x 25.599) y la 256, que cae en la fila siguiente (x 25.600 y 25.601); el índice justo en
# 65.536 (x 25.601, y 25.550); la fila 256 (y 25.601); una columna de 10.000 (x 1.000.000); y la fila 2^24
# (y 1.677.721.728), que en 32 bits da la vuelta a la columna.
EDGES = [(0.0, 0.0), (0.0, 5000.0), (5000.0, 0.0), (5050.0, 20000.0), (20000.0, 5050.0), (100.0, 25600.0),
         (12345.0, 25600.0), (25500.0, 25500.0), (25599.0, 100.0), (25600.0, 100.0), (25601.0, 100.0),
         (25601.0, 25550.0), (100.0, 25601.0), (1000000.0, 50.0), (5050.0, 1677721728.0)]


def bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def table_bytes(e):
    return bytes(e.uc.mem_read(TABLE, COUNT * SIZE))


def main():
    out = sys.argv[1]
    cases = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    rng = random.Random(31)
    e = Emu()
    calls = []

    # Las texturas y las mezclas van a la misma lista que las llamadas a GL, en su orden.
    def on_bind():
        esp = e.uc.reg_read(UC_X86_REG_ESP)
        e.events.append(('hook', 'bind', '%x' % e.r32(esp + 4)))

    e.at(BIND, on_bind)
    e.at(0x620a90, lambda: e.events.append(('hook', 'blend', 'additive')))
    e.at(0x620a00, lambda: e.events.append(('hook', 'blend', 'alpha')))
    # push [esp+8] (y); push [esp+8] (x); call 0x5f93c0; add esp, 8; fstp qword [result]; ret
    result = e.alloc(8)
    height_stub = e.alloc(32)
    code = bytes.fromhex('ff742408ff742408') + b'\xe8' + struct.pack('<i', HEIGHT - (height_stub + 13))
    code += bytes.fromhex('83c408') + b'\xdd\x1d' + struct.pack('<I', result) + b'\xc3'
    e.uc.mem_write(height_stub, code)
    hero = e.alloc(0x1d4)
    e.w32(HERO, hero)
    e.w32(MAP, 0x37)
    e.w32(SCENE, 2)
    lines = ['leaves 1', 'cases %d' % cases]
    for k in range(cases):
        # El mapa de alturas y los atributos salen de una fórmula entera que el test repite: la altura de la
        # fila i y la columna j es 100 + ((i·a + j·b + (i·j) % 97) % 512) · 0,5, justa en float, y el
        # atributo lleva 0x40 donde (i·c + j·d) % 53 es 0.
        a, b, c, d = rng.randrange(1, 300), rng.randrange(1, 300), rng.randrange(1, 60), rng.randrange(1, 60)
        heights = [100.0 + ((i * a + j * b + (i * j) % 97) % 512) * 0.5 for i in range(256) for j in range(256)]
        e.uc.mem_write(HEIGHTS, struct.pack('<65536f', *heights))
        attributes = [0x40 if (i * c + j * d) % 53 == 0 else 0 for i in range(256) for j in range(256)]
        e.uc.mem_write(ATTRIBUTES, struct.pack('<65536H', *attributes))
        e.uc.mem_write(TABLE, b'\0' * (200 * SIZE))
        position = [0.0, 0.0, 0.0] if k == 0 else [f32(11679.0 + rng.uniform(-50, 50)),
                                                    f32(14191.0 + rng.uniform(-50, 50)), f32(333.0)]
        e.wfv(hero + 0x10, position)
        # El caso 0 es el primer paso del login: el héroe y la cámara en (0, 0, 0) (0x6349a0, 0x634b80).
        camera = f32(0.0) if k == 0 else f32(12678.0) if k == 1 else f32(14400.0)
        e.wf(CAMERA_Y, camera)
        seed = rng.getrandbits(32)
        e.rand_state = seed
        lines.append('case %08x %s %s %d %d %d %d' % (seed, ' '.join(bits(x) for x in position), bits(camera), a, b,
                                                       c, d))
        # Alturas sueltas de 0x5f93c0: el double que deja en la pila de la x87, guardado con fstp qword.
        for _ in range(40):
            x, y = f32(rng.uniform(-300, 25900)), f32(rng.uniform(-300, 25900))
            e.call(height_stub, [e.fbits(x), e.fbits(y)])
            lines.append('height %s %s %016x' % (bits(x), bits(y), struct.unpack('<Q', bytes(e.uc.mem_read(result, 8)))[0]))
        for x, y in EDGES:
            e.call(height_stub, [e.fbits(f32(x)), e.fbits(f32(y))])
            lines.append('height %s %s %016x' % (bits(f32(x)), bits(f32(y)),
                                                 struct.unpack('<Q', bytes(e.uc.mem_read(result, 8)))[0]))
        for step in range(STEPS):
            e.wf(CLOCK, f32(rng.uniform(0, 3_000_000)))
            e.call(STEP, [])
            raw = table_bytes(e)
            lines.append('step %08x %08x' % (e.rand_state, zlib.crc32(raw) & 0xffffffff))
            if (step + 1) % 30 == 0:
                for slot in range(COUNT):
                    lines.append('slot %d %s' % (slot, ' '.join(
                        '%x' % w for w in struct.unpack('<30I', raw[slot * SIZE:(slot + 1) * SIZE]))))
            if (step + 1) % 75 == 0:
                calls.clear()
                start = len(e.events)
                e.call(DRAW, [])
                for source, name, args in e.events[start:]:
                    if source == 'hook':
                        calls.append('%s %s' % (name, args))
                    elif name in ('glColor3f', 'glTranslatef', 'glBegin', 'glTexCoord2f', 'glVertex3fv', 'glEnd'):
                        values = []
                        for a in args:
                            if isinstance(a, list):
                                values += [bits(x) for x in a]
                            elif isinstance(a, float):
                                values.append(bits(a))
                            else:
                                values.append(str(a))
                        calls.append('%s %s' % (name, ' '.join(values)))
                lines.append('draw %d' % len(calls))
                lines.extend(calls)
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('casos', cases, '->', out)


if __name__ == '__main__':
    main()
