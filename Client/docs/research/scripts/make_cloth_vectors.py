#!/usr/bin/env python3
r"""Pasa una salida de emulate_cloth.py (el código real del main.exe) a los vectores de texto que leen
los tests de la tela de MuRet (Client/tests/render/test_cloth.cpp y scenes/test_character_cloth.cpp).

Cada float va como los 8 dígitos hexadecimales de sus bits. La mezcla de cada cara sale de simular el
estado de GL con las llamadas que hizo el binario (los wrappers de 0x620xxx con sus cachés), no de las
banderas de la tela.

    python make_cloth_vectors.py salida.json > caso.txt
"""
import json
import struct
import sys

BLEND, CULL, ALPHA, DEPTH = 0xbe2, 0xb44, 0xbc0, None


def h(x):
    return '%08x' % struct.unpack('<I', struct.pack('<f', float(x)))[0]


def num(v):
    return int(v, 0) if isinstance(v, str) else int(v)


class GlState:
    def __init__(self, g):
        self.blend = g.get('gl_blend_7d366dc', 0) != 0
        self.func = None
        self.cull = bool(g.get('gl_cull_7d366cc', 0))
        self.mask = bool(g.get('gl_depth_mask_7d36898', 0))
        self.alpha = bool(g.get('gl_alpha_test_7d366cd', 0))
        self.bound = g.get('bound_texture_6d5d68', 0)
        cache = g.get('gl_blend_7d366dc', 0)
        self.func = {2: (0x302, 0x303), 3: (1, 1)}.get(cache)

    def call(self, name, args):
        if name in ('glEnable', 'glDisable'):
            cap, on = num(args[0]), name == 'glEnable'
            if cap == BLEND:
                self.blend = on
            elif cap == CULL:
                self.cull = on
            elif cap == ALPHA:
                self.alpha = on
        elif name == 'glBlendFunc':
            self.func = (num(args[0]), num(args[1]))
        elif name == 'glDepthMask':
            self.mask = bool(num(args[0]))
        elif name == 'glBindTexture':
            self.bound = num(args[1])

    def blend_name(self):
        if not self.blend and self.cull and self.mask and not self.alpha:
            return 'opaque'
        if self.blend and self.func == (0x302, 0x303) and not self.cull and self.mask and self.alpha:
            return 'alpha'
        if self.blend and self.func == (1, 1) and not self.cull and not self.mask and not self.alpha:
            return 'additive'
        raise SystemExit('estado de GL sin nombre: %r' % vars(self))


