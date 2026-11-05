#!/usr/bin/env python3
"""Una reimplementación de 0x635ae0 en doubles, con un f32() donde el binario hace FSTP a float (la x87
en 53 bits), y su comparación contra el código real emulado (emu_camera.CameraEmu) en casos al azar.
Es la prueba de que el puerto a C++ puede ser bit a bit con la misma receta que
Render/Models/OriginalRotation (OriginalAngleMatrix, 0x5fc8f0) más un VectorIRotate (0x5fcc30).

Cubre el camino normal: sin la vista de arriba, sin el CCameraMove, sin el desvío de 0x724588 y sin el
mapa 6 con "webzen". Imprime los frames distintos (hasta 10) y el total.

Uso: camera_reference.py [casos] [frames]      (con MURET_MAIN_EXE; por defecto 60 casos de 40 frames)"""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

DEG = struct.unpack('<f', struct.pack('<I', 0x3C8EFA35))[0]  # 0x6ba360


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def bits(v):
    return '%08x' % struct.unpack('<I', struct.pack('<f', v))[0]


def angle_matrix(x, y, z):
    """0x5fc8f0 (como OriginalAngleMatrix de MuRet): por filas, m[i*4+j]."""
    az, ay, ax = z * DEG, y * DEG, x * DEG
    sz, cz = f32(math.sin(az)), math.cos(az)
    sy, cy = f32(math.sin(ay)), f32(math.cos(ay))
    sx, cx = f32(math.sin(ax)), f32(math.cos(ax))
    m = [0.0] * 12
    m[0] = f32(cy * cz)
    m[4] = f32(cy * sz)
    m[8] = f32(-sy)
    sxsy = sx * sy
    m[1] = f32(sxsy * cz - cx * sz)
    m[5] = f32(sxsy * sz + cx * cz)
    m[9] = f32(sx * cy)
    cxsy = cx * sy
    m[2] = f32(cxsy * cz + sx * sz)
    m[6] = f32(cxsy * sz - sx * cz)
    m[10] = f32(cx * cy)
    return m


def vector_irotate(v, m):
    """0x5fcc30: out.x = (m8·z + m4·y) + x·m0; out.y = (m1·x + m9·z) + m5·y; out.z = (m2·x + m10·z) + m6·y."""
    x, y, z = v
    return (f32((m[8] * z + m[4] * y) + x * m[0]),
            f32((m[1] * x + m[9] * z) + m[5] * y),
            f32((m[2] * x + m[10] * z) + m[6] * y))


TARGETS = {0: 1000.0, 1: 1100.0, 2: 1200.0, 3: 1300.0, 4: 1400.0}
FARS = {0: None, 1: 2500.0, 2: 2600.0, 3: 2950.0, 4: 3200.0}


class State:
    def __init__(self):
        self.pos = [0.0, 0.0, 0.0]
        self.ang = [0.0, 0.0, -45.0]
        self.zoom = 0.0
        self.dist = 1000.0
        self.dist_target = 1000.0
        self.level = 0
        self.far = 2000.0
        self.special = 1200.0


def step(s, hero, attr, scene=5, map_=0, shake=0.0, clock=0.0):
    """Un frame de 0x635ae0 en el camino normal (sin la vista de arriba, sin el CCameraMove, sin el
    desvío de 0x724588 y sin el mapa 6 con "webzen")."""
    hx, hy, hz = hero
    s.ang[0] = 0.0
    s.ang[1] = 0.0
    if 16100.0 <= hx <= 19000.0 and 18900.0 <= hy <= 21700.0:
        s.far = 3000.0
    elif map_ == 0x1e and scene == 5:
        s.far = 2500.0
    elif 0 <= s.level <= 4:
        if s.level == 0:
            s.far = 20400.0 if scene == 2 else (struct.unpack('<f', struct.pack('<I', 0x4633ff9a))[0] if scene == 4 else 2000.0)
        else:
            s.far = FARS[s.level]
    m = angle_matrix(0.0, 0.0, s.ang[2])
    out = vector_irotate((0.0, f32(-s.dist), 0.0), m)
    s.level = 0
    s.pos[0] = f32(out[0] + hx)
    s.pos[1] = f32(out[1] + hy)
    z0 = 255.0 if map_ == 0x1e else hz
    if attr & 0x40:
        s.special = 1201.0
        z0 = 1201.0
    s.pos[2] = f32((s.dist - 150.0) + z0)
    if scene == 2:
        s.ang[1] = 0.0
        s.ang[2] = -36.0
        s.pos = [12778.0, 12678.0, 579.0]
        pitch = -96.5
    elif scene == 4:
        s.ang[1] = 0.0
        s.ang[2] = -45.0
        s.pos = [13909.0, 14482.0, 543.0]
        pitch = -84.5
    else:
        pitch = -48.5
    s.ang[0] = f32(shake + pitch)
    if attr & 0x80:
        if s.zoom <= 200.0:
            s.zoom = f32(s.zoom + 10.0)
    elif s.zoom > 0.0:
        s.zoom = f32(s.zoom - 10.0)
    if s.zoom != 0.0:
        m2 = angle_matrix(0.0, 0.0, -45.0)
        o2 = vector_irotate((0.0, s.zoom, 0.0), m2)
        s.pos = [f32(s.pos[0] + o2[0]), f32(s.pos[1] + o2[1]), f32(s.pos[2] + o2[2])]
    if map_ == 5:
        k1 = struct.unpack('<f', struct.pack('<I', 0x3a03126f))[0]
        k2 = struct.unpack('<f', struct.pack('<I', 0x3a51b717))[0]
        s.ang[0] = f32(2.0 * math.sin(clock * k1) + s.ang[0])
        s.ang[1] = f32(math.sin(clock * k2) * 2.5 + s.ang[1])
        return
    if map_ == 0x1e:
        s.dist_target = s.dist = 1100.0
        return
    if 0 <= s.level <= 4:
        t = TARGETS[s.level]
        s.dist_target = t
        third = struct.unpack('<f', struct.pack('<I', 0x3eaaaaab))[0]
        s.dist = f32((t - s.dist) * third + s.dist)


