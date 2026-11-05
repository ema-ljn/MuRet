#!/usr/bin/env python3
"""El paso real del héroe caminando, paso de lógica por paso de lógica, sobre el .att y las alturas:
0x4f37c0 (SetPlayerWalk), 0x4ea270 (MovePath, con el giro y la llegada) y, si no llegó, 0x508a80
(MoveCharacterPosition, con la velocidad de 0x508940 y la altura de 0x5f93c0).

`WalkEmu(mundo)` carga el .att (en el mapa de paredes 0x7d0d348) y las alturas (0x7accca4, ×1,5) del
mundo, pone la escena 5 y el mapa, y abre en la tabla de protección la celda lógica del héroe (+0x41c,
+0x420) para que quede en claro. SetAction (0x4ea030) y 0x41ed10 (el sonido) no corren: SetAction anota
la acción. Las velocidades de las acciones 13 y 22 (caminar y correr) que pisa 0x4f37c0 van a un
arreglo propio del modelo 0x20a.

`setup(camino, ángulo, botas=(ítem, nivel))` y `step(zona_segura)` devuelven por paso la posición, el
ángulo, la celda lógica, el índice y el sub-punto del camino, Run y las dos velocidades. Como script,
tres caminos en Lorencia: una recta sin botas, una con botas +5 (corre) y un codo. Es la base de
make_movement_vectors.py de la CL7.4 C.

Uso: emu_walk.py      (con MURET_MAIN_EXE y el Data\\)
"""
import os
import struct
import sys

from unicorn import UC_HOOK_CODE

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402
import emulate_cloth as ec  # noqa: E402

WALL, HEIGHTS = 0x7d0d348, 0x7accca4
SET_PLAYER_WALK, MOVE_PATH, MOVE_POSITION = 0x4f37c0, 0x4ea270, 0x508a80
SET_ACTION, PLAY_SOUND = 0x4ea030, 0x41ed10
PLAYER_MODEL = 0x20a
DIRS = [(-1, -1), (0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0)]


class WalkEmu:
    def __init__(self, world=1):
        e = self.e = ec.ClothEmulator(common.main_exe())
        e.construct_statics()
        _, cells = common.att_cells(world)
        e.uc.mem_write(WALL, struct.pack('<65536H', *cells))
        e.uc.mem_write(HEIGHTS, struct.pack('<65536f', *common.load_heights(world)))
        e.w32(0x6d5dc8, 5)             # la escena 5: deja pedir la altura
        e.w32(0x6c9dd0, world - 1)     # el mapa
        self.actions = []

        def on_set_action(_uc, _a, _s, _d):
            esp = e.uc.reg_read(ec.UC_X86_REG_ESP)
            self.actions.append(e.r32(esp + 8))
            e._return(0)
        e.uc.hook_add(UC_HOOK_CODE, on_set_action, begin=SET_ACTION, end=SET_ACTION)
        e.uc.hook_add(UC_HOOK_CODE, lambda u, a, s, d: e._return(0), begin=PLAY_SOUND, end=PLAY_SOUND)
        self.act = e.alloc(0x10 * 256)                 # Models[0x20a].Actions
        e.w32(ec.MODELS_BASE + PLAYER_MODEL * ec.MODEL_SIZE + 0x30, self.act)
        self.c = c = ec.CHAR_BASE
        e.uc.mem_write(c, b'\0' * 0x1000)
        for off in (0x41c, 0x420):     # abiertos en la tabla de protección: quedan en claro
            rec = e.alloc(5)
            e.w8(rec + 4, 1)
            e.call(ec.MAP_INSERT, args=(rec, c + off), ecx=ec.MAP_OBJECT)

    def setup(self, path, angle=0.0, kind_88=1, boots=None):
        e, c = self.e, self.c
        e.w16(c + 2, PLAYER_MODEL)
        e.w8(c + 0x88, kind_88)
        e.w8(c + 0x1d4, 1)
        for off in (0x258, 0x278, 0x298, 0x2b8, 0x2d8, 0x2f8, 0x318):     # el equipo vacío
            e.w16(c + off, 0xffff)
        if boots:
            e.w16(c + 0x298, boots[0])
            e.w8(c + 0x29a, boots[1])
        e.w8(c + 0x38c, 0)
        e.w8(c + 0x3e2, 0)
        e.w8(c + 0x3e8, 0)
        e.w8(c + 0x3e9, 0)
        e.w8(c + 0x3ea, len(path))
        for i, (x, y) in enumerate(path):
            e.w8(c + 0x3eb + i, x)
            e.w8(c + 0x3fa + i, y)
        x0, y0 = path[0]
        e.wfv(c + 0x10, [(x0 + 0.5) * 100, (y0 + 0.5) * 100, 0.0])
        e.wfv(c + 0x1c, [0.0, 0.0, angle])
        e.w32(c + 0x41c, x0)
        e.w32(c + 0x420, y0)

    def step(self, safe=None):
        e, c = self.e, self.c
        if safe is not None:
            e.w8(c + 0x3e2, safe)
        e.call(SET_PLAYER_WALK, args=(c,))
        arrived = e.call(MOVE_PATH, args=(c, 1)) & 0xff
        if not arrived:
            e.call(MOVE_POSITION, args=(c,))
        return dict(arrived=arrived, pos=e.rfv(c + 0x10, 3), angle=e.rf(c + 0x24),
                    cell=(e.ri32(c + 0x41c), e.ri32(c + 0x420)), cur=e.r8(c + 0x3e8), sub=e.r8(c + 0x3e9),
                    run=e.r8(c + 0x38c), walk_speed=e.rf(self.act + 0x10 * 13 + 4),
                    run_speed=e.rf(self.act + 0x10 * 22 + 4))


def walk(x, y, dirs):
    p = [(x, y)]
    for d in dirs:
        x += DIRS[d][0]
        y += DIRS[d][1]
        p.append((x, y))
    return p


def main():
    w = WalkEmu(1)
    for label, path, kw, safe in [
            ('recta +x, sin botas, fuera de la zona segura', walk(183, 116, [3] * 6), {}, 0),
            ('recta +x con botas +5, fuera de la zona segura', walk(183, 116, [3] * 14),
             {'boots': (0x0e00, 5)}, 0),
            ('codo: +x y después +y, arrancando mirando a 0', walk(183, 116, [3, 3, 5, 5]), {}, 0)]:
        w.setup(path, **kw)
        w.actions = []
        print('==', label, path)
        prev = None
        for n in range(400):
            r = w.step(safe)
            p = r['pos']
            d = ((p[0] - prev[0]) ** 2 + (p[1] - prev[1]) ** 2) ** 0.5 if prev else 0
            prev = p
            if n < 12 or r['arrived'] or n % 10 == 0:
                print(' paso %3d celda %s cur %d sub %d run %2d  pos (%.3f, %.3f, %.2f) ang %.3f  avance %.3f  acc %s' % (
                    n, r['cell'], r['cur'], r['sub'], r['run'], p[0], p[1], p[2], r['angle'], d,
                    w.actions[-1] if w.actions else None))
            if r['arrived']:
                print('  llegó en el paso', n, 'velocidades de acción 13/22:', r['walk_speed'], r['run_speed'])
                break


if __name__ == '__main__':
    main()
