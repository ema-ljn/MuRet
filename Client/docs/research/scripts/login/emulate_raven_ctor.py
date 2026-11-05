import os
import struct, sys, json
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import emulate_cloth as ec
EXE = os.environ['MURET_MAIN_EXE']
emu = ec.ClothEmulator(EXE)
uc = emu.uc
emu.rand_main = ec.Rand({'msvc_state': 99})
emu.construct_statics()
emu.set_frame_counter(1234, 0)
emu.w32(0x6c9dd0, 0x37)
# el socket: PTR_DAT_006c82a0 -> objeto; +0xc = -1 (sin socket: no manda el A9)
sock = emu.r32(0x6c82a0)
print('socket obj', hex(sock))
emu.w32(sock + 0xc, 0xffffffff)
m = ec.MODELS_BASE + 0xc0 * ec.MODEL_SIZE
emu.w16(m + 0x22, 77); emu.w16(m + 0x26, 4)
owner = ec.CHAR_BASE
uc.mem_write(owner, b'\0' * 0x438)
emu.w8(owner, 1); emu.w16(owner + 2, 0x20a)
emu.wfv(owner + 0x10, [10603.0, 14112.0, 580.0])
emu.w32(owner + 0x41c, int(sys.argv[1]) if len(sys.argv) > 1 else 0)
emu.w32(owner + 0x420, int(sys.argv[2]) if len(sys.argv) > 2 else 0)
pet = emu.alloc(0x460)
emu.events = []
ret = emu.call(0x4167b0, args=(owner,), ecx=pet, rand=emu.rand_main)
o = pet + 0xc
print('ret', hex(ret), 'pet', hex(pet))
print('rands', [(e['rand'], e['from']) for e in emu.events if 'rand' in e])
print('vtable', hex(emu.r32(pet)), 'owner', hex(emu.r32(pet + 4)), '+8', hex(emu.r32(pet + 8)))
print('live', emu.r8(o), 'type', hex(emu.r16(o + 2) & 0xffff), 'scale', emu.rf(o + 0xc))
print('pos', emu.rfv(o + 0x10, 3), 'ang', emu.rfv(o + 0x1c, 3), 'head', emu.rfv(o + 0x28, 3))
print('dir', emu.rfv(o + 0xcc, 3), 'vel', emu.rf(o + 0xd8), '+0xdc', emu.rf(o + 0xdc), '+0xe4', emu.rf(o + 0xe4))
print('vlight', emu.r8(o + 0xe8), 'light', emu.rfv(o + 0xf4, 3), 'state', emu.r8(o + 0x110), 'action', emu.r8(o + 0x111))
print('alpha', emu.rf(o + 0x174), 'target', emu.rf(o + 0x170), 'enableBones', emu.r8(o + 0x11c), 'blend', hex(emu.r32(o + 0x58)), hex(emu.r32(o + 0x64)), emu.rf(o + 0x68))
print('bbox', emu.rfv(o + 0x124, 6), 'visible', emu.r8(o + 0x16c), '+0x16d', emu.r8(o + 0x16d))
print('cells', emu.r32(o + 0x41c), emu.r32(o + 0x420), 'level(+0x1d6)', emu.r16(o + 0x1d6), 'class', emu.r8(o + 0x1d4), 'pet+0x444', emu.r32(pet + 0x444), 'pet+0x45c', emu.r8(pet + 0x45c), 'target +0x3a4', hex(emu.r16(o + 0x3a4) & 0xffff))
print('owner cells after', hex(emu.r32(owner + 0x41c)), hex(emu.r32(owner + 0x420)))