def main(path):
    data = json.load(open(path))
    spec = data['input']
    out = ['cloth-vectors 1']
    r = spec.get('rand', {'msvc_state': 0})
    if not isinstance(r, dict):
        raise SystemExit('solo rand con msvc_state')
    out.append('rand %d' % (r['msvc_state'] & 0xffffffff))
    out.append('direct %d' % len(spec.get('direct', [])))
    for d in spec.get('direct', []):
        i = d['init']
        wb = d.get('wind_bytes', [1, 1])
        out.append('cloth %d %s %s %s %d %d %s %s %d %d %d %d %d' % (
            i['bone'], h(i['x']), h(i['y']), h(i['z']), i['columns'], i['rows'], h(i['width']), h(i['height']),
            num(i['front']), num(i['back']), num(i['flags']) & 0xffffffff, wb[0], wb[1]))
        out.append('spheres %d' % len(d.get('spheres', [])))
        for x, y, z, rad, bone in d.get('spheres', []):
            out.append('%s %s %s %s %d' % (h(x), h(y), h(z), h(rad), bone))
    out.append('frames %d' % len(spec['frames']))
    gl = None
    for fi, (fin, fout) in enumerate(zip(spec['frames'], data['frames'])):
        ch = fin.get('character', {})
        g = fin.get('globals', {})
        # El emulador pone los cachés de 0x620xxx de la entrada en cada frame; en el juego reflejan el
        # estado de GL, así que el estado arranca de ellos.
        if any(k.startswith('gl_') or k == 'bound_texture_6d5d68' for k in g) or gl is None:
            gl = GlState(g)
        out.append('frame %d' % fi)
        out.append('character %d %d %d %d %d %d' % (ch.get('class_1d4', 0), ch.get('armor_238', 0) & 0xffff,
                   ch.get('wings_2f8', -1) & 0xffff, ch.get('monster_376', -1) & 0xffff, ch.get('flags_78', 0),
                   ch.get('type_02', 0x20a)))
        model = fin.get('model', {})
        a = ch.get('angle_1c', [0, 0, 0])
        o = model.get('body_origin_6c', ch.get('position_10', [0, 0, 0]))
        out.append('object %s %s %s %s %s %s %s' % (h(a[0]), h(a[1]), h(a[2]),
                   h(model.get('body_scale_68', ch.get('scale_0c', 1.0))), h(o[0]), h(o[1]), h(o[2])))
        out.append('bones %d' % len(fin['bones']))
        for m in fin['bones']:
            out.append(' '.join(h(v) for v in m))
        color = fin.get('color', [1, 1, 1])
        out.append('globals %d %s %s %d %d %d %s %d %s %s' % (
            g.get('map_6c9dd0', 0), h(g.get('world_time_5767b88', 0.0)),
            h(g['wind_base_7245f0']) if 'wind_base_7245f0' in g else '-',
            fin.get('frame_counter_7d3a0f0', 0), fin.get('frame_counter_lock_depth', 0),
            fin.get('wind_walk_calls', 0), ('%s %s %s' % tuple(h(c) for c in color)) if color is not None else 'none',
            fin.get('bvar23', 1), h(fin['dt']) if 'dt' in fin else '-',
            h(g['wind_z_7245fc']) if 'wind_z_7245fc' in g else '-'))
        out.append('windbase %s' % h(fout['wind_base_after_walk']))
        events = []
        last_state = {}
        draws = {d['cloth']: d for d in fout.get('draw', [])}
        for e in fout['events']:
            if 'gl' in e:
                gl.call(e['gl'], e.get('args', []))
                if e['gl'] == 'glBegin':
                    events.append(('face', gl.blend_name(), gl.bound))
            elif 'step' in e:
                s = e['step']
                last_state[s['cloth']] = s['state']
                lines = ['step %d %d %d %s %d' % (s['cloth'], s['iteration'], s['ok'], h(s['wind']['cloth_44']),
                                                  len(s['state']))]
                # La velocidad y la fuerza, en el último paso de cada tela (o en uno que rompe); en los
                # demás, la posición y el anclaje.
                full = s['iteration'] == 4 or not s['ok']
                lines[0] += ' %s' % ('full' if full else 'pos')
                for p in s['state']:
                    extra = [h(v) for v in p['vel']] + [h(v) for v in p['force']] if full else []
                    lines.append(' '.join([h(v) for v in p['pos']] + extra + [str(p['pinned'])]))
                events.append(('raw', lines))
            elif 'render' in e:
                events.append(('render', e['render']['cloth'], last_state[e['render']['cloth']]))
            elif 'sprite_0x5491e0' in e:
                s = e['sprite_0x5491e0']
                events.append(('raw', ['flare %s %s %s %s %s' % (h(s['position'][0]), h(s['position'][1]),
                                                                 h(s['position'][2]), h(s['scale']),
                                                                 h(s['light'][0]))]))
            elif 'destroy_0x4fb1a0' in e:
                events.append(('raw', ['destroy']))
            elif 'broken' in e:
                events.append(('raw', ['broken %d' % e['broken']]))
        # Las caras: los vértices de 'draw' en el orden de los glBegin.
        faces = []
        for c in sorted(draws):
            for f in draws[c]['faces']:
                faces.append((c, f))
        lines = []
        fi_face = 0
        current = None
        for ev in events:
            if ev[0] == 'raw':
                lines.extend(ev[1])
            elif ev[0] == 'render':
                current = ev[1]
                index_of = {}
                for k, q in enumerate(ev[2]):
                    index_of.setdefault(tuple(h(v) for v in q['pos']), k)
            elif ev[0] == 'face':
                c, f = faces[fi_face]
                fi_face += 1
                assert c == current, (c, current)
                col = f['color']
                lines.append('face %d %s %d %s %s %s %d' % (c, ev[1], ev[2], h(col[0]), h(col[1]), h(col[2]),
                                                             len(f['vertices'])))
                # Cada vértice, como el índice de la partícula del último paso y sus UV.
                for v in f['vertices']:
                    lines.append('%d %s %s' % (index_of[tuple(h(x) for x in v['xyz'])], h(v['uv'][0]),
                                               h(v['uv'][1])))
        assert fi_face == len(faces), (fi_face, len(faces))
        out.append('events %d' % sum(1 for l in lines if not l[0].isdigit() and not l[0] in 'abcdef' or
                                     l.split()[0] in ('step', 'face', 'flare', 'destroy', 'broken')))
        out.extend(lines)
        rands = sum(1 for e in fout['events'] if 'rand' in e)
        out.append('end %d %d' % (fout.get('object_18c', -1), rands))
    print('\n'.join(out))


if __name__ == '__main__':
    main(sys.argv[1])
