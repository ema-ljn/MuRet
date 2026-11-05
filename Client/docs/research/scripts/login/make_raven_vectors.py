#!/usr/bin/env python3
"""Los vectores del cuervo (CL7.6 C1, Client/tests/vectors/world55/raven.txt): el código real del
constructor (0x4167b0, con 0x415be0 y la altura del terreno de 0x5f93c0) y del paso (0x416a20), con la
chispa que pide a 0x53fe70 (el 0x5c6, subtipo 5) y el grito (0x41ed10).

Cada caso arma el dueño (el personaje del login, el modelo 0x20a, quieto), le pone las celdas y llama al
constructor con el rand() de la CRT desde una semilla; después corre pasos del cuervo con una pose de
"último dibujo" de mentira en sus huesos propios (+0x120: 77 matrices con la traslación de una fórmula,
que el test repite). De cada paso se guarda el estado del rand(), la posición, los ángulos, la dirección
(+0xcc), la velocidad (+0xd8), el estado, la acción, el frame, el contador (+0x60), el grito y, si nació,
la chispa entera (el lugar 0 de 0x743a198, que se vacía antes de cada paso), en hexadecimal.

El mapa de alturas y los atributos salen de una fórmula entera, como en make_leaf_vectors.py.

Uso: make_raven_vectors.py <salida>  (con MURET_MAIN_EXE)"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import emulate_cloth as ec  # noqa: E402

EXE = os.environ['MURET_MAIN_EXE']
CTOR, STEP, SOUND = 0x4167b0, 0x416a20, 0x41ed10
PARTICLES, PARTICLE_SIZE = 0x743a198, 0x78
HEIGHTS, ATTRIBUTES = 0x7accca4, 0x7d0d348
MAP, SCENE, COUNTER = 0x6c9dd0, 0x6d5dc8, 0x7d3a0f0
OWNER_POSITION = [10603.0, 14112.0, 580.0]
BONES = 77

# (semilla, celdas del dueño, dónde arranca ('cell': en la celda; 'near': junto al dueño, 250 arriba;
# 'yaw180': en la celda con el rumbo en 180), banderas del dueño (+0x78), el contador que lee el grito,
# pasos, la fórmula de las alturas (a, b))
CASES = [
    (0x1234, (0, 0), 'cell', 0, 0x12345677, 400, (3, 5)),
    (0xbeef, (0, 0), 'cell', 0, 0x12345677, 400, (7, 11)),
    (0x5eed, (13, 27), 'cell', 0, 0x12345677, 300, (5, 3)),
    (0x77, (0, 0), 'near', 0, 0x12345678 - (0x12345678 % 60), 400, (3, 5)),
    (0x99, (0, 0), 'near', 0x4000, 0x12345677, 120, (3, 5)),
    (0x42, (0, 0), 'near', 0x1000, 0x12345677, 20, (3, 5)),
    # El rumbo justo en 180 y lejos del dueño: el primer paso gira con TurnAngle desde a = 180.
    (0x180, (0, 0), 'yaw180', 0, 0x12345677, 40, (3, 5)),
]


def bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def bone_translation(i):
    # La pose de mentira del último dibujo: el test repite esta fórmula.
    return [f32(1000.0 + 7.0 * i), f32(2000.0 - 3.0 * i), f32(300.0 + 5.0 * i)]


def heights_of(a, b):
    return [100.0 + ((i * a + j * b + (i * j) % 97) % 512) * 0.5 for i in range(256) for j in range(256)]


def run_case(seed, cells, start, owner_flags, counter, steps, formula, lines):
    emu = ec.ClothEmulator(EXE)
    uc = emu.uc
    emu.rand_main = ec.Rand({'msvc_state': 99})
    emu.construct_statics()
    emu.set_frame_counter(1234, 0)
    emu.w32(MAP, 0x37)
    emu.w32(SCENE, 2)
    sock = emu.r32(0x6c82a0)
    emu.w32(sock + 0xc, 0xffffffff)
    uc.mem_write(HEIGHTS, struct.pack('<65536f', *heights_of(*formula)))
    uc.mem_write(ATTRIBUTES, b'\0' * 0x20000)
    sounds = []

    def on_sound():
        esp = uc.reg_read(ec.UC_X86_REG_ESP)
        sounds.append(emu.r32(esp + 4))
        emu._return(0)
    uc.hook_add(ec.UC_HOOK_CODE, lambda *_: on_sound(), begin=SOUND, end=SOUND)

    # El cuervo (DarkSpirit: 77 huesos, 4 acciones de 6, 4, 4 con lock y 4 keys) y el dueño (0x20a).
    m = ec.MODELS_BASE + 0xc0 * ec.MODEL_SIZE
    acts = emu.alloc(4 * 0x10)
    uc.mem_write(acts, b'\0' * 0x40)
    for k, (keys, lock) in enumerate([(6, 0), (4, 0), (4, 1), (4, 0)]):
        emu.w16(acts + k * 0x10 + 8, keys)
        emu.w8(acts + k * 0x10 + 10, lock)
    emu.w16(m + 0x22, BONES)
    emu.w16(m + 0x26, 4)
    emu.w32(m + 0x30, acts)
    mo = ec.MODELS_BASE + 0x20a * ec.MODEL_SIZE
    emu.w16(mo + 0x22, 60)
    emu.w16(mo + 0x26, 205)
    owner = ec.CHAR_BASE
    uc.mem_write(owner, b'\0' * 0x438)
    emu.w8(owner, 1)
    emu.w16(owner + 2, 0x20a)
    emu.wf(owner + 0xc, 1.6)
    emu.wfv(owner + 0x10, OWNER_POSITION)
    emu.wfv(owner + 0x1c, [0.0, 0.0, 188.0])
    emu.w32(owner + 0x120, ec.BONES_BASE)
    emu.w16(owner + 0x318, 0x1c16)
    emu.w8(owner + 0x3e2, 0)
    emu.w32(owner + 0x41c, cells[0])
    emu.w32(owner + 0x420, cells[1])

    rand = ec.Rand({'msvc_state': seed})
    pet = emu.alloc(0x460)
    emu.call(CTOR, args=(owner,), ecx=pet, rand=rand)
    o = pet + 0xc
    lines.append('case %08x %d %d %s %08x %08x %d %d %d' % (seed, cells[0], cells[1], start, owner_flags, counter,
                                                            formula[0], formula[1], steps))
    lines.append('ctor %08x %s %s %s %s %s %d %d %s %08x' % (
        rand.state, ' '.join(bits(v) for v in emu.rfv(o + 0x10, 3)), bits(emu.rf(o + 0xc)),
        ' '.join(bits(v) for v in emu.rfv(o + 0xf4, 3)), bits(emu.rf(o + 0xd8)), bits(emu.rf(o + 0x174)),
        emu.r8(o + 0x110), emu.r8(o + 0x111), bits(emu.rf(o + 0x114)), emu.r32(o + 0x60)))
    if start == 'near':
        emu.wfv(o + 0x10, [OWNER_POSITION[0], OWNER_POSITION[1], f32(OWNER_POSITION[2] + 250.0)])
    if start == 'yaw180':
        emu.wf(o + 0x24, 180.0)
    emu.w32(owner + 0x78, owner_flags)
    emu.w32(COUNTER, counter)
    bones = emu.alloc(BONES * 0x30)
    flat = []
    for i in range(BONES):
        t = bone_translation(i)
        flat += [1.0, 0.0, 0.0, t[0], 0.0, 1.0, 0.0, t[1], 0.0, 0.0, 1.0, t[2]]
    emu.wfv(bones, flat)
    emu.w32(o + 0x120, bones)
    emu.w32(pet, 0x6b8904)
    emu.w32(pet + 4, owner)
    emu.w32(owner + 0x430, pet)
    for _ in range(steps):
        sounds.clear()
        uc.mem_write(PARTICLES, b'\0' * (3000 * PARTICLE_SIZE))
        emu.call(STEP, ecx=pet, rand=rand)
        lines.append('step %08x %s %s %s %s %d %d %s %d %s' % (
            rand.state, ' '.join(bits(v) for v in emu.rfv(o + 0x10, 3)),
            ' '.join(bits(v) for v in emu.rfv(o + 0x1c, 3)), ' '.join(bits(v) for v in emu.rfv(o + 0xcc, 3)),
            bits(emu.rf(o + 0xd8)), emu.r8(o + 0x110), emu.r8(o + 0x111), bits(emu.rf(o + 0x114)),
            emu.ri32(o + 0x60), ('%x' % sounds[0]) if sounds else '-'))
        raw = bytes(uc.mem_read(PARTICLES, PARTICLE_SIZE))
        if raw[0]:
            lines.append('spark ' + ' '.join('%x' % w for w in struct.unpack('<30I', raw)))


def main():
    out = sys.argv[1]
    lines = ['raven 1']
    for case in CASES:
        run_case(*case, lines)
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('casos', len(CASES), '->', out)


if __name__ == '__main__':
    main()
