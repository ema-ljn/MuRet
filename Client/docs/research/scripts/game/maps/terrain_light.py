#!/usr/bin/env python3
"""La luz sombreada del terreno (0x5f8b10, 0x5fd000 y 0x5f8d90) y la luz en un punto (0x5f99e0), con el
perfil PC53 del ASM: los esperados de GoldenTerrainLighting, TerrainLightingTest y FaceNormalTest (06 § La
luz del terreno en un punto).

Los float de Python son binary64 y redondean cada operación al más cercano, como la x87 con PC53 en este
dominio (sin desbordes ni subnormales); cada FSTP float pasa por struct. No comparte código con
World::FaceNormal ni con World::ShadeTerrainLight: es un cálculo aparte, no una observación del original.

Con MURET_CLIENT_MEMORY_DIR compara además, celda por celda, los volcados de 0x078ecc10 (lor_light.bin y
s31_light.bin) con la luz del OZJ sin sombrear y con la sombreada. Son la luz sin sombrear, la que
0x5f8d90 lee; la sombreada (0x7c0ccc4) no se volcó. Con el Data\\ del repo: 0 celdas distintas de la
luz sin sombrear en los dos mapas; de la sombreada, 14.736 en Lorencia y 29.446 en Devias.

Necesita Pillow, para el JPEG del OZJ (requirements.txt).

Uso: terrain_light.py      (con el Data\\; MURET_CLIENT_MEMORY_DIR, opcional)
"""
import io
import math
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402

SIDE = 256
LIGHT_SCALE = struct.unpack('<f', struct.pack('<I', 0x3b808081))[0]   # 0x6b88f8, 1/255
CELLS_PER_UNIT = struct.unpack('<f', struct.pack('<I', 0x3c23d70a))[0]  # 0x6b86c0, 0,01
# mapa: (carpeta, escala de las alturas, celdas de GoldenTerrainLighting)
MAPS = {
    0: (1, 1.5, ((104, 101), (157, 120), (143, 165))),
    2: (3, 1.5, ((147, 21), (142, 22), (136, 25))),
    30: (31, 1.5, ((123, 63), (108, 65), (146, 58))),
}
LOGIN_MAP = (56, 3.0)
LOGIN_POINTS = ((10603.0, 14112.0), (10680.0, 15550.0))   # el personaje y el logo (06 § El login)
DUMPS = ((0, '2026-10-09-lorencia', 'lor_light.bin'), (2, '2026-10-09-devias', 's31_light.bin'))
# Los de TerrainLightingTest: la celda (10, 20) con la luz (0,8f; 0,6f; 0,4f) y las alturas de (11, 20),
# (11, 21) y (10, 21); y los triángulos de FaceNormalTest.
SYNTHETIC_CELLS = ((0, (345.0, 279.0, 256.5)), (30, (381.0, 247.5, 219.0)))
SYNTHETIC_LIGHT = (0.8, 0.6, 0.4)
TRIANGLES = (((1100.0, 2000.0, 34.5), (1100.0, 2100.0, 102.0), (1000.0, 2100.0, 222.0)),
             ((-822.3125, 829.9375, -389.8125), (599.0625, -460.6875, 460.1875), (580.8125, -318.75, 172.25)))


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def face_normal(p1, p2, p3):
    """0x5fd000: uy, vz, uz, nz y el largo en el registro; vy, nx, vx, ux y ny por un float."""
    uy = p2[1] - p1[1]
    vz = p3[2] - p1[2]
    uz = p2[2] - p1[2]
    vy = f32(p3[1] - p1[1])
    nx = f32(vz * uy - vy * uz)
    vx = f32(p3[0] - p1[0])
    ux = f32(p2[0] - p1[0])
    ny = f32(vx * uz - ux * vz)
    nz = ux * vy - vx * uy
    length = math.sqrt((nz * nz + ny * ny) + nx * nx)
    return f32(nx / length), f32(ny / length), f32(nz / length)


def shade(normal, world_map):
    """0x5f8d90: ((b · nz + a · ny) + nx · 0,5) + 0,5, de 0 a 1, con a y b de 0x6c9dd0 == 0x1e."""
    a, b = (-1.0, 1.0) if world_map == 30 else (-0.5, 0.5)
    value = ((b * normal[2] + a * normal[1]) + normal[0] * 0.5) + 0.5
    if not value >= 0.0:
        return 0.0
    return 1.0 if value > 1.0 else value


