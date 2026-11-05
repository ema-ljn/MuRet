#!/usr/bin/env python3
"""Las celdas del terreno que dibuja el original, escritas desde la lectura del asm de main.exe
(c32bbd5f): 0x5fbb00 (los 5 puntos del frustum y el rango de bloques), 0x5fb570 (el polígono 2D),
0x5fc190 / 0x5fc0e0 (los lazos de bloques y celdas) y 0x5fba90 (el test del polígono), más el bit 0x08
del atributo que mira 0x5fac90. Ver contract.md.

Precisión: el x87 con el control word de la CRT de MSVC (0x27f: 53 bits, redondeo al más cercano).
Cada suma, resta y producto del x87 redondea a 53 bits, que es el double de Python; cada `fstp DWORD`
redondea a float (`f32`). fsin/fcos/fptan dan 64 bits en el x87 y acá el double de math: puede haber
una diferencia en el último bit del double, que casi nunca llega al float.

La matriz de la cámara (la que 0x5fbb00 lee con glGetFloatv) la arma el driver de GL; acá se arma
como Mesa (`gl_modelview`). Es lo único que no sale del binario.

Uso:
    frustum_reference.py lorencia [traza.txt]   # compara con las celdas opacas de la traza
    frustum_reference.py selection [traza.txt]  # lo mismo con la de la selección
    frustum_reference.py login                  # el rango de bloques y la cuenta del login
Las trazas, de MURET_CLIENT_GL_TRACE_DIR; el .att, de MURET_CLIENT_DATA (world.py).
    frustum_reference.py --camera fov far ax ay az cx cy cz --hero hx hy hz --scene S --map M
"""
import argparse
import math
import os
import struct
import sys


