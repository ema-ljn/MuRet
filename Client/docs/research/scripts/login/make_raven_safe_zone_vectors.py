#!/usr/bin/env python3
"""Los vectores del cuervo en la zona segura (Client/tests/vectors/world55/raven_safe_zone.txt): el código real
del paso (0x416a20) con el dueño entrando y saliendo de la zona segura (+0x3e2), en los estados 3 (posado en el
hueso 37 del dueño) y 4 (bajando hacia el hueso 42 con 0x4e9810), y con la mascota en 0x1a04 fuera del mapa
0x37 (el vuelo a 350, Q-C413).

Como make_raven_vectors.py, cada caso arma el dueño (el modelo 0x20a, quieto) y crea el cuervo con el
constructor real (0x4167b0); además le da al dueño sus huesos propios (+0x120: 60 matrices con un giro de 0,6 y
0,8 sobre z y la traslación de una fórmula, que el test repite) y al modelo compartido de su tipo (0x5769ad8 +
0x20a · 0xe0) la escala (+0x68) y la posición (+0x6c) que leen 0x4178d2 y 0x4179a5 (Q-C412). El dueño cambia de
zona en los pasos que dice el caso. De cada paso se guarda lo mismo que en raven.txt, sin la chispa.

Uso: make_raven_safe_zone_vectors.py <salida>  (con MURET_MAIN_EXE)"""
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
OWNER_ANGLES = [3.0, 0.0, 188.0]
RAVEN_BONES = 77
OWNER_BONES = 60

# (semilla, mapa, celdas del dueño, dónde arranca ('cell': en la celda; 'near': junto al dueño, 250 arriba),
# la mascota (+0x318), la escala y la posición del modelo compartido, los pasos y los tramos de la zona segura:
# (desde el paso, +0x3e2)).
CASES = [
    # Entra en la zona desde lejos: baja acelerando, el contador de lejanía lo trae junto al dueño, se posa y
    # sale de la zona (vuelve al 0) y vuelve a entrar.
    (0x5afe, 0x37, (0, 0), 'cell', 0x1c16, 1.25, [10500.0, 14000.0, 560.0], 260, [(0, 1), (180, 0), (210, 1)]),
    # Junto al dueño: se posa enseguida y queda posado con el modelo compartido de otro jugador.
    (0x0a7e, 0, (13, 27), 'near', 0x1c16, 1.0, [10603.0, 14112.0, 580.0], 120, [(0, 1), (90, 0), (100, 1)]),
    # Con la mascota en 0x1a04 en el mapa 0: vuela a 350 como en el 0x37 y después se posa.
    (0x1a04, 0, (0, 0), 'near', 0x1a04, 0.8, [9000.0, 13000.0, 400.0], 200, [(0, 0), (150, 1)]),
    # +0x3e2 distinto de 0 y de 1: no es la zona segura.
    (0x0002, 0x37, (0, 0), 'near', 0x1c16, 1.25, [10500.0, 14000.0, 560.0], 30, [(0, 2)]),
]


def bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def raven_bone_translation(i):
    # La pose de mentira del último dibujo del cuervo, la de make_raven_vectors.py.
    return [f32(1000.0 + 7.0 * i), f32(2000.0 - 3.0 * i), f32(300.0 + 5.0 * i)]


def owner_bone(i):
    # Los huesos propios del dueño: el giro de 0,6 y 0,8 sobre z y la traslación (−40 + 3i, 25 − 2i, 90 + 4i).
    c, s = f32(0.6), f32(0.8)
    return [c, -s, 0.0, f32(-40.0 + 3.0 * i), s, c, 0.0, f32(25.0 - 2.0 * i), 0.0, 0.0, 1.0, f32(90.0 + 4.0 * i)]


def heights_of(a, b):
    return [100.0 + ((i * a + j * b + (i * j) % 97) % 512) * 0.5 for i in range(256) for j in range(256)]


def zone_at(step, zones):
    value = 0
    for start, zone in zones:
        if step >= start:
            value = zone
    return value