def load_light(world):
    """0x63b490: el OZJ desde el byte 24, la fila y de la tabla es la 255 − y de la imagen, por 1/255f."""
    with open(common.need('world%d' % world, 'TerrainLight.OZJ'), 'rb') as f:
        image = Image.open(io.BytesIO(f.read()[24:])).convert('RGB').load()
    return [[f32(channel * LIGHT_SCALE) for channel in image[x, SIDE - 1 - y]]
            for y in range(SIDE) for x in range(SIDE)]


def shaded_cell(light, heights, world_map, x, y):
    """0x5f8b10 y 0x5f8d90 en la celda (x, y): la sombra queda en 53 bits hasta cada producto."""
    def height(cx, cy):
        return heights[(cy & 0xff) * SIDE + (cx & 0xff)]
    left, right, bottom, top = x * 100.0, (x + 1) * 100.0, y * 100.0, (y + 1) * 100.0
    normal = face_normal((right, bottom, height(x + 1, y)), (right, top, height(x + 1, y + 1)),
                         (left, top, height(x, y + 1)))
    s = shade(normal, world_map)
    return [f32(s * channel) for channel in light[y * SIDE + x]]


def light_at(light, heights, world_map, x, y):
    """0x5f99e0 sobre la luz sombreada: la celda truncada y la bilineal, primero en y, en 53 bits."""
    cell_x, cell_y = f32(x * CELLS_PER_UNIT), f32(y * CELLS_PER_UNIT)
    left, bottom = int(cell_x), int(cell_y)
    along_x, along_y = cell_x - left, cell_y - bottom

    def at(cx, cy):
        return shaded_cell(light, heights, world_map, cx, cy)
    bl, br, tl, tr = at(left, bottom), at(left + 1, bottom), at(left, bottom + 1), at(left + 1, bottom + 1)
    out = []
    for c in range(3):
        left_column = (tl[c] - bl[c]) * along_y + bl[c]
        right_column = (tr[c] - br[c]) * along_y + br[c]
        out.append(f32((right_column - left_column) * along_x + left_column))
    return out


def compare_with_memory(world_map, capture, name):
    world, scale, _ = MAPS[world_map]
    light, heights = load_light(world), common.load_heights(world, scale)
    with open(common.memory_file(name, capture), 'rb') as f:
        dump = struct.unpack('<%dI' % (SIDE * SIDE * 3), f.read())
    unshaded = shaded = 0
    for y in range(SIDE):
        for x in range(SIDE):
            cell = y * SIDE + x
            native = list(dump[cell * 3:cell * 3 + 3])
            unshaded += native != [bits(v) for v in light[cell]]
            shaded += native != [bits(v) for v in shaded_cell(light, heights, world_map, x, y)]
    print('mapa %d, %s: distinta de la luz sin sombrear en %d celdas; de la sombreada, en %d'
          % (world_map, name, unshaded, shaded))


def synthetic():
    for triangle in TRIANGLES:
        print('triángulo %s: %s' % (triangle, ', '.join('0x%08x' % bits(v) for v in face_normal(*triangle))))
    light = [[f32(channel) for channel in SYNTHETIC_LIGHT]] * (SIDE * SIDE)
    for world_map, (right, top_right, top) in SYNTHETIC_CELLS:
        heights = [0.0] * (SIDE * SIDE)
        heights[20 * SIDE + 11], heights[21 * SIDE + 11], heights[21 * SIDE + 10] = right, top_right, top
        values = shaded_cell(light, heights, world_map, 10, 20)
        print('mapa %d, celda (10, 20) a mano: %s' % (world_map, ', '.join('0x%08x' % bits(v) for v in values)))


def main():
    synthetic()
    for world_map, (world, scale, cells) in MAPS.items():
        light, heights = load_light(world), common.load_heights(world, scale)
        for x, y in cells:
            values = shaded_cell(light, heights, world_map, x, y)
            print('mapa %d, celda (%d, %d): %s' % (world_map, x, y, ', '.join('0x%08x' % bits(v) for v in values)))
    light, heights = load_light(LOGIN_MAP[0]), common.load_heights(*LOGIN_MAP)
    for x, y in LOGIN_POINTS:
        values = light_at(light, heights, 55, x, y)
        print('mapa 55, punto (%g, %g): %s' % (x, y, ', '.join('0x%08x' % bits(v) for v in values)))
    if os.environ.get('MURET_CLIENT_MEMORY_DIR'):
        for world_map, capture, name in DUMPS:
            compare_with_memory(world_map, capture, name)


if __name__ == '__main__':
    main()
