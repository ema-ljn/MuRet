#!/usr/bin/env python3
"""Reimplementación del paso del cuervo (0x416a20) en los estados 0 y 1, escrita desde la lectura del
ensamblador (C-cuervo-y-resto.md), para compararla con emulate_raven.py. Dueño quieto, sin la
bandera 0x4000, fuera de zona segura y en el mapa 0x37 (altura 350)."""
import json
import math
import struct
import sys


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


class Rand:
    def __init__(self, state):
        self.state = state & 0xffffffff

    def __call__(self):
        self.state = (self.state * 0x343fd + 0x269ec3) & 0xffffffff
        return (self.state >> 16) & 0x7fff


def create_angle(x1, y1, x2, y2):  # 0x4e93f0
    nx = f32(x2 - x1)
    ny = f32(y2 - y1)
    if abs(nx) < f32(0.0001):
        return 0.0 if ny < 0 else 180.0
    if abs(ny) < f32(0.0001):
        return 270.0 if nx < 0 else 90.0
    a = math.atan(ny / nx) * f32(57.2957763671875) + 90.0
    if nx < 0:
        a += 180.0
    return a


def turn_angle(a, b, d):  # 0x4e9560 (a, b, d floats; devuelve el valor de st0)
    if a < 0:
        a = f32(a + 360.0)
    if b < 0:
        b = f32(b + 360.0)
    if a < 180.0:
        lo = f32(a - d)
        if not (b < a + d):
            if a + 180.0 > b:
                return a + d
        if not (lo < 0):
            if a + 180.0 <= b:
                return lo
            if b < lo:
                return lo
            return b
        if a + 180.0 > b:
            return b
        if lo + 360.0 <= b:
            return b
        return lo + 360.0
    hi = f32(a + d)
    if b < a - d:
        if a - 180.0 <= b:
            return a - d
    if hi < 360.0:
        if a - 180.0 > b:
            return hi
        if b < hi:
            return b
        return hi
    if a - 180.0 <= b:
        return b
    if hi - 360.0 > b:
        return b
    return hi - 360.0


DEG = f32(0.01745329238474369)  # 0x6ba360, a comprobar


def angle_matrix(ang):  # 0x5fc8f0
    sy = f32(math.sin(ang[2] * DEG)); cy = math.cos(ang[2] * DEG)
    sp = f32(math.sin(ang[1] * DEG)); cp = f32(math.cos(ang[1] * DEG))
    sr = f32(math.sin(ang[0] * DEG)); cr = f32(math.cos(ang[0] * DEG))
    m = [[0.0] * 4 for _ in range(3)]
    m[0][0] = f32(cp * cy); m[1][0] = f32(cp * sy); m[2][0] = -sp
    m[0][1] = f32(sr * sp * cy - cr * sy); m[1][1] = f32(cr * cy + sr * sp * sy); m[2][1] = f32(sr * cp)
    m[0][2] = f32(sr * sy + cr * sp * cy); m[1][2] = f32(cr * sp * sy - sr * cy); m[2][2] = f32(cr * cp)
    return m


def vector_rotate(v, m):  # 0x5fcbd0
    return [f32((m[0][1] * v[1] + m[0][2] * v[2]) + v[0] * m[0][0]),
            f32((m[1][0] * v[0] + m[1][1] * v[1]) + m[1][2] * v[2]),
            f32((m[2][0] * v[0] + m[2][1] * v[1]) + m[2][2] * v[2])]


KEYS = [6, 4, 3, 4]  # n, o n - 1 con lock_positions (la acción 2)


def advance(o):  # 0x416480 → 0x4ec200 con 0,4
    n = KEYS[o['action']]
    o['frame'] = f32(o['frame'] + f32(0.4))
    if o['frame'] >= n:
        k = int(o['frame'])
        o['frame'] = f32((o['frame'] - k) + (k % n))
        return False
    return True


def set_action(o, a):  # 0x4ea030
    if o['action'] != a:
        o['action'] = a
        o['frame'] = 0.0


