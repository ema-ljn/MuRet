#!/usr/bin/env python3
"""Genera referencias nominales de Helper07→614/14 (4e9810/536b24..536e8a).

Uso: python3 make_helper_joint_vectors.py [--check]
Requiere mpmath. No importa el port ni ejecuta/emula el original.

Original estático: VMA400000, SHA256
c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a.
Perfil finito de estos fixtures: trig/atan a significando64, productosPC53,
spills binary32/RN. No acredita status x87 ni precisión universal de FSIN/FPATAN.
"""

import argparse
import math
import struct

import mpmath as mp

mp.mp.dps = 180


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def from_bits(value):
    return struct.unpack('<f', struct.pack('<I', value))[0]


def bits(value):
    return hex(struct.unpack('<I', struct.pack('<f', value))[0])


def rounded(value, precision):
    """RN ties-to-even a un significando binario dado, sin exponente limitado."""
    with mp.workprec(precision):
        return +value


def atan_product53(quotient53):
    # 4e9481 / literal6ba108: atan→R64, producto porK24→R53.
    atan64 = rounded(mp.atan(mp.mpf(quotient53)), 64)
    product53 = rounded(atan64 * mp.mpf(from_bits(0x42652ee0)), 53)
    return float(product53)


def angle(x1, y1, x2, y2):
    nx, ny = f32(x2 - x1), f32(y2 - y1)
    if abs(nx) < from_bits(0x38d1b717):
        return 0.0 if ny < 0 else 180.0
    if abs(ny) < from_bits(0x38d1b717):
        return 270.0 if nx < 0 else 90.0
    quotient53 = ny / nx
    result = atan_product53(quotient53) + 90.0
    return result + 180.0 if nx < 0 else result


def turn(a, b, step=25.0):
    if a < 0:
        a = f32(a + 360)
    if b < 0:
        b = f32(b + 360)
    if a < 180:
        low = f32(a - step)
        if b >= a + step and a + 180 > b:
            return a + step
        if low >= 0:
            return low if a + 180 <= b or b < low else b
        return b if a + 180 > b or low + 360 <= b else low + 360
    high = f32(a + step)
    if b < a - step and a - 180 <= b:
        return a - step
    if high < 360:
        return high if a - 180 > b or b >= high else b
    return b if a - 180 <= b or high - 360 > b else high - 360


def matrix(angles):
    radians = [a * from_bits(0x3c8efa35) for a in angles]
    sx, cx = [f32(float(rounded(fn(radians[0]), 64))) for fn in (mp.sin, mp.cos)]
    sy, cy = [f32(float(rounded(fn(radians[1]), 64))) for fn in (mp.sin, mp.cos)]
    sz = f32(float(rounded(mp.sin(radians[2]), 64)))
    cz = float(rounded(mp.cos(radians[2]), 64))
    return [f32(cy * cz), f32(sx * sy * cz - cx * sz), f32(cx * sy * cz + sx * sz), 0,
            f32(cy * sz), f32(sx * sy * sz + cx * cz), f32(cx * sy * sz - sx * cz), 0,
            f32(-sy), f32(sx * cy), f32(cx * cy), 0]


def rotate(vector, rotation):
    x, y, z = vector
    m = rotation
    return [f32((m[1] * y + m[2] * z) + x * m[0]),
            f32((m[4] * x + m[5] * y) + m[6] * z),
            f32((m[8] * x + m[9] * y) + m[10] * z)]


def add(first, second):
    return [f32(x + y) for x, y in zip(first, second)]


def movement_vectors():
    position, angles = [11., 22., 33.], [10., 20., 30.]
    target, velocity, speed, state = [1000., 2000., 3000.], [0., 7., 0.], 50., 1
    common = matrix(angles)
    position = add(position, rotate([0, -speed, 0], common))

    def rand():
        nonlocal state
        state = (214013 * state + 2531011) & 0xffffffff
        return (state >> 16) & 32767

    rows = []
    for _ in range(2):
        angles[2] = f32(turn(angles[2], f32(angle(*position[:2], *target[:2]))))
        x, y = position[0] - target[0], position[1] - target[1]
        xx, yy, z = f32(x * x), f32(y * y), f32(position[2] - target[2])
        horizontal = f32(math.sqrt(xx + yy))
        pitch = f32(360 - angle(position[2], horizontal, target[2], 0))
        angles[0] = f32(turn(angles[0], pitch))
        distance = f32(math.sqrt((z * z + xx) + yy))
        velocity[0], velocity[2] = f32((rand() % 1024 - 512) / 23), f32((rand() % 1024 - 512) / 23)
        rotation = matrix(add(angles, velocity))
        tail = add(position, rotate([-11.5, 0, 0], rotation))
        position = add(position, rotate([0, -speed, 0], rotation))
        rows.append({'angles': list(map(bits, angles)), 'jitter': list(map(bits, velocity)),
                     'tail': list(map(bits, tail)), 'position': list(map(bits, position)),
                     'state': hex(state), 'distance': bits(distance)})
    return rows, list(map(bits, add(position, rotate([-11.5, 0, 0], common))))


def check():
    # Aísla también atan64: derramar primero atan53 cambia un bit del producto.
    assert atan_product53(from_bits(0x3e7bb3e2)).hex() == '0x1.b9e9316d615f3p+3'
    # Con180/pi del host falla45→42340000.
    assert bits(f32(angle(0, 0, 1, -1))) == '0x42340001'
    assert angle(0, 0, 1, -1).hex() == '0x1.6800014b9f5aep+5'
    assert bits(f32(turn(45, f32(angle(0, 0, 1, -1))))) == '0x42340001'
    assert bits(f32(turn(45, f32(360 - angle(0, 1, -1, 0))))) == '0x42340001'
    # 4e9870 y536b3a: sin el spill final, la llegada invierte su decisión estricta.
    z = from_bits(0x403504f3)
    wide = math.sqrt(z * z + 1)
    assert wide.hex() == '0x1.7fffff9dfe36ap+1'
    assert wide < 3 and f32(wide) == 3
    rows, common = movement_vectors()
    assert rows[-1]['position'] == ['0x42bbae84', '0xc1d4ec56', '0x42ad7ad8']
    assert rows[-1]['state'] == '0xe7847115'
    assert common == ['0x42a8f6e0', '0xc2001314', '0x42b558a9']


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='sólo verificar referencias fijas')
    arguments = parser.parse_args()
    check()
    if not arguments.check:
        rows, common = movement_vectors()
        for index, row in enumerate(rows):
            print(index, row)
        print('common tail', common)
        print('non-saturated yaw/pitch', bits(f32(angle(0, 0, 1, -1))),
              bits(f32(360 - angle(0, 1, -1, 0))))
        print('distance spill', '0x403504f3', '0x1.7fffff9dfe36ap+1', '0x40400000')
    print('Helper joint references: OK')