def f32(x):
    """`fstp DWORD`: el valor redondeado a float."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def bits(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def from_bits(u):
    return struct.unpack('<f', struct.pack('<I', u))[0]


def ftol(x):
    """__ftol (0x69fd3c): trunca hacia 0."""
    return int(x)


def cdiv4(a):
    """`cdq; and edx,3; add eax,edx; sar eax,2`: a / 4 de C (trunca hacia 0)."""
    return (a + (3 if a < 0 else 0)) >> 2


# --- constantes del binario (por sus bits) ------------------------------------------------------
K_FAR_0_9 = from_bits(0x3f666666)      # 0x6b8a1c  0.9f
K_HALF_FOV = from_bits(0x3c0efa33)     # 0x6ba70c  ≈ π/360 (no es el float de π/360, 0x3c0efa35)
K_100 = from_bits(0x42c80000)          # 0x6b86b4  100.0f
K_0_75 = from_bits(0x3f400000)         # 0x6ba36c  0.75f
K_M0_01 = from_bits(0xbc23d70a)        # 0x6ba110  -0.01f
K_0_01 = from_bits(0x3c23d70a)         # 0x6b88d4  0.01f
K_DEG = from_bits(0x3c8efa35)          # 0x6ba360  π/180 en float
K_1_640 = from_bits(0x3acccccd)        # 0x6ba070  0.0015625f (1/640)
MARGIN_BLOCK = from_bits(0xc2200000)   # 0x6c9d60  -40.0f (.data inicializada; nadie la escribe)

# 0x5fb570, nivel de cámara 0 (DAT_07b4ccb8 = 0): por escena (DAT_006d5dc8)
#   escala A = ancho * k, lejos K; cerca = K * 0.19f, objetivo = K * 0.47f,
#   ancho lejos = A * 1190, ancho cerca = A * 540
POLY_LEVEL0 = {
    2: (from_bits(0x3e233333), from_bits(0x486f1000)),  # 0x6ba6d8 0.159375f, 0x6ba6d0 244800
    4: (from_bits(0x3c137469), from_bits(0x4657ff86)),  # 0x6ba6d4 0.0089999f, 0x6ba6cc 13823.88
}
POLY_LEVEL0_OTHER = (K_1_640, from_bits(0x45160000))    # 0x6ba070, 0x6ba6c8 2400
K_NEAR = from_bits(0x3e428f5c)        # 0x6ba364 0.19f
K_TARGET = from_bits(0x3ef0a3d7)      # 0x6ba6c4 0.47f
K_WFAR = from_bits(0x4494c000)        # 0x6ba6c0 1190
K_WNEAR = from_bits(0x44070000)       # 0x6ba000 540
ANGLE_DEFAULT = from_bits(0x42340000)  # 45.0f, inmediato en 0x5fb9e8


# --- la matriz de GL ------------------------------------------------------------------------------
def _matmul_f32(a, b):
    out = [[0.0] * 4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            s = f32(a[i][0] * b[0][j])
            for k in range(1, 4):
                s = f32(s + f32(a[i][k] * b[k][j]))
            out[i][j] = s
    return out


def _rotate(angle, x, y, z):
    """El caso optimizado de _math_matrix_rotate de Mesa para un eje."""
    m = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    s = f32(math.sin(angle * math.pi / 180.0))
    c = f32(math.cos(angle * math.pi / 180.0))
    if x == 0 and y == 0:          # z
        m[0][0] = c; m[1][1] = c
        m[0][1], m[1][0] = (s, -s) if z < 0 else (-s, s)
    elif x == 0 and z == 0:        # y
        m[0][0] = c; m[2][2] = c
        m[0][2], m[2][0] = (-s, s) if y < 0 else (s, -s)
    elif y == 0 and z == 0:        # x
        m[1][1] = c; m[2][2] = c
        m[1][2], m[2][1] = (s, -s) if x < 0 else (-s, s)
    else:
        raise ValueError('solo ejes')
    return m


def gl_modelview_rows(angles, camera):
    """0x620db0: glLoadIdentity; glRotatef(ay,0,1,0); glRotatef(ax,1,0,0) (si DAT_07d36899 == 0);
    glRotatef(az,0,0,1); glTranslatef(-camera). Filas: M[fila][columna]."""
    ax, ay, az = angles
    m = [[1.0 if i == j else 0.0 for j in range(4)] for i in range(4)]
    for ang, axis in ((ay, (0, 1, 0)), (ax, (1, 0, 0)), (az, (0, 0, 1))):
        if f32(ang) != 0.0:        # Mesa no rota con ángulo 0
            m = _matmul_f32(m, _rotate(f32(ang), *axis))
    t = [-f32(v) for v in camera]
    for i in range(4):
        m[i][3] = f32(m[i][3] + f32(f32(f32(m[i][0] * t[0]) + f32(m[i][1] * t[1])) + f32(m[i][2] * t[2])))
    return m


def gl_modelview(angles, camera):
    """Los 16 floats (bits) que devuelve glGetFloatv(GL_MODELVIEW_MATRIX): por columnas."""
    m = gl_modelview_rows(angles, camera)
    return [bits(m[r][c]) for c in range(4) for r in range(4)]


# --- 0x5fbb00 -------------------------------------------------------------------------------------
def frustum(fov, far, param_1, matrix_rows, camera):
    """Los 5 puntos (cámara y 4 esquinas lejanas), el rango de bloques y los 5 planos."""
    fov, far, param_1 = f32(fov), f32(far), f32(param_1)
    d = f32(far * K_FAR_0_9)                                   # [ebp-4]
    w = math.tan(fov * K_HALF_FOV) * d * param_1 + K_100       # en el registro (53 bits)
    h = w * K_0_75
    W, H, D = f32(w), f32(h), d
    local = [(0.0, 0.0, 0.0), (-W, H, -D), (W, H, -D), (W, -H, -D), (-W, -H, -D)]
    # 0x620370: la matriz 3x4 por filas (m[i*4+j] = M[i][j]); 0x5fcc30 multiplica por la transpuesta
    m = [matrix_rows[i][j] for i in range(3) for j in range(4)]
    cam = [f32(v) for v in camera]
    pts = []
    for vx, vy, vz in local:
        ox = f32((m[8] * vz + m[4] * vy) + vx * m[0])
        oy = f32((m[1] * vx + m[9] * vz) + m[5] * vy)
        oz = f32((m[2] * vx + m[10] * vz) + m[6] * vy)
        pts.append((f32(cam[0] + ox), f32(cam[1] + oy), f32(cam[2] + oz)))
    minx = miny = 25600.0
    maxx = maxy = 0.0
    for x, y, _ in pts:
        if minx > x: minx = x                   # test ah,0x41 / jne: estrictamente mayor
        if miny > y: miny = y
        if maxx < x: maxx = x                   # test ah,0x01 / je: estrictamente menor
        if maxy < y: maxy = y
    x0 = (-1 - cdiv4(ftol(minx * K_M0_01))) * 4
    y0 = (-1 - cdiv4(ftol(miny * K_M0_01))) * 4
    x1 = 4 - cdiv4(ftol(maxx * K_M0_01)) * 4
    y1 = 4 - cdiv4(ftol(maxy * K_M0_01)) * 4
    clamp = lambda v: 0xfb if v > 0xfb else (0 if v < 0 else v)  # noqa: E731
    rng = tuple(clamp(v) for v in (x0, y0, x1, y1))
    planes = [plane(pts[0], pts[1], pts[2]), plane(pts[0], pts[2], pts[3]), plane(pts[0], pts[3], pts[4]),
              plane(pts[0], pts[4], pts[1]), plane(pts[3], pts[2], pts[1])]
    origin = [pts[0]] * 4 + [pts[1]]
    dist = [f32(-((n[2] * p[2] + n[1] * p[1]) + n[0] * p[0])) for n, p in zip(planes, origin)]
    return {'points': pts, 'min': (minx, miny), 'max': (maxx, maxy), 'range': rng,
            'planes': planes, 'planes_d': dist, 'W': W, 'H': H, 'D': D}


def plane(a, b, c):
    """0x5fd000: la normal de u × v (u = b - a, v = c - a), normalizada. uy, uz, vz y nz quedan en el
    registro (53 bits); vy, vx, ux, nx y ny pasan por la pila como float."""
    uy, vz, uz, vy = b[1] - a[1], c[2] - a[2], b[2] - a[2], f32(c[1] - a[1])
    nx = f32(vz * uy - vy * uz)
    vx, ux = f32(c[0] - a[0]), f32(b[0] - a[0])
    ny = f32(vx * uz - ux * vz)
    nz = ux * vy - vx * uy
    n = math.sqrt((nz * nz + ny * ny) + nx * nx)
    if n == 0.0:
        return (0.0, 0.0, 0.0)  # 0x5fd000 no escribe nada; queda lo de antes
    return (f32(nx / n), f32(ny / n), f32(nz / n))


# --- 0x5fb570 -------------------------------------------------------------------------------------
def angle_matrix(angles):
    """0x5fc8f0 (AngleMatrix, grados): la 3x4 por filas. cos(z) queda en el registro."""
    a2 = f32(angles[2]) * K_DEG
    sy, cy = f32(math.sin(a2)), math.cos(a2)
    a1 = f32(angles[1]) * K_DEG
    sp, cp = f32(math.sin(a1)), f32(math.cos(a1))
    a0 = f32(angles[0]) * K_DEG
    sr, cr = f32(math.sin(a0)), f32(math.cos(a0))
    m = [0.0] * 12
    m[0] = f32(cp * cy); m[4] = f32(cp * sy); m[8] = f32(-sp)
    t = sr * sp
    m[1] = f32(t * cy - cr * sy); m[5] = f32(t * sy + cr * cy); m[9] = f32(sr * cp)
    t = cr * sp
    m[2] = f32(t * cy + sr * sy); m[6] = f32(t * sy - sr * cy); m[10] = f32(cr * cp)
    return m


def vector_rotate(v, m):
    """0x5fcbd0: m · v (la fila por v)."""
    return (f32((m[1] * v[1] + m[2] * v[2]) + v[0] * m[0]),
            f32((m[4] * v[0] + m[5] * v[1]) + m[6] * v[2]),
            f32((m[8] * v[0] + m[9] * v[1]) + m[10] * v[2]))


POLY_LEVELS = {  # 0x5fb570, niveles 1 a 4: A = ancho * 0.0015625f + 0.1f (0x6b87b8)
    #   (objetivo, cerca, k del ancho lejos, k del ancho cerca, lejos K)
    1: (1269.0, 513.0, from_bits(0x44960000), K_WNEAR, from_bits(0x4528c000)),           # 1200, 540, 2700
    2: (1410.0, 570.0, from_bits(0x44a28000), K_WNEAR, from_bits(0x453b8000)),           # 1300, 540, 3000
    3: (1551.0, 627.0, from_bits(0x44bb8000), from_bits(0x44110000), from_bits(0x454e4000)),  # 1500, 580, 3300
    4: (1598.0, 646.0, from_bits(0x44c80000), from_bits(0x44250000), from_bits(0x45548000)),  # 1600, 660, 3400
}
K_0_1 = from_bits(0x3dcccccd)          # 0x6b87b8 0.1f


def polygon_params(scene, world_map, hero, screen_width=640, level=0, webzen=False):
    """La primera mitad de 0x5fb570: (ancho lejos, ancho cerca, K - objetivo, cerca - objetivo)."""
    w = screen_width
    if world_map == 6 and webzen:                       # 0x5fb5cc
        a = f32(w * K_1_640)
        k, target, near = from_bits(0x4604d000), 3995.0, 425.0          # 8500
        wfar, wnear = f32(a * from_bits(0x453b8000)), f32(a * K_WNEAR)   # 3000, 540
    elif world_map == 0x1e and scene == 5:              # 0x5fb62e
        a = f32(w * K_1_640)
        x, y = f32(hero[0]), f32(hero[1])
        inbox = not (x < from_bits(0x467b9000) or x > from_bits(0x46947000) or
                     y < from_bits(0x4693a800) or y > from_bits(0x46a98800))
        if inbox and (x < from_bits(0x46859800) or x > from_bits(0x468ef800)):   # 17100, 18300
            k, target, near = from_bits(0x459f6000), 2397.0, 969.0       # 5100
            wfar, wnear = f32(a * from_bits(0x450ca000)), f32(a * K_WNEAR)  # 2250, 540
        else:
            k, target, near = from_bits(0x454e4000), 1551.0, 627.0       # 3300
            wfar, wnear = f32(a * from_bits(0x44a28000)), f32(a * from_bits(0x44110000))  # 1300, 580
    elif level == 0:                                    # 0x5fb74d
        ka, k = POLY_LEVEL0.get(scene, POLY_LEVEL0_OTHER)
        a = f32(w * ka)
        near = f32(k * K_NEAR)
        target = f32(k * K_TARGET)
        wfar = f32(a * K_WFAR)
        wnear = f32(a * K_WNEAR)
    elif level in POLY_LEVELS:                          # 0x5fb7e9, 0x5fb834, 0x5fb87f, 0x5fb8c7
        target, near, kf, kn, k = POLY_LEVELS[level]
        a = f32(w * K_1_640 + K_0_1)
        wfar, wnear = f32(a * kf), f32(a * kn)
    else:
        raise ValueError('nivel > 4: 0x5fb90f carga basura (el puntero param_1 como float)')
    return wfar, wnear, f32(k - target), f32(near - target)


def polygon(scene, world_map, hero, camera_angle_z, screen_width=640, level=0, webzen=False):
    """0x5fb570. level = DAT_07b4ccb8 (el nivel de cámara, que copia DAT_07d3a0ec cuando
    ftol(DAT_006d5ee0) >= ftol(DAT_07d36ba4)); webzen = el nombre del héroe contiene "webzen".
    Devuelve (X[4], Y[4]) en celdas, como 0x78ecc00 y 0x78ecbf0."""
    wfar, wnear, yfar, ynear = polygon_params(scene, world_map, hero, screen_width, level, webzen)
    p = [(-wfar, yfar, 0.0), (wfar, yfar, 0.0), (wnear, ynear, 0.0), (-wnear, ynear, 0.0)]
    ang = -f32(camera_angle_z) if (world_map == 6 and webzen) or world_map == 0x37 else ANGLE_DEFAULT
    m = angle_matrix((0.0, 0.0, ang))
    xs, ys = [], []
    for v in p:
        o = vector_rotate(v, m)
        xs.append(f32(f32(o[0] + f32(hero[0])) * K_0_01))
        ys.append(f32(f32(o[1] + f32(hero[1])) * K_0_01))
    return xs, ys


# --- 0x5fba90 y los lazos -------------------------------------------------------------------------
def test2d(xs, ys, x, y, margin, scene):
    """0x5fba90: en las escenas 0, 1 y 3, siempre. Si no, para cada lado (j, i) = (3,0),(0,1),(1,2),
    (2,3): c = (Y[j]-y)(X[i]-x) - (X[j]-x)(Y[i]-y) en 53 bits; pasa si c > margen en los cuatro."""
    if scene in (0, 1, 3):
        return True
    j = 3
    for i in range(4):
        c = (ys[j] - y) * (xs[i] - x) - (xs[j] - x) * (ys[i] - y)
        if not c > margin:
            return False
        j = i
    return True


def margin_of(xs, ys, x, y):
    """La c más chica de los cuatro lados (para ver qué tan cerca está cada decisión)."""
    out = []
    j = 3
    for i in range(4):
        out.append((ys[j] - y) * (xs[i] - x) - (xs[j] - x) * (ys[i] - y))
        j = i
    return min(out)


def terrain_cells(rng, xs, ys, scene, top_view=False, margin=MARGIN_BLOCK):
    """0x5fc190 + 0x5fc0e0: las celdas que llegan a 0x5fac90, en orden. top_view = DAT_07d36899."""
    x0, y0, x1, y1 = rng
    out = []
    by = y0
    while by <= y1:
        bx = x0
        while bx <= x1:
            if test2d(xs, ys, float(bx) + 2.0, float(by) + 2.0, margin, scene) or top_view:
                for dy in range(4):
                    for dx in range(4):
                        if test2d(xs, ys, float(bx + dx) + 0.5, float(by + dy) + 0.5, 0.0, scene) or top_view:
                            out.append((bx + dx, by + dy))
            bx += 4
        by += 4
    return out


def load_att(world_number):
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from world import load_att as _load
    return _load(world_number)


def drop_hidden(cells, att):
    """0x5fac90: con el bit 0x08 del atributo (palabra en y*256 + x) y el pase de dibujo, no dibuja."""
    return [(x, y) for x, y in cells if not struct.unpack_from('<H', att, 4 + 2 * (y * 256 + x))[0] & 0x08]


# --- casos ----------------------------------------------------------------------------------------
CASES = {
    # La traza de Lorencia: la cámara de los glRotatef/glTranslatef; el héroe sale de la regla de
    # 0x635ae0 (cámara = héroe + giro z(0, -1000, 0), z = héroe.z + 1000 - 150): (14350, 12050, 165).
    'lorencia': dict(scene=5, map=0, fov=35.0, far=2000.0, angles=(-48.5, 0.0, -45.0),
                     camera=(15057.106445, 11342.893555, 1015.0), hero=(14350.0, 12050.0, 165.0), world=1),
    # El login (0x6350b0 y 0x635ae0): DAT_07d325c8 (el temblor) en 0.
    'login': dict(scene=2, map=0x37, fov=35.0, far=20400.0, angles=(-96.5, 0.0, -36.0),
                  camera=(12778.0, 12678.0, 579.0), hero=(11679.0, 14191.0, 333.0), world=56),
    # La selección (escena 4, mapa 0x36): la cámara de su traza, con el far por sus bits.
    'selection': dict(scene=4, map=0x36, fov=35.0, far=struct.unpack('<f', struct.pack('<I', 0x4633ff9a))[0],
                      angles=(-84.5, 0.0, -45.0), camera=(13909.0, 14482.0, 543.0),
                      hero=(12975.0, 15423.0, 333.0), world=55),
}
TRACES = {'lorencia': 'lorencia-Frame_016215.txt', 'selection': 'character-selection-Frame_003207.txt'}


def run(c, screen_width=640):
    rows = gl_modelview_rows(c['angles'], c['camera'])
    param_1 = f32(screen_width * K_1_640)           # el llamador: fild(0x5bb8d0()) * 0.0015625f
    fr = frustum(c['fov'], c['far'], param_1, rows, c['camera'])
    xs, ys = polygon(c['scene'], c['map'], c['hero'], c['angles'][2], screen_width,
                     c.get('level', 0), c.get('webzen', False))
    cells = terrain_cells(fr['range'], xs, ys, c['scene'])
    return fr, xs, ys, cells


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('case', nargs='?', default='lorencia')
    ap.add_argument('trace', nargs='?')
    ap.add_argument('--camera', nargs=8, type=float, metavar=('FOV', 'FAR', 'AX', 'AY', 'AZ', 'CX', 'CY', 'CZ'))
    ap.add_argument('--hero', nargs=3, type=float)
    ap.add_argument('--scene', type=int, default=5)
    ap.add_argument('--map', type=lambda s: int(s, 0), default=0)
    ap.add_argument('--world', type=int)
    ap.add_argument('--list', action='store_true', help='imprime las celdas')
    a = ap.parse_args()
    if a.camera:
        fov, far, ax, ay, az, cx, cy, cz = a.camera
        c = dict(scene=a.scene, map=a.map, fov=fov, far=far, angles=(ax, ay, az), camera=(cx, cy, cz),
                 hero=tuple(a.hero), world=a.world or a.map + 1)
    else:
        c = CASES[a.case]
    fr, xs, ys, cells = run(c)
    att = load_att(c['world'])
    drawn = drop_hidden(cells, att)
    x0, y0, x1, y1 = fr['range']
    print('frustum: W %.6f H %.6f D %.6f' % (fr['W'], fr['H'], fr['D']))
    for i, p in enumerate(fr['points']):
        print('  P%d (%.6f, %.6f, %.6f)' % (i, *p))
    print('min (%.6f, %.6f) max (%.6f, %.6f)' % (*fr['min'], *fr['max']))
    print('bloques: x de %d a %d, y de %d a %d (inclusive, paso 4) -> %d x %d bloques' % (
        x0, x1, y0, y1, len(range(x0, x1 + 1, 4)), len(range(y0, y1 + 1, 4))))
    print('polígono (celdas): ' + ', '.join('(%.6f, %.6f)' % (x, y) for x, y in zip(xs, ys)))
    blocks = sorted({(y // 4 * 4, x // 4 * 4) for x, y in cells})
    print('pasan los dos tests: %d celdas en %d bloques; sin el bit 0x08: %d' % (len(cells), len(blocks), len(drawn)))
    near = min((abs(margin_of(xs, ys, x + 0.5, y + 0.5)), (x, y)) for y in range(max(0, y0), min(255, y1 + 3) + 1)
               for x in range(max(0, x0), min(255, x1 + 3) + 1))
    print('la decisión de celda más ajustada: |c| = %.6g en %s' % near)
    if a.list:
        print(drawn)
    if a.case in TRACES and not a.camera:
        trace = a.trace or os.path.join(os.environ.get('MURET_CLIENT_GL_TRACE_DIR', '.'), TRACES[a.case])
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import trace_cells
        tc = trace_cells.read(trace)
        print('traza: %d celdas opacas' % len(tc))
        if drawn == tc:
            print('IGUAL a la traza: mismo conjunto y mismo orden')
        else:
            s, t = set(drawn), set(tc)
            print('DISTINTO: sobran', sorted(s - t), 'faltan', sorted(t - s))
            for k, (u, v) in enumerate(zip(drawn, tc)):
                if u != v:
                    print('primer cambio de orden en', k, u, v)
                    break

if __name__ == '__main__':
    main()