def step(o, owner, rand, v_garbage, sparks, sounds):
    # el dueño en zona segura no; estados 3/4 no
    if not advance(o):
        set_action(o, {0: 1, 1: 0, 2: 3, 3: 2, 4: 1, 5: 3}.get(o['state'], 0))
    ox, oy, oz = owner
    px, py, pz = o['pos']
    dx = f32(ox - px)
    dy = f32(oy - py)
    # estados 0 y 1
    bone = rand() % 66
    sparks.append(bone)
    rand()  # 0x53fe70(0x5c6, …, 5): rand() % 2
    d2 = dy * dy + dx * dx
    d2f = f32(d2)
    if d2 >= 22500.0:
        target = f32(create_angle(px, py, ox, oy))
        r = rand() % 15
        o['ang'][2] = f32(turn_angle(o['ang'][2], target, f32(r + 5.0)))
    m = angle_matrix(o['ang'])
    mv = vector_rotate(o['dir'], m)
    o['pos'][0] = f32(mv[0] + o['pos'][0])
    o['pos'][1] = f32(mv[1] + o['pos'][1])
    znew = mv[2] + o['pos'][2]
    o['pos'][2] = f32(znew)
    div = 28
    h = float(350) + oz
    if znew < h:
        o['ang'][0] = f32(o['ang'][0] - 2.0)
        div = 10
        if o['ang'][0] < -15.0:
            o['ang'][0] = -15.0
    elif znew > h + 100.0:
        o['ang'][0] = f32(o['ang'][0] + 2.0)
        div = 20
        if o['ang'][0] > 15.0:
            o['ang'][0] = 15.0
    if rand() % div == 0:
        if d2f < 22500.0:
            near = f32((rand() % 8 + 32) * f32(-0.1))
            o['ang'][2] = f32(rand() % 60 + o['ang'][2])
            base = near
        else:
            base = (rand() % 64 + 128) * f32(-0.1)
        o['dir'][0] = 0.0
        o['dir'][1] = f32((base + o['dir'][1]) * 0.5)
        o['dir'][2] = f32((rand() % 64 - 32) * f32(0.1))
    if o['dir'][1] < -12.0:
        if o['state'] != 0:
            o['state'] = 0; o['counter'] = 0
    else:
        if o['state'] != 1:
            o['state'] = 1; o['counter'] = 0
    if rand() % 100 == 0 and v_garbage % 60 == 0:
        sounds.append(0x1d1)
    ddx = ox - o['pos'][0]
    ddy = oy - o['pos'][1]
    if oz - 200.0 > o['pos'][2] or ddy * ddy + ddx * ddx > 409600.0:
        o['counter'] += 1
    if o['counter'] > 90:
        o['pos'] = [ox, oy, f32(oz + 250.0)]
        o['counter'] = 0


def main():
    seed = int(sys.argv[1]); steps = int(sys.argv[2]); start = sys.argv[3]
    emu = json.load(open(sys.argv[4]))
    rand = Rand(seed)
    owner = (10603.0, 14112.0, 580.0)
    pos = [50.0, 50.0, 300.0] if start == 'cell0' else [10603.0, 14112.0, 830.0]
    o = {'pos': pos, 'ang': [0.0, 0.0, 0.0], 'dir': [0.0, 0.0, 0.0], 'state': 0, 'action': 0, 'frame': 0.0,
         'counter': 0}
    worst = 0.0
    first_bad = None
    for k in range(steps):
        step(o, owner, rand, 0x12345677, [], [])
        e = emu[k]
        diffs = [abs(a - b) for a, b in zip(o['pos'] + o['ang'] + o['dir'] + [o['frame']],
                                           e['pos'] + e['ang'] + e['dir'] + [e['frame']])]
        same_int = (o['state'], o['action'], o['counter']) == (e['state'], e['action'], e['counter'])
        worst = max(worst, max(diffs))
        if (max(diffs) > 0 or not same_int) and first_bad is None:
            first_bad = (k + 1, o, e)
    print('pasos', steps, 'peor diferencia', worst)
    if first_bad:
        k, o2, e = first_bad
        print('primera diferencia en el paso', k)
        print(' ref', o2)
        print(' emu', {x: e[x] for x in ('pos', 'ang', 'dir', 'state', 'action', 'frame', 'counter')})
    else:
        print('bit a bit igual en todos los pasos')


if __name__ == '__main__':
    main()
