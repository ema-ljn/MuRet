#!/usr/bin/env python3
"""Los vectores de los objetos del mapa 55 (CL7.2, Client/tests/vectors/world55/objects.txt): cada uno
de los 276 registros de EncTerrain56.obj, en el orden del archivo, con el código real del main.exe
(draw_emu.py): lo que deja 0x606660 al crearlo en la escena 2, lo que deja cada uno de tres pasos de
0x604ab0 (con +0x16c prendido, como después de un dibujo) y lo que 0x602e90 le pasa a RenderBody, con
el color del modelo (+0x48) y la luz por vértice (+0x44). La luz del terreno es la fija de draw_emu.py
(LIGHT), que el test le da también a MuRet. Todos los floats van por sus bits.

Uso: make_world55_vectors.py <salida>"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from draw_emu import DrawEmu  # noqa: E402
from world import find, load_obj  # noqa: E402


def bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def raw(emu, address):
    return '%08x' % emu.r32(address)


def state(emu, obj):
    fields = [raw(emu, obj + 0xc)]
    fields += [raw(emu, obj + 0x10 + 4 * k) for k in range(3)]
    fields += [raw(emu, obj + 0x1c + 4 * k) for k in range(3)]
    fields += [str(emu.ri32(obj + 0x58)), str(emu.ri32(obj + 0x64)), raw(emu, obj + 0x68), raw(emu, obj + 0x6c),
               raw(emu, obj + 0x70), raw(emu, obj + 0xd8), str(emu.r8(obj + 0xe8))]
    fields += [raw(emu, obj + 0xf4 + 4 * k) for k in range(3)]
    fields += [raw(emu, obj + 0x174), raw(emu, obj + 0x170), raw(emu, obj + 0x114), str(emu.r8(obj + 0x111))]
    return ' '.join(fields)


def main():
    out = sys.argv[1]
    _, objects = load_obj()
    emu = DrawEmu()
    loaded = set()
    with_model = set()
    for o in objects:
        t = o['type']
        if t in loaded:
            continue
        loaded.add(t)
        name = 'Object%02d.bmd' % (t + 1) if t + 1 < 10 else 'Object%d.bmd' % (t + 1)
        path = find('object56', name)
        if path:
            emu.load_model(t, path)
            with_model.add(t)
    created = []
    for o in objects:
        pos = emu.alloc(12)
        emu.wfv(pos, list(o['pos']))
        ang = emu.alloc(12)
        emu.wfv(ang, list(o['ang']))
        obj = emu.call(0x606660, [o['type'], pos, ang, emu.fbits(o['scale'])])
        created.append((o, obj, state(emu, obj)))
    lines = ['world55-objects 1', 'light ' + ' '.join(bits(v) for v in emu.terrain_light),
             'objects %d' % len(created)]
    for index, (o, obj, first) in enumerate(created):
        lines.append('object %d %d %d' % (index, o['type'], 1 if o['type'] in with_model else 0))
        lines.append('created ' + first)
        emu.w8(obj + 0x16c, 1)
        for k in range(3):
            emu.call(0x604ab0, [obj])
            lines.append('step %d %s' % (k + 1, state(emu, obj)))
        emu.log = []
        emu.events = []
        emu.call(0x602e90, [obj, 0, 0, 0])
        calls = [entry for entry in emu.log if entry[0] in ('RenderBody', 'RenderMesh')]
        lines.append('draw %d' % len(calls))
        for name, _mid, args, color, _f88, vertex_light in calls:
            values = []
            for a in args:
                values.append(bits(a) if isinstance(a, float) else str(a))
            lines.append('call %s %s %s %s' % (name, ' '.join(values), ' '.join(bits(c) for c in color or [0, 0, 0]),
                                               vertex_light if vertex_light is not None else 0))
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('objetos', len(created), '->', out)


if __name__ == '__main__':
    main()
