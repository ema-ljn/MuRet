#!/usr/bin/env python3
"""Corre el paso real del cuervo (0x416a20, vtable 0x6b8904 + 4) del main.exe sobre unicorn, con el
dueño quieto del login, y vuelca el estado del objeto paso a paso. Reusa el emulador de la tela del
repo (solo lectura). El estado inicial es el que deja 0x415be0 + 0x4167b0 (escrito a mano)."""
import os
import json
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import emulate_cloth as ec  # noqa: E402

EXE = os.environ['MURET_MAIN_EXE']
MOVE_PET = 0x416a20
SOUND = 0x41ed10
PARTICLE = 0x53fe70

RAND_SITES = {
    0x417b87: 'chispa: rand() % 66 (hueso)',
    0x417c05: 'giro: rand() % 15',
    0x417d25: 'sorteo: rand() % div',
    0x417d46: 'lejos: rand() & 63',
    0x417d6a: 'cerca: rand() & 7',
    0x417d8d: 'cerca: rand() % 60 (yaw)',
    0x417dc5: 'dirZ: rand() & 63',
    0x417e43: 'grito: rand() % 100',
}


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 12345
    steps = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    start = sys.argv[3] if len(sys.argv) > 3 else 'cell0'
    garbage_v = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0x12345677
    emu = ec.ClothEmulator(EXE)
    uc = emu.uc
    rand = ec.Rand({'msvc_state': seed})
    emu.rand_main = rand
    events = []

    def on_sound():
        esp = uc.reg_read(ec.UC_X86_REG_ESP)
        events.append({'sound': hex(emu.r32(esp + 4)), 'obj': hex(emu.r32(esp + 8))})
        emu._return(0)
    uc.hook_add(ec.UC_HOOK_CODE, lambda *_: on_sound(), begin=SOUND, end=SOUND)

    def on_particle():
        esp = uc.reg_read(ec.UC_X86_REG_ESP)
        a = [emu.r32(esp + 4 + 4 * k) for k in range(7)]
        events.append({'particle': hex(a[0]), 'pos': emu.rfv(a[1], 3), 'angles': emu.rfv(a[2], 3),
                       'color': emu.rfv(a[3], 3), 'sub': a[4],
                       'scale': struct.unpack('<f', struct.pack('<I', a[5]))[0]})
    uc.hook_add(ec.UC_HOOK_CODE, lambda *_: on_particle(), begin=PARTICLE, end=PARTICLE)

    # el mapa del login, el contador protegido con basura
    emu.w32(0x6c9dd0, 0x37)
    emu.w32(0x7d3a0f0, garbage_v)
    emu.w32(0x6d5dc8, 2)  # la escena

    # modelos: 0xc0 (DarkSpirit: 77 huesos, 4 acciones de 6, 4, 4 (lock) y 4 keys) y 0x20a
    m = ec.MODELS_BASE + 0xc0 * ec.MODEL_SIZE
    acts = emu.alloc(4 * 0x10)
    uc.mem_write(acts, b'\0' * 0x40)
    for k, (keys, lock) in enumerate([(6, 0), (4, 0), (4, 1), (4, 0)]):
        emu.w8(acts + k * 0x10, 0)
        emu.w16(acts + k * 0x10 + 8, keys)
        emu.w8(acts + k * 0x10 + 10, lock)
    emu.w16(m + 0x22, 77)
    emu.w16(m + 0x26, 4)
    emu.w32(m + 0x30, acts)
    mo = ec.MODELS_BASE + 0x20a * ec.MODEL_SIZE
    emu.w16(mo + 0x22, 60)
    emu.w16(mo + 0x26, 205)

    # el dueño: el personaje del login
    owner = ec.CHAR_BASE
    uc.mem_write(owner, b'\0' * 0x438)
    emu.w8(owner, 1)
    emu.w16(owner + 2, 0x20a)
    emu.wf(owner + 0xc, 1.6)
    emu.wfv(owner + 0x10, [10603.0, 14112.0, 580.0])
    emu.wfv(owner + 0x1c, [0.0, 0.0, 188.0])
    emu.w32(owner + 0x78, 0)
    emu.w32(owner + 0x120, ec.BONES_BASE)
    emu.w16(owner + 0x318, 0x1c16)
    emu.w8(owner + 0x3e2, 0)

    # el cuervo: 0x460 bytes, el objeto en +0xc
    pet = emu.alloc(0x460)
    uc.mem_write(pet, b'\0' * 0x460)
    o = pet + 0xc
    emu.w32(pet, 0x6b8904)
    emu.w32(pet + 4, owner)
    emu.w32(owner + 0x430, pet)
    emu.w8(o + 0, 1)
    emu.w16(o + 2, 0xc0)
    emu.wf(o + 0xc, 0.7)
    if start == 'cell0':
        pos = [50.0, 50.0, 0.0 + 300.0]
    else:
        pos = [10603.0, 14112.0, 580.0 + 250.0]
    emu.wfv(o + 0x10, pos)
    emu.wf(o + 0xd8, 1.0)
    emu.wf(o + 0xe4, 13.0)
    emu.w8(o + 0xe8, 1)
    emu.w8(o + 0x11c, 1)
    emu.w32(o + 0x58, 0xffffffff)
    emu.w32(o + 0x64, 0xffffffff)
    emu.wf(o + 0x68, 1.0)
    emu.wf(o + 0x170, 1.0)
    emu.wf(o + 0x174, 1.0)
    emu.w16(o + 0x3a4, 0xffff)  # objetivo (sin iniciar en el original: no se lee en los estados 0/1)
    bones = emu.alloc(77 * 0x30)
    flat = []
    for i in range(77):
        flat += [1, 0, 0, float(i), 0, 1, 0, 0.0, 0, 0, 1, 0.0]
    emu.wfv(bones, flat)
    emu.w32(o + 0x120, bones)

    out = []
    for step in range(steps):
        events.clear()
        start_rand = rand.index
        emu.events = []
        # el mover de partículas (0x5446e0) las mata a los 10 pasos: acá se vacía el arreglo
        uc.mem_write(0x743a198, b'\0' * (3000 * 0x78))
        emu.call(MOVE_PET, ecx=pet, rand=rand)
        rands = [(e['rand'], RAND_SITES.get(int(e['from'], 16), e['from'])) for e in emu.events if 'rand' in e]
        out.append({
            'step': step + 1,
            'pos': emu.rfv(o + 0x10, 3), 'ang': emu.rfv(o + 0x1c, 3), 'dir': emu.rfv(o + 0xcc, 3),
            'vel_d8': emu.rf(o + 0xd8), 'state': emu.r8(o + 0x110), 'action': emu.r8(o + 0x111),
            'frame': emu.rf(o + 0x114), 'counter': emu.ri32(o + 0x60),
            'rands': rands, 'events': list(events)})
    json.dump(out, sys.stdout)


if __name__ == '__main__':
    main()
