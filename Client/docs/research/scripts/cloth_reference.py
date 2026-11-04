#!/usr/bin/env python3
r"""La tela de contract.md, escrita de nuevo en Python a partir del contrato (no del binario), para
compararla bit a bit con lo que da el código real en emulate_cloth.py.

Cada cuenta sigue el orden de la x87 del original con la precisión en 53 bits (la palabra de
control 0x27f): lo que el original guarda con FSTP/FST float pasa por f32(); lo que deja en la pila
sigue en double. Ver contract.md § El paso, donde cada línea dice qué se redondea.

    python cloth_reference.py example.json      # repite los frames de la entrada de example.json
                                                # y compara cada paso, cada tela y cada vértice
"""

import json
import math
import struct
import sys

F = lambda bits: struct.unpack('<f', struct.pack('<I', bits))[0]  # noqa: E731

# Las constantes, por su dirección (contract.md § Constantes).
C_04 = F(0x3ecccccc)     # 0x6b8cf4: 0,4 (el ancho de la fila crece de 0,6 a 1,0)
C_06 = F(0x3f19999a)     # 0x6b8a20
C_HALF = F(0x3f000000)   # 0x6b882c
C_10 = F(0x41200000)     # 0x6b87a8
C_20 = F(0x41a00000)     # 0x6b86d0 (y la y por defecto del armado, 0x41a00000 inmediato)
C_08 = F(0x3f4ccccd)     # 0x6b8a38
C_001 = F(0x3c23d70a)    # 0x6b88d4 (y 0x6b86c0, el mismo valor)
C_01 = F(0x3dcccccd)     # 0x6b87b8
C_100 = F(0x42c80000)    # 0x6b86b4
C_180 = F(0x43340000)    # 0x6b86bc
C_002 = F(0x3ca3d70a)    # 0x6b89e4
C_DEG = F(0x3c8efa35)    # 0x6b8cf8 y 0x6ba360: π/180
C_0001 = F(0x3a83126f)   # 0x6b89c8
C_1_32 = F(0x3d000000)   # 0x6b8880
C_3 = F(0x40400000)      # 0x6b86b8
G_98 = F(0x411ccccd)     # 0x6c7a40
G_K = F(0x3b23d70a)      # 0x6c7a44
MASS = F(0x43c80000)     # 0x6c7a48: 400
C_04B = F(0x3ecccccd)    # 0x6b87b4
C_02 = F(0x3e4ccccd)     # 0x6b8854 y 0x6b8cfc
C_099 = F(0x3f7d70a4)    # 0x6b8d00 y el inmediato de 0x455c40
C_M02 = F(0xbe4ccccd)    # 0x6b8d3c
C_1 = 1.0


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def cdiv(a, b):
    """La división entera de la x86 (IDIV): trunca hacia 0."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def cmod(a, b):
    return a - b * cdiv(a, b)


def s32(x):
    x &= 0xffffffff
    return x - (1 << 32) if x & 0x80000000 else x


class MsvcRand:
    def __init__(self, spec):
        if isinstance(spec, list):
            self.values, self.state = list(spec), None
        else:
            self.values, self.state = None, spec['msvc_state'] & 0xffffffff
        self.index = 0

    def __call__(self):
        if self.values is not None:
            v = self.values[self.index]
        else:
            self.state = (self.state * 0x343fd + 0x269ec3) & 0xffffffff
            v = (self.state >> 16) & 0x7fff
        self.index += 1
        return v


# --- la matemática de Half-Life del binario ------------------------------------------------------
def angle_matrix(angles):
    """0x5fc8f0: el coseno del ángulo 2 queda en la pila (double); el resto, en floats."""
    a2 = angles[2] * C_DEG
    sy = f32(math.sin(a2))
    cy = math.cos(a2)
    a1 = angles[1] * C_DEG
    sp = f32(math.sin(a1))
    cp = f32(math.cos(a1))
    a0 = angles[0] * C_DEG
    sr = f32(math.sin(a0))
    cr = f32(math.cos(a0))
    m = [[0.0] * 4 for _ in range(3)]
    m[0][0] = f32(cp * cy)
    m[1][0] = f32(cp * sy)
    m[2][0] = f32(-sp)
    t1 = sr * sp
    m[0][1] = f32(t1 * cy - cr * sy)
    m[1][1] = f32(t1 * sy + cr * cy)
    m[2][1] = f32(sr * cp)
    t2 = cr * sp
    m[0][2] = f32(t2 * cy + sr * sy)
    m[1][2] = f32(t2 * sy - sr * cy)
    m[2][2] = f32(cr * cp)
    return m


def vector_transform(v, m):
    """0x5fccc0, con el orden de las sumas de cada fila."""
    x = f32(((m[0][1] * v[1] + m[0][2] * v[2]) + v[0] * m[0][0]) + m[0][3])
    y = f32(((m[1][0] * v[0] + m[1][1] * v[1]) + m[1][2] * v[2]) + m[1][3])
    z = f32(((m[2][0] * v[0] + m[2][1] * v[1]) + m[2][2] * v[2]) + m[2][3])
    return [x, y, z]


def transform_position(m, v, scale, origin):
    """0x4ec110 con la bandera en 1: la x sin redondear antes de sumar el origen; la y y la z, sí."""
    t = vector_transform(v, m)
    x = f32(scale * t[0] + origin[0])
    y = f32(f32(scale * t[1]) + origin[1])
    z = f32(f32(scale * t[2]) + origin[2])
    return [x, y, z]


# --- la tela ----------------------------------------------------------------------------------
class Particle:
    __slots__ = ('force', 'vel', 'pos', 'pinned', 'count', 'corr')

    def __init__(self):
        self.force = [0.0, 0.0, 0.0]
        self.vel = [0.0, 0.0, 0.0]
        self.pos = [0.0, 0.0, 0.0]
        self.pinned = 0
        self.count = 0
        self.corr = [0.0, 0.0, 0.0]


def distance(a, b):
    """0x4543c0: la diferencia en floats y la raíz de (dx² + dy²) + dz² en double."""
    d = [f32(a.pos[k] - b.pos[k]) for k in range(3)]
    return d, math.sqrt((d[0] * d[0] + d[1] * d[1]) + d[2] * d[2])


def clamped_distance(a, b):
    d, length = distance(a, b)
    return d, (C_0001 if length < C_0001 else length)




class Cloth:
    """La tela (0x60 bytes, vtable 0x6b8cd8). `init` es 0x454a50 y se puede llamar otra vez sobre la
    misma: así lo hace 0x50ba30 con la cuarta (las banderas se suman con OR y las esferas quedan)."""

    def __init__(self, world):
        self.world = world
        self.flags = 0
        self.spheres = []
        self.wind = 0.0
        self.reset()

    def reset(self):
        """0x454a00 (y el final de 0x454f80)."""
        self.bone = 0
        self.front = self.back = 0
        self.flags = 0
        self.x = self.y = self.z = 0.0
        self.width = self.height = 1.0
        self.nh = self.nv = self.n = 0
        self.particles = []
        self.springs = []
        self.dx = self.dy = 1.0
        self.wind_range = self.wind_min = 1

    def release(self):
        """0x454f80 (vtable + 8): suelta las esferas, los resortes y las partículas."""
        self.spheres = []
        self.reset()

    # 0x454a50
    def init(self, bone, x, y, z, columns, rows, width, height, front, back, flags):
        self.bone = bone
        self.front, self.back = front, back
        self.flags |= flags & 0xffffffff
        self.x, self.y, self.z = f32(x), f32(y), f32(z)
        self.width, self.height = f32(width), f32(height)
        self.nh, self.nv = columns, rows
        self.n = columns * rows
        self.particles = [Particle() for _ in range(self.n)]
        self.springs = []
        self.dx = f32(self.width / (columns - 1))
        self.dy = f32(self.height / (rows - 1))
        w = self.world
        m = angle_matrix(w.angles)
        for r in range(3):
            m[r][3] = w.bones[self.bone][r][3]
        taper = (self.flags & 0xc) == 4
        arch = self.flags & 3
        for j in range(self.nv):
            dx = self.dx
            span = self.width
            if taper:
                span = self.width * ((j * C_04) / (self.nv - 1) + C_06)
                dx = f32(span / (self.nh - 1))
            half = f32(span * C_HALF)
            z = f32(-(j * self.dy))
            for i in range(self.nh):
                xx = f32(i * dx - half)
                if arch == 1:
                    t = abs(i / (self.nh - 1) - C_HALF) * 2.0
                    yy = f32(C_20 - t * t * C_10)
                elif arch == 2:
                    yy = 0.0
                else:
                    yy = C_20
                self.particles[j * self.nh + i].pos = transform_position(m, [xx, yy, z], w.scale, w.origin)
        pull_flag = 0 if (self.flags & 0x300) == 0x100 else 4
        diagonal_flag = 0 if (self.flags & 0x300) == 0x100 else 1
        nh, nv, p = self.nh, self.nv, self.particles

        def add(a, b, flag):
            length = f32(distance(p[a], p[b])[1])
            self.springs.append([a, b, f32(length * C_08), length, flag])
        for j in range(nv):
            for i in range(nh):
                k = nh * j + i
                if j < nv - 1:
                    add(k, k + nh, pull_flag | 2)
                if i < nh - 1:
                    add(k, k + 1, 3)
                    if j < nv - 1:
                        add(k, k + nh + 1, diagonal_flag)
                    if j > 1:
                        add(k, k - nh + 1, diagonal_flag)
        self.pin_top_row(m)

    # 0x455020
    def pin_top_row(self, m):
        w = self.world
        taper = (self.flags & 0xc) == 4
        for i in range(self.nh):
            if taper:
                span = self.width * C_06
                dx = span / (self.nh - 1)
            else:
                span = self.width
                dx = self.dx
            xi = f32((i * dx - span * C_HALF) + self.x)
            y = self.y
            if (self.flags & 3) == 1:
                t = abs(i / (self.nh - 1) - C_HALF) * 2.0
                y = f32(y - t * t * C_10)
            p = self.particles[i]
            p.pos = transform_position(m, [self.z, -y, xi], w.scale, w.origin)
            p.pinned = 1

    # 0x455cd0
    def add_sphere(self, x, y, z, radius, bone):
        self.spheres.append({'local': [f32(x), f32(y), f32(z)], 'radius': f32(radius), 'bone': bone,
                             'world': [0.0, 0.0, 0.0]})

    # 0x455190
    def move(self, dt, count, out):
        for iteration in range(count):
            ok = self.step(dt)
            out.append({'step': {'cloth': self.index, 'iteration': iteration, 'ok': ok,
                                 'state': [{'pos': list(q.pos), 'vel': list(q.vel), 'force': list(q.force),
                                            'pinned': q.pinned} for q in self.particles],
                                 'wind': {'cloth_44': self.wind, 'wind_7245f0': self.world.wind_base,
                                          'wind_7245f4': self.world.wind[0], 'wind_7245f8': self.world.wind[1],
                                          'wind_7245fc': self.world.wind[2]}}})
            if not ok:
                return 0
        return 1

    # 0x4551d0
    def step(self, dt):
        w = self.world
        if (self.flags & 0x300) == 0x200:
            self.wind = f32((w.rand() % self.wind_range + self.wind_min) * C_001)
        elif w.map == 0x37:
            self.wind = f32((w.rand() % 40 + 10) * C_002)
        else:
            self.wind = w.wind_base
        a = (w.angles[2] + C_180) * C_DEG
        w.wind[0] = f32(math.sin(a) * self.wind)
        w.wind[1] = f32(-(math.cos(a) * self.wind))
        self.forces()
        self.spring_forces()
        self.pin_top_row(w.bones[self.bone])
        for p in self.particles:
            if not p.pinned:
                for k in range(3):
                    v = (MASS * p.force[k]) * dt + p.vel[k]
                    p.vel[k] = f32(v)
                    p.pos[k] = f32(v * dt + p.pos[k])
        return self.constraints()

    # 0x4552a0 y 0x454240
    def forces(self):
        w = self.world
        if w.lock_depth == 0:
            for _ in range(4):
                w.rand()      # la protección vuelve a cifrar 0x7d3a0f0
        v = s32(w.frame_counter)
        center = cmod(s32(cdiv(v, 10) * 101), self.n)
        g = G_K * G_98
        for i, p in enumerate(self.particles):
            d = abs(cmod(center, self.nh) - cmod(i, self.nh)) + abs(cdiv(center, self.nh) - cdiv(i, self.nh))
            if p.pinned:
                p.force = [0.0, 0.0, 0.0]
                continue
            u = (5 - d) & 0xffffffff
            weight = 6 if u >= 4 else (0 if u == 0 else u + 2)
            for k in range(3):
                p.force[k] = f32(weight * w.wind[k] - p.vel[k] * C_001)
            mode = self.flags & 0x300
            if mode == 0x100:
                p.force[2] = f32((self.wind + C_01) * weight + p.force[2])
            elif mode == 0x200:
                p.force[2] = f32(weight * self.wind + p.force[2])
            if (self.flags & 0xc00) == 0x400:
                p.force[2] = f32(p.force[2] - g * C_180)
            else:
                p.force[2] = f32(p.force[2] - g * C_100)

    # 0x4555d0, el lazo de los resortes con la bandera 2
    def spring_forces(self):
        p = self.particles
        for a, b, _short, rest, flag in self.springs:
            if not flag & 2:
                continue
            d, length = clamped_distance(p[a], p[b])
            if not length > rest + C_001:
                continue
            e = length - rest
            f = []
            for k in range(3):
                value = e * d[k] / length
                stored = f32(value)
                if self.flags & 0x20000000:
                    stored = f32((rest * C_1_32) * value)
                if (self.flags & 0x300) == 0x100:
                    stored = f32(stored * C_3)
                f.append(stored)
            for k in range(3):
                p[a].force[k] = f32(-f[k] + p[a].force[k])
            for k in range(3):
                p[b].force[k] = f32(f[k] + p[b].force[k])

    # 0x455770
    def constraints(self):
        w = self.world
        p = self.particles
        for s in self.spheres:
            x, y, z = s['local']
            s['world'] = transform_position(w.bones[s['bone']], [z, -y, x], w.scale, w.origin)
        if self.spheres:
            for s in self.spheres:
                c, r = s['world'], s['radius']
                for q in p:
                    dx = f32(q.pos[0] - c[0])
                    dy = f32(q.pos[1] - c[1])
                    dz_exact = q.pos[2] - c[2]
                    dz = f32(dz_exact)
                    dist = math.sqrt((dz_exact * dz + dy * dy) + dx * dx)
                    if dist < C_001:
                        dist = C_001
                        dx, dy, dz = C_001, 0.0, 0.0
                    if dist < r:
                        k = (r - dist) / dist
                        dd = [f32(k * dx), f32(k * dy), f32(k * dz)]
                        for m in range(3):
                            q.corr[m] = f32(dd[m] + q.corr[m])
                        q.count += 1
            for q in p:
                self.apply(q)
        for a, b, _short, rest, flag in self.springs:
            if not flag & 1:
                continue
            d, length = clamped_distance(p[a], p[b])
            k = ((length - rest) * C_HALF) / length
            dx, dy, dz = f32(k * d[0]), f32(k * d[1]), k * d[2]
            p[a].corr = [f32(p[a].corr[0] - dx), f32(p[a].corr[1] - dy), f32(p[a].corr[2] - dz)]
            p[b].corr = [f32(dx + p[b].corr[0]), f32(dy + p[b].corr[1]), f32(dz + p[b].corr[2])]
            p[a].count += 1
            p[b].count += 1
        for q in p:
            self.apply(q)
        for a, b, short, rest, flag in self.springs:
            if b < self.nh or not flag & 4:
                continue
            this, other = p[b], p[a]
            if this.pinned:
                continue
            d, length = clamped_distance(this, other)
            if length > rest * C_20:
                return 0
            if length > rest:
                k = (length - rest) / length
            elif length < short:
                k = (length - short) / length
            else:
                continue
            dx, dy, dz = f32(k * d[0]), f32(k * d[1]), k * d[2]
            this.pos = [f32(this.pos[0] - dx), f32(this.pos[1] - dy), f32(this.pos[2] - dz)]
        return 1

    @staticmethod
    def apply(q):
        """0x454570."""
        if q.pinned:
            q.corr = [0.0, 0.0, 0.0]
            return
        if q.count > 0:
            q.corr = [f32(c / q.count) for c in q.corr]
            q.pos = [f32(q.corr[0] + q.pos[0]), f32(q.corr[1] + q.pos[1]), f32(q.pos[2] + q.corr[2])]
            q.corr = [0.0, 0.0, 0.0]
            q.count = 0

    # 0x455930, 0x455b10 y 0x455c40
    def render(self, color, out):
        w, gl = self.world, self.world.gl
        out.append({'render': {'cloth': self.index, 'color': list(color) if color is not None else None}})
        verts = [list(q.pos) for q in self.particles]
        mode = self.flags & 0x3000
        if mode == 0:
            gl.opaque(out)
        elif mode == 0x1000:
            gl.alpha_test(1, out)
        elif mode == 0x2000:
            gl.additive(out)
        if color is not None:
            out.append({'gl': 'glColor3fv', 'args': [list(color)]})
        else:
            out.append({'gl': 'glColor3f', 'args': [1.0, 1.0, 1.0]})
        if self.flags & 0x40000000:
            t = f32(math.sin(w.world_time * C_0001) * C_01 + C_04B)
            k = 0
            for i in range(self.nh):
                for _j in range(self.nv):
                    light = f32((self.nh - i) * t * C_02)
                    out.append({'sprite_0x5491e0': {'texture': '0x5ac', 'position': verts[k],
                                                    'scale': f32(i * C_02 + C_04B), 'light': [light] * 3,
                                                    'arg5_owner': 0, 'arg6_rotation': 0.0, 'arg7_subtype': 0}})
                    k += 1
            out.append({'gl': 'glColor3f', 'args': [t, t, t]})
            gl.additive(out)
        self.quads(True, self.front, verts, out)
        if not (mode == 0x2000 and self.flags & 0x40000000):
            self.quads(False, self.back, verts, out)

    def quads(self, front, texture, verts, out):
        self.world.gl.bind(texture, out)
        out.append({'gl': 'glBegin', 'args': [7]})
        for j in range(self.nv - 1):
            for i in range(self.nh - 1):
                corners = [(i, j), (i + 1, j), (i + 1, j + 1), (i, j + 1)] if front else \
                          [(i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)]
                for ci, cj in corners:
                    v = cj / (self.nv - 1)
                    v = C_099 if C_099 < v else f32(v)
                    out.append({'gl': 'glTexCoord2f', 'args': [f32(ci / (self.nh - 1)), v]})
                    out.append({'gl': 'glVertex3f', 'args': list(verts[self.nh * cj + ci])})
        out.append({'gl': 'glEnd', 'args': []})


class GLCache:
    """Los wrappers de 0x620xxx y sus cachés en la .data (0x7d366dc la mezcla, 0x7d366cd el alpha
    test, 0x7d366d5 la textura, 0x7d366cc el culling, 0x7d36898 la máscara de profundidad, 0x7d368a0
    la niebla, 0x6d5d68 el slot atado)."""
    BLEND, CULL, ALPHA_TEST, TEXTURE_2D, FOG = 0xbe2, 0xb44, 0xbc0, 0xde1, 0xb60

    def __init__(self, g, names):
        self.blend = g.get('gl_blend_7d366dc', 0)
        self.alpha = g.get('gl_alpha_test_7d366cd', 0)
        self.texture = g.get('gl_texture_7d366d5', 0)
        self.cull = g.get('gl_cull_7d366cc', 0)
        self.mask = g.get('gl_depth_mask_7d36898', 0)
        self.fog = g.get('gl_fog_7d368a0', 0)
        self.bound = g.get('bound_texture_6d5d68', 0)
        self.names = names

    def update(self, g):
        for key, attr in (('gl_blend_7d366dc', 'blend'), ('gl_alpha_test_7d366cd', 'alpha'),
                          ('gl_texture_7d366d5', 'texture'), ('gl_cull_7d366cc', 'cull'),
                          ('gl_depth_mask_7d36898', 'mask'), ('gl_fog_7d368a0', 'fog'),
                          ('bound_texture_6d5d68', 'bound')):
            if key in g:
                setattr(self, attr, g[key])

    def _call(self, out, name, *args):
        out.append({'gl': name, 'args': list(args)})

    def _cull_on(self, out):        # 0x6208d0
        if not self.cull:
            self.cull = 1
            self._call(out, 'glEnable', self.CULL)

    def _cull_off(self, out):       # 0x6208f0
        if self.cull:
            self.cull = 0
            self._call(out, 'glDisable', self.CULL)

    def _mask_on(self, out):        # 0x620890
        if not self.mask:
            self.mask = 1
            self._call(out, 'glDepthMask', 1)

    def _mask_off(self, out):       # 0x6208b0
        if self.mask:
            self.mask = 0
            self._call(out, 'glDepthMask', 0)

    def opaque(self, out):          # 0x620980
        if self.blend != 0:
            self.blend = 0
            self._call(out, 'glDisable', self.BLEND)
        self._cull_on(out)
        self._mask_on(out)
        if self.alpha:
            self.alpha = 0
            self._call(out, 'glDisable', self.ALPHA_TEST)
        if not self.texture:
            self.texture = 1
            self._call(out, 'glEnable', self.TEXTURE_2D)
        if self.fog:
            self._call(out, 'glEnable', self.FOG)

    def alpha_test(self, with_mask, out):     # 0x620a00
        if self.blend != 2:
            self.blend = 2
            self._call(out, 'glEnable', self.BLEND)
            self._call(out, 'glBlendFunc', 0x302, 0x303)
        self._cull_off(out)
        if with_mask:
            self._mask_on(out)
        if not self.alpha:
            self.alpha = 1
            self._call(out, 'glEnable', self.ALPHA_TEST)
        if not self.texture:
            self.texture = 1
            self._call(out, 'glEnable', self.TEXTURE_2D)
        if self.fog:
            self._call(out, 'glEnable', self.FOG)

    def additive(self, out):        # 0x620a90
        if self.blend != 3:
            self.blend = 3
            self._call(out, 'glEnable', self.BLEND)
            self._call(out, 'glBlendFunc', 1, 1)
        self._cull_off(out)
        self._mask_off(out)
        if self.alpha:
            self.alpha = 0
            self._call(out, 'glDisable', self.ALPHA_TEST)
        if not self.texture:
            self.texture = 1
            self._call(out, 'glEnable', self.TEXTURE_2D)
        if self.fog:
            self._call(out, 'glDisable', self.FOG)

    def bind(self, slot, out):      # 0x620780
        if self.bound != slot:
            self.bound = slot
            name = self.names.get(slot, 0) if slot >= 0 else -slot
            self._call(out, 'glBindTexture', 0xde1, name)


class World:
    """Lo que la tela lee de afuera: el objeto, los huesos, el modelo y los globales."""

    def __init__(self, rand, gl):
        self.rand = rand
        self.gl = gl
        self.bones = None
        self.angles = [0.0, 0.0, 0.0]
        self.scale = 1.0
        self.origin = [0.0, 0.0, 0.0]
        self.map = 0
        self.lock_depth = 0
        self.frame_counter = 0
        self.world_time = 0.0
        self.wind_base = 0.0
        self.wind = [0.0, 0.0, 0.0]


class Character:
    """El bloque de 0x50ba30 (0x50d58b..0x50db57) sobre el objeto del personaje."""

    def __init__(self, world):
        self.world = world
        self.slots = None       # objeto + 0x190
        self.count = 0          # objeto + 0x18c

    def _new(self, n):
        self.slots = []
        for index in range(n):
            c = Cloth(self.world)
            c.index = index
            self.slots.append(c)

    @staticmethod
    def _init(c, out, *args):
        names = ('bone', 'x', 'y', 'z', 'columns', 'rows', 'width', 'height', 'texture_front', 'texture_back', 'flags')
        e = dict(zip(names, args))
        e['texture_front'], e['texture_back'], e['flags'] = hex(e['texture_front'] & 0xffffffff), \
            hex(e['texture_back'] & 0xffffffff), hex(e['flags'] & 0xffffffff)
        out.append({'init': e})
        c.init(*args)

    @staticmethod
    def _sphere(c, out, x, y, z, r, bone):
        out.append({'sphere': {'x': x, 'y': y, 'z': z, 'radius': r, 'bone': bone}})
        c.add_sphere(x, y, z, r, bone)

    def block(self, ch, color, bvar23, out):
        if not bvar23:
            return
        klass = ch.get('class_1d4', 4) & 7
        armor = ch.get('armor_238', 0) & 0xffff
        if self.slots is None:
            if klass == 4:
                self._new(4)
                s = self.slots
                self._init(s[0], out, 0x14, 0.0, 0.0, 20.0, 6, 5, 30.0, 70.0, 0x256, 0x256, 0xc0000205)
                s[0].wind_min, s[0].wind_range = 10, 50
                self._sphere(s[0], out, -10.0, 20.0, 20.0, 27.0, 0x11)
                self._sphere(s[0], out, 10.0, 20.0, 20.0, 27.0, 0x11)
                self._init(s[1], out, 0x14, 0.0, 5.0, 18.0, 5, 5, 30.0, 70.0, 0x256, 0x256, 0x80002205)
                s[1].wind_min, s[1].wind_range = 8, 40
                self._sphere(s[1], out, -10.0, 20.0, 20.0, 27.0, 0x11)
                self._sphere(s[1], out, 10.0, 20.0, 20.0, 27.0, 0x11)
                self._init(s[2], out, 0x13, 0.0, 8.0, 10.0, 10, 10, 180.0, 180.0, 0x257, 0x257, 0x1005)
                for x, y, z, r in [(-10.0, -10.0, -10.0, 25.0), (10.0, -10.0, -10.0, 25.0),
                                   (-10.0, -10.0, 20.0, 27.0), (10.0, -10.0, 20.0, 27.0)]:
                    self._sphere(s[2], out, x, y, z, r, 0x11)
                if armor == 0x221e:
                    self._init(s[3], out, 0x12, 0.0, 10.0, -5.0, 5, 5, 50.0, 90.0, 0x6ea, 0x6ea, 0x1406)
                    self._sphere(s[3], out, 0.0, -15.0, -20.0, 30.0, 2)
                self.count = 3
            else:
                self._new(1)
                c = self.slots[0]
                if (ch.get('monster_376', -1) & 0xffff) == 0x37:
                    self._init(c, out, 0x13, 0.0, 10.0, 0.0, 10, 10, 55.0, 140.0, 0x252, 0x252, 0x1001)
                    self._sphere(c, out, -10.0, -10.0, -10.0, 35.0, 0x11)
                    self._sphere(c, out, 10.0, -10.0, -10.0, 35.0, 0x11)
                    self._sphere(c, out, 0.0, -10.0, -40.0, 50.0, 0x13)
                else:
                    self._init(c, out, 0x13, 0.0, 10.0, 0.0, 10, 10, 75.0, 120.0, 0x250, 0x251, 0x405)
                    for x, y, z, r in [(-10.0, -10.0, -10.0, 25.0), (10.0, -10.0, -10.0, 25.0),
                                       (-10.0, -10.0, 20.0, 27.0), (10.0, -10.0, 20.0, 27.0)]:
                        self._sphere(c, out, x, y, z, r, 0x11)
                self.count = 1
        elif klass == 4:
            if armor == 0x221e:
                if self.count == 3:
                    c = self.slots[3]
                    self._init(c, out, 0x12, 0.0, 8.0, -5.0, 5, 5, 50.0, 90.0, 0x6ea, 0x6ea, 0x1407)
                    self._sphere(c, out, 0.0, -15.0, -20.0, 30.0, 2)
                    self.count = 4
            elif self.count == 4:
                self.slots[3].release()
                self.count = 3
        flags78 = ch.get('flags_78', 0)
        if flags78 & 0x4000:
            return
        index = 0
        while index < self.count:
            skip = (klass == 4 and index == 2 and (ch.get('wings_2f8', -1) & 0xffff) != 0x1c30 and
                    color == [1.0, 1.0, 1.0])
            if not skip:
                dt = 0.0 if flags78 & 0x1000 else F(0x3ba3d70a)
                c = self.slots[index]
                if not c.move(dt, 5, out):
                    out.append({'destroy_0x4fb1a0': True})
                    for k in range(self.count):
                        self.slots[k].release()
                    self.slots = None
                    self.count = 0
                else:
                    c.render(color, out)
            index += 1


def run(spec):
    """Los frames de la entrada, como emulate_cloth.run (el bloque de 0x50ba30 o el modo directo)."""
    rand = MsvcRand(spec.get('rand', {'msvc_state': 0}))
    first = spec['frames'][0].get('globals', {})
    names = {int(k, 0): v for k, v in spec.get('texture_gl_names', {}).items()}
    world = World(rand, GLCache(first, names))
    character = Character(world)
    direct = None
    frames = []
    for number, frame in enumerate(spec['frames']):
        ch = frame.get('character', {})
        world.bones = [[m[0:4], m[4:8], m[8:12]] for m in frame['bones']]
        world.angles = [f32(a) for a in ch.get('angle_1c', [0.0, 0.0, 0.0])]
        model = frame.get('model', {})
        world.scale = f32(model.get('body_scale_68', ch.get('scale_0c', 1.0)))
        world.origin = [f32(a) for a in model.get('body_origin_6c', ch.get('position_10', [0.0, 0.0, 0.0]))]
        g = frame.get('globals', {})
        world.gl.update(g)
        world.map = g.get('map_6c9dd0', world.map)
        world.world_time = f32(g.get('world_time_5767b88', world.world_time))
        if 'wind_base_7245f0' in g:
            world.wind_base = f32(g['wind_base_7245f0'])
        if 'wind_z_7245fc' in g:
            world.wind[2] = f32(g['wind_z_7245fc'])
        world.lock_depth = frame.get('frame_counter_lock_depth', 0)
        world.frame_counter = frame.get('frame_counter_7d3a0f0', 0)
        for _ in range(frame.get('wind_walk_calls', 0)):        # 0x456b10
            base = f32(world.wind_base + (rand() % 200 - 100) * C_0001)
            if not C_M02 <= base:
                base = C_M02
            elif base >= C_1:
                base = C_1
            world.wind_base = base
        wind_after_walk = world.wind_base
        out = []
        color = frame.get('color', [1.0, 1.0, 1.0])
        color = None if color is None else [f32(c) for c in color]
        if 'direct' in spec:
            if direct is None:
                direct = []
                for index, d in enumerate(spec['direct']):
                    c = Cloth(world)
                    c.index = index
                    i = d['init']
                    Character._init(c, out, i['bone'], i['x'], i['y'], i['z'], i['columns'], i['rows'], i['width'],
                                    i['height'], i['front'], i['back'], i['flags'])
                    if 'wind_bytes' in d:
                        c.wind_min, c.wind_range = d['wind_bytes']
                    for x, y, z, r, bone in d.get('spheres', []):
                        Character._sphere(c, out, x, y, z, r, bone)
                    c.broken = False
                    direct.append(c)
            dt = f32(frame.get('dt', F(0x3ba3d70a)))
            for c in direct:
                if c.broken:
                    continue
                if c.move(dt, 5, out):
                    c.render(color, out)
                else:
                    out.append({'broken': c.index})
                    c.broken = True
        else:
            character.block(ch, color, frame.get('bvar23', 1), out)
        frames.append({'frame': number, 'wind_base_after_walk': wind_after_walk, 'events': out,
                       'count': character.count, 'rand_index': rand.index})
    return frames


def bits(x):
    return struct.pack('<f', x)


def compare(result):
    """Compara la salida de emulate_cloth.py (`result`, con su entrada) con esta reimplementación:
    cada init y cada esfera, cada paso (cada partícula: posición, velocidad, fuerza y anclaje; el viento),
    cada llamada a GL con sus argumentos (los vértices, las UV, los colores, la mezcla y las texturas),
    cada sprite, cada destrucción y la cantidad de rand() de cada frame."""
    spec = result['input']
    ours = run(spec)
    checked = 0
    bad = []

    def note(what):
        bad.append(what)

    def same_value(a, b, where):
        nonlocal checked
        checked += 1
        if isinstance(a, float) or isinstance(b, float):
            if bits(float(a)) != bits(float(b)):
                note('%s: %r != %r' % (where, a, b))
        elif a != b:
            note('%s: %r != %r' % (where, a, b))

    def same(a, b, where):
        if isinstance(a, dict) and isinstance(b, dict):
            for k in b:
                if k in ('cloth_address', 'object'):
                    continue
                if k not in a:
                    note('%s: falta %s' % (where, k))
                    continue
                same(a[k], b[k], where + '.' + k)
        elif isinstance(a, list) and isinstance(b, list):
            if len(a) != len(b):
                note('%s: largo %d != %d' % (where, len(a), len(b)))
            for k, (x, y) in enumerate(zip(a, b)):
                same(x, y, '%s[%d]' % (where, k))
        else:
            same_value(a, b, where)
    kinds = ('init', 'sphere', 'step', 'render', 'gl', 'sprite_0x5491e0', 'destroy_0x4fb1a0', 'broken')
    for emu, ref in zip(result['frames'], ours):
        n = emu['frame']
        same(emu['wind_base_after_walk'], ref['wind_base_after_walk'], 'f%d viento base' % n)
        ev = [e for e in emu['events'] if any(k in e for k in kinds)]
        rv = ref['events']
        if len(ev) != len(rv):
            note('f%d: %d eventos en el emulador, %d en la referencia' % (n, len(ev), len(rv)))
        for k, (a, b) in enumerate(zip(ev, rv)):
            ka = [x for x in kinds if x in a][0]
            kb = [x for x in kinds if x in b][0]
            if ka != kb:
                note('f%d evento %d: %s != %s' % (n, k, ka, kb))
                break
            if ka == 'gl':
                if a['gl'] != b['gl']:
                    note('f%d evento %d: %s != %s' % (n, k, a['gl'], b['gl']))
                    break
                same(a['args'], b['args'], 'f%d %s' % (n, a['gl']))
            elif ka == 'destroy_0x4fb1a0':
                checked += 1
            else:
                same(a[ka], b[ka], 'f%d %s' % (n, ka))
        rands = sum(1 for e in emu['events'] if 'rand' in e)
        previous = ours[n - 1]['rand_index'] if n else 0
        same(rands + (0 if n else 0), ref['rand_index'] - previous, 'f%d rand() del frame' % n)
        if 'count' in ref and 'object_18c' in emu and 'direct' not in spec:
            same(emu['object_18c'], ref['count'], 'f%d objeto + 0x18c' % n)
    return checked, bad


if __name__ == '__main__':
    total_bad = 0
    for path in sys.argv[1:]:
        data = json.load(open(path))
        checked, bad = compare(data)
        for line in bad[:15]:
            print('  ', line)
        print('%s: %d valores comparados, %d distintos -> %s' % (path, checked, len(bad),
                                                                 'IGUAL BIT A BIT' if not bad else 'DISTINTO'))
        total_bad += len(bad)
    sys.exit(1 if total_bad else 0)