def compare(n_cases=60, frames=40, seed=1):
    from emu_camera import CameraEmu, HERO, ZOOM, DIST, SHAKE, TIME, MAP
    rnd = random.Random(seed)
    mism = 0
    total = 0
    for case in range(n_cases):
        c = CameraEmu()
        e = c.emu
        s = State()
        map_ = rnd.choice([0, 0, 0, 2, 3, 5, 0x1e])
        e.w32(MAP, map_)
        d0 = f32(rnd.choice([1000.0, rnd.uniform(0.0, 3500.0), 3200.0, 1100.0, 999.99994]))
        e.wf(DIST, d0)
        s.dist = d0
        z0 = f32(float(rnd.choice([0, 10, 200, 210, 50])))
        e.wf(ZOOM, z0)
        s.zoom = z0
        c.hero(14350.0, 12050.0, 165.0)
        for k in range(frames):
            hx = f32(rnd.uniform(0.0, 25599.0))
            hy = f32(rnd.uniform(0.0, 25599.0))
            if rnd.random() < 0.15:
                hx, hy = f32(rnd.uniform(16000.0, 19100.0)), f32(rnd.uniform(18800.0, 21800.0))
            hz = f32(rnd.uniform(-200.0, 1500.0))
            attr = rnd.choice([0, 0, 0x01, 0x80, 0x81, 0x80, 0x40, 0xc0])
            shake = f32(rnd.choice([0.0, 0.0, rnd.uniform(-3, 3)]))
            clock = f32(rnd.uniform(0, 2e6))
            e.wfv(HERO + 0x10, [hx, hy, hz])
            c.att(143, 120, attr)
            e.wf(SHAKE, shake)
            e.wf(TIME, clock)
            got = c.frame()
            step(s, (hx, hy, hz), attr, map_=map_, shake=shake, clock=clock)
            want = {'pos': s.pos, 'ang': s.ang, 'zoom': s.zoom, 'dist': s.dist, 'far': s.far,
                    'special_h': s.special}
            ok = True
            for key in ('pos', 'ang'):
                if [bits(v) for v in got[key]] != [bits(v) for v in want[key]]:
                    ok = False
            for key in ('zoom', 'dist', 'far', 'special_h'):
                if bits(got[key]) != bits(want[key]):
                    ok = False
            total += 1
            if not ok:
                mism += 1
                if mism <= 10:
                    print('distinto: caso %d frame %d map %d attr %x' % (case, k, map_, attr))
                    print('   emu ', {kk: (got[kk] if not isinstance(got[kk], list) else [bits(v) for v in got[kk]]) for kk in ('pos', 'ang', 'zoom', 'dist', 'far', 'special_h')})
                    print('   ref ', {kk: (want[kk] if not isinstance(want[kk], list) else [bits(v) for v in want[kk]]) for kk in want})
                # sincronizar para seguir
                s.pos, s.ang, s.zoom, s.dist, s.far = list(got['pos']), list(got['ang']), got['zoom'], got['dist'], got['far']
    print('frames %d, distintos %d' % (total, mism))


if __name__ == '__main__':
    compare(int(sys.argv[1]) if len(sys.argv) > 1 else 60, int(sys.argv[2]) if len(sys.argv) > 2 else 40)
