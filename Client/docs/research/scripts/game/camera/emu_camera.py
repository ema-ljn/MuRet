#!/usr/bin/env python3
"""La cámara del juego con el código real: 0x635ae0 (la cámara, una vez por frame dibujado) y 0x620db0
(el armado del pase 3D con esa cámara), sobre unicorn con el arnés de emulate_cloth.py.

`CameraEmu` es el arnés que usan make_camera_vectors.py, camera_reference.py y
../protection/camera_rand.py. Arranca como el juego: los inicializadores estáticos (el CCameraMove de
0x403220 y la D de 0x635ad0), el héroe en 0x743a170, el mapa 0, la escena 5 y el yaw −45 del primer
paso (0x636838). Cada caso imprime la cámara de uno o más frames; `lorencia` la compara con la
traza (glTranslatef de lorencia-Frame_016215.txt), bit a bit.

Uso: emu_camera.py [caso ...]      (con MURET_MAIN_EXE; sin casos, todos)
Casos: lorencia, zoom, bit40, rect, level, tour, map5, webzen, shake, scene24.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402
import emulate_cloth as ec  # noqa: E402

EXE = common.main_exe()

HERO_PTR = 0x743a170
HERO = ec.CHAR_BASE              # el personaje del héroe, al que apunta HERO_PTR
CAM_ANG = 0x7d36868
CAM_POS = 0x7d36884
ZOOM = 0x7d3689c
DIST = 0x7d36ba4
DIST_TARGET = 0x6d5ee0
LEVEL = 0x7d3a0ec
FOV, NEAR, FAR = 0x6d5d48, 0x6d5d40, 0x6d5d44
SPECIAL_H = 0x6c9d5c
MAP, SCENE = 0x6c9dd0, 0x6d5dc8
ATT = 0x7d0d348
SHAKE = 0x7d325c8
TIME = 0x5767b88
TOUR_PTR, TOUR_OBJ = 0x6e5904, 0x6e57a8
SCREEN_W, SCREEN_H = 0x6d5d60, 0x6d5d64

ec.GL_SIGNATURES.update({
    'glMatrixMode': 'i', 'glLoadIdentity': '', 'glViewport': 'iiii', 'glRotatef': 'ffff',
    'glTranslatef': 'fff', 'glGetFloatv': 'ii', 'gluPerspective': 'iiiiiiii', 'glFogi': 'ii',
    'glFogf': 'if', 'glFogfv': 'ii',
})


def bits(v):
    return '%08x' % struct.unpack('<I', struct.pack('<f', v))[0]


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


class CameraEmu:
    def __init__(self):
        self.emu = ec.ClothEmulator(EXE)
        e = self.emu
        # gluPerspective vive en glu32: el arnés solo deja pasar opengl32.
        for addr, (dll, name) in list(e.stubs.items()):
            if name == 'gluPerspective':
                e.stubs[addr] = ('opengl32.dll', name)
        e.construct_statics()
        # Los inicializadores estáticos que tocan la cámara: el CCameraMove global (0x403210 →
        # 0x403220 → 0x403250) y la distancia (0x635ad0: DIST = DIST_TARGET = 1000).
        e.call(0x403220)
        e.call(0x635ad0)
        e.uc.mem_write(ec.CHAR_BASE, b'\0' * 0x600)
        e.w32(HERO_PTR, ec.CHAR_BASE)
        e.w32(SCREEN_W, 1024)
        e.w32(SCREEN_H, 768)
        e.w32(MAP, 0)
        e.w32(SCENE, 5)
        # 0x636290 en el primer paso del juego (0x636838).
        e.wf(CAM_ANG + 8, -45.0)

    def hero(self, x, y, z, cx=None, cy=None, name=b''):
        e = self.emu
        h = ec.CHAR_BASE
        e.wfv(h + 0x10, [x, y, z])
        if cx is None:
            cx, cy = int(x / 100), int(y / 100)
        # +0x41c y +0x420 pasan por la tabla de protección: se escriben en claro solo si no tienen
        # registro todavía (la primera vez); después hay que dejar que el código real los cifre.
        if not getattr(self, '_cells_written', False):
            e.w32(h + 0x41c, cx)
            e.w32(h + 0x420, cy)
            self._cells_written = True
        e.uc.mem_write(h + 0x1d9, name + b'\0')

    def att(self, cx, cy, value):
        self.emu.w16(ATT + (cy * 256 + cx) * 2, value)

    def frame(self):
        e = self.emu
        e.events = []
        al = e.call(0x635ae0) & 0xff
        cam = {
            'ret_al': al,
            'pos': e.rfv(CAM_POS, 3), 'ang': e.rfv(CAM_ANG, 3), 'zoom': e.rf(ZOOM), 'dist': e.rf(DIST),
            'dist_target': e.rf(DIST_TARGET), 'level': e.r16(LEVEL), 'fov': e.rf(FOV), 'near': e.rf(NEAR),
            'far': e.rf(FAR), 'special_h': e.rf(SPECIAL_H),
        }
        return cam

    def setup_pass(self, rect):
        e = self.emu
        e.events = []
        e.call(0x620db0, args=rect)
        gl = []
        for ev in e.events:
            if 'gl' in ev:
                name, args = ev['gl'], ev['args']
                if name == 'gluPerspective':
                    raw = struct.pack('<8I', *args)
                    args = list(struct.unpack('<4d', raw))
                gl.append((name, args))
        return gl


def show(cam):
    p, a = cam['pos'], cam['ang']
    return ('pos=(%.6f %.6f %.6f)[%s %s %s] ang=(%.4f %.4f %.4f) zoom=%g D=%.7g lvl=%d far=%g fov=%g special=%g al=%d' %
            (p[0], p[1], p[2], bits(p[0]), bits(p[1]), bits(p[2]), a[0], a[1], a[2], cam['zoom'], cam['dist'],
             cam['level'], cam['far'], cam['fov'], cam['special_h'], cam['ret_al']))


def case_lorencia():
    c = CameraEmu()
    c.hero(14350.0, 12050.0, 165.0)
    c.att(143, 120, 0x01)
    cam = c.frame()
    print('lorencia', show(cam))
    gl = c.setup_pass((0, 0, 640, 432))
    for g in gl:
        print('   ', g)
    # La traza: glTranslatef(-15057.106445,-11342.893555,-1015.000000)
    exp = [f32(15057.106445), f32(11342.893555), f32(1015.0)]
    print('   igual a la traza (bits):', [bits(v) for v in cam['pos']] == [bits(v) for v in exp])
    return c


def case_zoom():
    c = CameraEmu()
    c.hero(14350.0, 12050.0, 165.0)
    out = []
    for k in range(60):
        c.att(143, 120, 0x81 if k < 30 else 0x01)
        cam = c.frame()
        out.append((k, cam['zoom'], cam['pos']))
    for k, z, p in out:
        if k in (0, 1, 2, 19, 20, 21, 22, 29, 30, 31, 50, 51, 52, 59):
            print('zoom frame %2d zoom=%5g pos=(%.4f %.4f %.4f)' % (k, z, p[0], p[1], p[2]))


def case_height40():
    c = CameraEmu()
    c.hero(14350.0, 12050.0, 165.0)
    c.att(143, 120, 0x41)
    cam = c.frame()
    print('bit40', show(cam))


def case_far_rect():
    for (x, y) in [(16100.0, 18900.0), (16099.99, 18900.0), (19000.0, 21700.0), (19000.01, 21700.0), (17500.0, 20000.0)]:
        c = CameraEmu()
        c.hero(x, y, 200.0)
        cam = c.frame()
        print('rect (%g, %g): far=%g' % (x, y, cam['far']))


def case_level():
    c = CameraEmu()
    c.hero(14350.0, 12050.0, 165.0)
    for lvl in [2, 4]:
        c.emu.w16(LEVEL, lvl)
        cam = c.frame()
        print('level puesto %d: far=%g level después=%d D=%.9g target=%g' % (lvl, cam['far'], cam['level'], cam['dist'], cam['dist_target']))
    # Ahora la convergencia de D desde 1400 hacia 1000.
    c.emu.wf(DIST, 1400.0)
    seq = []
    for k in range(40):
        cam = c.frame()
        seq.append(cam['dist'])
    print('D desde 1400:', ['%.9g' % v for v in seq[:12]], '...', '%.9g' % seq[-1], bits(seq[-1]))


def case_tour():
    c = CameraEmu()
    c.hero(14350.0, 12050.0, 165.0)
    e = c.emu
    # Estado 2 del CCameraMove, +0x6a y +0x158 en 0; posición +4/8/c y nivel +0x150.
    e.w32(TOUR_OBJ + 0x154, 2)
    e.w8(TOUR_OBJ + 0x6a, 0)
    e.w8(TOUR_OBJ + 0x158, 0)
    e.wfv(TOUR_OBJ + 4, [13000.0, 13000.0, 400.0])
    e.w32(TOUR_OBJ + 0x150, 3)
    cam = c.frame()
    print('tour', show(cam), 'hero z ahora', e.rf(ec.CHAR_BASE + 0x18))
    cam = c.frame()
    print('tour2', show(cam))


def case_map5():
    c = CameraEmu()
    c.emu.w32(MAP, 5)
    c.hero(14350.0, 12050.0, 165.0)
    for t in [0.0, 1000.0, 3141.5927, 123456.0]:
        c.emu.wf(TIME, t)
        cam = c.frame()
        print('map5 T=%g' % t, show(cam))


def case_webzen():
    # 0x576960 busca "webzen" como substring en el nombre del héroe (la cámara especial del mapa 6).
    # 'xwebzen2' y 'other' son nombres inventados para entrar y no entrar en esa rama.
    c = CameraEmu()
    c.emu.w32(MAP, 6)
    c.hero(6000.0, 15000.0, 100.0, name=b'xwebzen2')
    for k in range(2):
        cam = c.frame()
        print('webzen frame %d' % k, show(cam))
    c.emu.w32(MAP, 0)
    c.hero(14350.0, 12050.0, 165.0, name=b'other')
    cam = c.frame()
    print('después, en el mapa 0', show(cam))


def case_shake():
    c = CameraEmu()
    c.hero(14350.0, 12050.0, 165.0)
    c.emu.wf(SHAKE, 0.7)
    cam = c.frame()
    print('shake 0.7', show(cam))


def case_scene24():
    for sc in [2, 4]:
        c = CameraEmu()
        c.emu.w32(SCENE, sc)
        c.emu.w32(MAP, 55 if sc == 2 else 0)
        c.hero(11679.0, 14191.0, 333.0)
        cam = c.frame()
        print('scene %d' % sc, show(cam))


CASES = {
    'lorencia': case_lorencia, 'zoom': case_zoom, 'bit40': case_height40, 'rect': case_far_rect,
    'level': case_level, 'tour': case_tour, 'map5': case_map5, 'webzen': case_webzen, 'shake': case_shake,
    'scene24': case_scene24,
}

if __name__ == '__main__':
    names = sys.argv[1:] or list(CASES)
    for n in names:
        try:
            CASES[n]()
        except Exception as ex:  # noqa: BLE001
            print('caso %s: error %s' % (n, ex))