def run_case(seed, map_number, cells, start, pet, scale, origin, steps, zones, lines):
    emu = ec.ClothEmulator(EXE)
    uc = emu.uc
    emu.rand_main = ec.Rand({'msvc_state': 99})
    emu.construct_statics()
    emu.set_frame_counter(1234, 0)
    emu.w32(MAP, map_number)
    emu.w32(SCENE, 5)
    sock = emu.r32(0x6c82a0)
    emu.w32(sock + 0xc, 0xffffffff)
    uc.mem_write(HEIGHTS, struct.pack('<65536f', *heights_of(3, 5)))
    uc.mem_write(ATTRIBUTES, b'\0' * 0x20000)
    sounds = []

    def on_sound():
        esp = uc.reg_read(ec.UC_X86_REG_ESP)
        sounds.append(emu.r32(esp + 4))
        emu._return(0)
    uc.hook_add(ec.UC_HOOK_CODE, lambda *_: on_sound(), begin=SOUND, end=SOUND)

    m = ec.MODELS_BASE + 0xc0 * ec.MODEL_SIZE
    acts = emu.alloc(4 * 0x10)
    uc.mem_write(acts, b'\0' * 0x40)
    for k, (keys, lock) in enumerate([(6, 0), (4, 0), (4, 1), (4, 0)]):
        emu.w16(acts + k * 0x10 + 8, keys)
        emu.w8(acts + k * 0x10 + 10, lock)
    emu.w16(m + 0x22, RAVEN_BONES)
    emu.w16(m + 0x26, 4)
    emu.w32(m + 0x30, acts)
    mo = ec.MODELS_BASE + 0x20a * ec.MODEL_SIZE
    emu.w16(mo + 0x22, OWNER_BONES)
    emu.w16(mo + 0x26, 205)
    emu.wf(mo + 0x68, scale)
    emu.wfv(mo + 0x6c, origin)
    owner = ec.CHAR_BASE
    uc.mem_write(owner, b'\0' * 0x438)
    emu.w8(owner, 1)
    emu.w16(owner + 2, 0x20a)
    emu.wf(owner + 0xc, 1.6)
    emu.wfv(owner + 0x10, OWNER_POSITION)
    emu.wfv(owner + 0x1c, OWNER_ANGLES)
    flat = []
    for i in range(OWNER_BONES):
        flat += owner_bone(i)
    emu.wfv(ec.BONES_BASE, flat)
    emu.w32(owner + 0x120, ec.BONES_BASE)
    emu.w16(owner + 0x318, pet)
    emu.w8(owner + 0x3e2, 0)
    emu.w32(owner + 0x41c, cells[0])
    emu.w32(owner + 0x420, cells[1])

    rand = ec.Rand({'msvc_state': seed})
    raven = emu.alloc(0x460)
    emu.call(CTOR, args=(owner,), ecx=raven, rand=rand)
    o = raven + 0xc
    lines.append('case %08x %x %d %d %s %04x %s %s %d %s' % (
        seed, map_number, cells[0], cells[1], start, pet, bits(scale), ' '.join(bits(v) for v in origin), steps,
        ' '.join('%d:%d' % zone for zone in zones)))
    lines.append('ctor %08x %s %s %s %d %d %s %08x' % (
        rand.state, ' '.join(bits(v) for v in emu.rfv(o + 0x10, 3)), ' '.join(bits(v) for v in emu.rfv(o + 0x1c, 3)),
        bits(emu.rf(o + 0xd8)), emu.r8(o + 0x110), emu.r8(o + 0x111), bits(emu.rf(o + 0x114)), emu.r32(o + 0x60)))
    if start == 'near':
        emu.wfv(o + 0x10, [OWNER_POSITION[0], OWNER_POSITION[1], f32(OWNER_POSITION[2] + 250.0)])
    emu.w32(owner + 0x78, 0)
    emu.w32(COUNTER, 0x12345677)
    bones = emu.alloc(RAVEN_BONES * 0x30)
    flat = []
    for i in range(RAVEN_BONES):
        t = raven_bone_translation(i)
        flat += [1.0, 0.0, 0.0, t[0], 0.0, 1.0, 0.0, t[1], 0.0, 0.0, 1.0, t[2]]
    emu.wfv(bones, flat)
    emu.w32(o + 0x120, bones)
    emu.w32(raven, 0x6b8904)
    emu.w32(raven + 4, owner)
    emu.w32(owner + 0x430, raven)
    for step in range(steps):
        sounds.clear()
        emu.w8(owner + 0x3e2, zone_at(step, zones))
        uc.mem_write(PARTICLES, b'\0' * (3000 * PARTICLE_SIZE))
        emu.call(STEP, ecx=raven, rand=rand)
        lines.append('step %08x %s %s %s %s %d %d %s %d %s' % (
            rand.state, ' '.join(bits(v) for v in emu.rfv(o + 0x10, 3)),
            ' '.join(bits(v) for v in emu.rfv(o + 0x1c, 3)), ' '.join(bits(v) for v in emu.rfv(o + 0xcc, 3)),
            bits(emu.rf(o + 0xd8)), emu.r8(o + 0x110), emu.r8(o + 0x111), bits(emu.rf(o + 0x114)),
            emu.ri32(o + 0x60), ('%x' % sounds[0]) if sounds else '-'))


def main():
    out = sys.argv[1]
    lines = ['raven-safe-zone 1']
    for case in CASES:
        run_case(*case, lines)
    lines.append('end')
    with open(out, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines) + '\n')
    print('casos', len(CASES), '->', out)


if __name__ == '__main__':
    main()
