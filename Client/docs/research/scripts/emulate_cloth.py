#!/usr/bin/env python3
r"""Oráculo de la tela (la clase de 0x6b8cd8) del main.exe de c32bbd5f, sobre unicorn.

Corre el código **real** del binario: el bloque de 0x50ba30 que arma, reinicia, simula y dibuja
las telas de un personaje (0x50d58b..0x50db57, con todo lo que llama: 0x454a50, 0x455cd0,
0x455190 → 0x4551d0 → 0x4552a0 / 0x4555d0 / 0x455770, 0x455930, los wrappers de GL de 0x620xxx,
0x5491e0, 0x4fb1a0 y la tabla de protección de 0x57630c8), y antes de cada frame, si se pide, el
paseo del viento base de 0x637700 (5 llamadas reales a 0x456b10 sobre el administrador vacío de
0x7d368f8). Ver contract.md.

Lo único que no es del binario:
  - `operator new` (0x69f403) y `free` (0x69eff0): un montón propio (la memoria nueva sale con
    0xCD, para ver si algo lee lo que no escribió);
  - `_rand` (0x69fd70): devuelve la lista dada o la fórmula de 0x69fd70 desde una semilla dada;
  - `atexit` (0x69f8e4) y el log de error de la tabla llena (0x41fa40): no hacen nada;
  - cada import (opengl32 y los demás): un stub que registra la llamada y vuelve (los de GL son
    stdcall). Un import que no sea de GL corta la emulación con error, salvo que el que usa el
    emulador le dé su propio stub en `stub_overrides` (por nombre: `{'send': fn}`; `fn(esp)` lee
    los argumentos y vuelve con `_return`, como `send` en game/walk/emu_send.py);
  - los huesos, el objeto, el modelo y los globales: los da la entrada.

Uso:
    python emulate_cloth.py --example > example.json          # el Dark Lord del login (3 telas)
    python emulate_cloth.py entrada.json > salida.json
    python emulate_cloth.py --example --print-input > entrada_del_ejemplo.json

El formato de la entrada está en la docstring de `run` y en contract.md § 13. Con "direct" en la
entrada, en vez del bloque de 0x50ba30 corre las telas que se le pidan (las mismas funciones reales).
cloth_reference.py compara una salida con la reimplementación del contrato; run_cases.py corre los
siete casos de prueba.
"""

import argparse
import json
import os
import struct
import sys

import pefile
from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBX, UC_X86_REG_ECX,
                               UC_X86_REG_ESI, UC_X86_REG_EDI, UC_X86_REG_EBP, UC_X86_REG_ESP,
                               UC_X86_REG_EIP, UC_X86_REG_FS, UC_X86_REG_GDTR, UC_X86_REG_FPCW,
                               UC_X86_REG_SS, UC_X86_REG_DS, UC_X86_REG_ES)

# El main.exe c32bbd5f (client-research/mainexe-vmxp-c32bbd5f/original/main.exe en
# mu-server-repack-research-102i; el de analysis/mainexe/originals/ es el otro build, 2f16a9c6), con --exe
# o en MURET_MAIN_EXE; Player.bmd, el de la Data del repo.
DEFAULT_EXE = os.environ.get('MURET_MAIN_EXE', 'main.exe')
PLAYER_BMD_CANDIDATES = [
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..', 'assets', 'Data', 'player',
                 'Player.bmd'),
]

# --- direcciones del binario -------------------------------------------------------------------
BLOCK_START = 0x50d58b          # `MOV AL,[EBP+0xf]`: if (bVar23) ...
BLOCK_END = 0x50db57            # después del lazo de las telas (sigue el rand() de la luz)
OP_NEW, OP_FREE, RAND, ATEXIT, MAP_ERROR = 0x69f403, 0x69eff0, 0x69fd70, 0x69f8e4, 0x41fa40
MAP_CTOR, MAP_OBJECT = 0x447f80, 0x57630c8          # la tabla de protección (0x4acef0 la construye)
MANAGER_CTOR, MANAGER = 0x456990, 0x7d368f8         # el administrador de física (0x622ad0)
WIND_WALK = 0x456b10                                # lo llama 0x637700 5 veces por paso de lógica
MAP_INSERT, ENCRYPT = 0x40ad90, 0x412890            # para armar el registro de 0x7d3a0f0
FRAME_COUNTER = 0x7d3a0f0                           # el contador protegido que lee 0x4552a0
MODELS_POINTER = 0x5769ad8
MODEL_SIZE = 0xe0
ANGLE_QUATERNION, QUATERNION_MATRIX, CONCAT_TRANSFORMS = 0x5fcd20, 0x5fcdb0, 0x5fca90

AFTER_STEP = 0x4551ac           # en 0x455190, después de cada 0x4551d0: EDI = tela, ESI = iteración
INIT_ENTRY, SPHERE_ENTRY = 0x454a50, 0x455cd0
RENDER_ENTRY, SPRITE_ENTRY, DESTROY_ENTRY = 0x455930, 0x5491e0, 0x4fb1a0

# Globales que la entrada puede poner (todos los que el bloque lee además del objeto y el modelo).
GLOBALS = {
    'map_6c9dd0': (0x6c9dd0, 'i'),             # el mapa (0x37: el viento de 4551d0 por rand() % 40)
    'wind_base_7245f0': (0x7245f0, 'f'),       # el viento base; lo pasea 0x456b10
    'wind_z_7245fc': (0x7245fc, 'f'),          # la z del viento: nadie la escribe (0 en la .data)
    'world_time_5767b88': (0x5767b88, 'f'),    # el reloj T (el brillo de 0x40000000)
    'gl_blend_7d366dc': (0x7d366dc, 'i'),      # la caché de la mezcla de 0x620xxx
    'gl_alpha_test_7d366cd': (0x7d366cd, 'b'),
    'gl_texture_7d366d5': (0x7d366d5, 'b'),
    'gl_cull_7d366cc': (0x7d366cc, 'b'),
    'gl_depth_mask_7d36898': (0x7d36898, 'b'),
    'gl_fog_7d368a0': (0x7d368a0, 'b'),
    'bound_texture_6d5d68': (0x6d5d68, 'i'),   # el slot atado (0x620780 no repite el bind)
}
TEXTURE_TABLE = 0x7d3a1c4      # + slot · 0x38: el nombre de GL del slot (0x620780)

# --- memoria propia ---------------------------------------------------------------------------
GDT_BASE, TEB_BASE = 0x00010000, 0x00011000
STUB_BASE = 0x00020000
SENTINEL = 0x00030000
STACK_BASE, STACK_SIZE = 0x00100000, 0x00100000
HEAP_BASE, HEAP_SIZE = 0x10000000, 0x04000000
CHAR_BASE, CHAR_SIZE = 0x0A000000, 0x2000
BONES_BASE, BONES_SIZE = 0x0A010000, 0x10000
MODELS_BASE = 0x0B000000
MODELS_SIZE = 0x489d * MODEL_SIZE

# Los imports de GL que el bloque llama, con sus argumentos (stdcall) y cuáles son float.
GL_SIGNATURES = {
    'glBegin': 'i', 'glEnd': '', 'glVertex3f': 'fff', 'glTexCoord2f': 'ff', 'glColor3f': 'fff',
    'glColor3fv': 'p3', 'glColor4f': 'ffff', 'glVertex3fv': 'p3', 'glEnable': 'i', 'glDisable': 'i',
    'glBlendFunc': 'ii', 'glBindTexture': 'ii', 'glDepthMask': 'i', 'glAlphaFunc': 'if', 'glDepthFunc': 'i',
    'glCullFace': 'i', 'glPushMatrix': '', 'glPopMatrix': '',
}

# Quién llama a rand(), por la dirección de vuelta.
RAND_USES = {
    0x4551f4: '0x4551d0: viento de la tela en el mapa 0x37 ((rand() % 40 + 10) * 0.02)',
    0x45521d: '0x4551d0: viento de la tela con (bandera & 0x300) == 0x200 ((rand() % [+0x48] + [+0x49]) * 0.01)',
    0x455505: '0x4552a0: protección de 0x7d3a0f0 (un byte de basura por cada uno de los 4)',
    0x456b1e: '0x456b10: paseo del viento base (rand() % 200 - 100) * 0.001',
    0x447fc7: '0x447f80: constructor de la tabla de protección (bloque de relleno)',
}


def f32(x):
    """El float más cercano (como un FSTP float)."""
    return struct.unpack('<f', struct.pack('<f', x))[0]


def fbits(x):
    return '%08x' % struct.unpack('<I', struct.pack('<f', x))[0]


def page_up(n):
    return (n + 0xfff) & ~0xfff


class Rand:
    """La fuente de rand(): una lista fija, o la fórmula de 0x69fd70 desde el estado dado."""

    def __init__(self, spec):
        self.values = None
        self.state = None
        self.index = 0
        if isinstance(spec, list):
            self.values = list(spec)
        elif isinstance(spec, dict) and 'msvc_state' in spec:
            self.state = spec['msvc_state'] & 0xffffffff
        else:
            raise ValueError('rand: una lista de enteros o {"msvc_state": n}')

    def next(self):
        if self.values is not None:
            if self.index >= len(self.values):
                raise RuntimeError('rand(): se acabó la lista dada (%d valores)' % len(self.values))
            value = self.values[self.index]
        else:
            # 0x69fd70: holdrand = holdrand · 0x343fd + 0x269ec3; (holdrand >> 16) & 0x7fff
            self.state = (self.state * 0x343fd + 0x269ec3) & 0xffffffff
            value = (self.state >> 16) & 0x7fff
        self.index += 1
        return value

    def describe(self):
        if self.values is not None:
            return {'consumed': self.index, 'given': len(self.values)}
        return {'consumed': self.index, 'msvc_state_after': self.state}


class ClothEmulator:
    def __init__(self, exe_path, fpu_control_word=0x27f, stub_overrides=None):
        self.pe = pefile.PE(exe_path)
        # Los imports que el que usa el emulador atiende él: nombre → fn(esp), que vuelve con _return.
        self.stub_overrides = dict(stub_overrides or {})
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.heap_next = HEAP_BASE
        self.events = []
        self.rand_main = None
        self.rand_setup = Rand({'msvc_state': 1})
        self.rand_current = None
        self.phase = 'setup'
        self.stubs = {}
        self.fpu_control_word = fpu_control_word
        self.error = None
        self.current_cloth = None
        self._map_image()
        self._map_own()
        self._install_stubs()
        self._install_hooks()

    # --- memoria -------------------------------------------------------------------------------
    def _map_image(self):
        base = self.pe.OPTIONAL_HEADER.ImageBase
        self.uc.mem_map(base, 0x1000)
        self.uc.mem_write(base, bytes(self.pe.header[:0x1000]))
        for section in self.pe.sections:
            start = base + section.VirtualAddress
            size = page_up(max(section.Misc_VirtualSize, section.SizeOfRawData))
            self.uc.mem_map(start, size)
            data = section.get_data()[:max(section.Misc_VirtualSize, section.SizeOfRawData)]
            self.uc.mem_write(start, data)

    def _map_own(self):
        uc = self.uc
        for base, size in [(GDT_BASE, 0x1000), (TEB_BASE, 0x1000), (STUB_BASE, 0x4000), (SENTINEL, 0x1000),
                           (STACK_BASE, STACK_SIZE), (HEAP_BASE, HEAP_SIZE), (CHAR_BASE, CHAR_SIZE),
                           (BONES_BASE, BONES_SIZE), (MODELS_BASE, page_up(MODELS_SIZE))]:
            uc.mem_map(base, size)
        uc.mem_write(SENTINEL, b'\xf4')
        uc.mem_write(STUB_BASE, b'\xc3' * 0x4000)
        # FS con una entrada de la GDT: el TEB con la cadena de SEH vacía.
        uc.mem_write(TEB_BASE, struct.pack('<I', 0xffffffff))
        uc.mem_write(TEB_BASE + 0x18, struct.pack('<I', TEB_BASE))

        def entry(base, limit, access, flags):
            e = limit & 0xffff
            e |= (base & 0xffffff) << 16
            e |= (access & 0xff) << 40
            e |= ((limit >> 16) & 0xf) << 48
            e |= (flags & 0xf) << 52
            e |= ((base >> 24) & 0xff) << 56
            return struct.pack('<Q', e)
        gdt = (b'\0' * 8 + entry(0, 0xfffff, 0x80 | 0x10 | 0x0a, 0xc) + entry(0, 0xfffff, 0x80 | 0x10 | 0x02, 0xc) +
               entry(TEB_BASE, 0xfff, 0x80 | 0x10 | 0x02 | 0x60, 0x4))
        uc.mem_write(GDT_BASE, gdt)
        uc.reg_write(UC_X86_REG_GDTR, (0, GDT_BASE, len(gdt) - 1, 0))
        self._load_segments()
        uc.mem_write(MODELS_POINTER, struct.pack('<I', MODELS_BASE))

    def _install_stubs(self):
        index = 0
        for dll in self.pe.DIRECTORY_ENTRY_IMPORT:
            for imp in dll.imports:
                name = imp.name.decode() if imp.name else 'ord%d' % imp.ordinal
                stub = STUB_BASE + index * 8
                self.stubs[stub] = (dll.dll.decode(), name)
                self.uc.mem_write(imp.address, struct.pack('<I', stub))
                index += 1

    # --- acceso --------------------------------------------------------------------------------
    def r32(self, a):
        return struct.unpack('<I', bytes(self.uc.mem_read(a, 4)))[0]

    def ri32(self, a):
        return struct.unpack('<i', bytes(self.uc.mem_read(a, 4)))[0]

    def r16(self, a):
        return struct.unpack('<h', bytes(self.uc.mem_read(a, 2)))[0]

    def r8(self, a):
        return bytes(self.uc.mem_read(a, 1))[0]

    def rf(self, a):
        return struct.unpack('<f', bytes(self.uc.mem_read(a, 4)))[0]

    def rfv(self, a, n):
        return list(struct.unpack('<%df' % n, bytes(self.uc.mem_read(a, 4 * n))))

    def w32(self, a, v):
        self.uc.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def w16(self, a, v):
        self.uc.mem_write(a, struct.pack('<H', v & 0xffff))

    def w8(self, a, v):
        self.uc.mem_write(a, struct.pack('<B', v & 0xff))

    def wf(self, a, v):
        self.uc.mem_write(a, struct.pack('<f', v))

    def wfv(self, a, vs):
        self.uc.mem_write(a, struct.pack('<%df' % len(vs), *vs))

    def alloc(self, size):
        size = max(int(size), 1)
        address = self.heap_next
        self.heap_next = (self.heap_next + size + 15) & ~15
        if self.heap_next > HEAP_BASE + HEAP_SIZE:
            raise RuntimeError('se acabó el montón del oráculo')
        self.uc.mem_write(address, b'\xcd' * size)
        return address

    # --- hooks ---------------------------------------------------------------------------------
    def _return(self, value=None, pop_args=0):
        uc = self.uc
        esp = uc.reg_read(UC_X86_REG_ESP)
        ret = self.r32(esp)
        if value is not None:
            uc.reg_write(UC_X86_REG_EAX, value & 0xffffffff)
        uc.reg_write(UC_X86_REG_ESP, esp + 4 + 4 * pop_args)
        uc.reg_write(UC_X86_REG_EIP, ret)
        return ret

    def _install_hooks(self):
        uc = self.uc

        def at(address, fn):
            uc.hook_add(UC_HOOK_CODE, lambda _uc, a, s, d: fn(), begin=address, end=address)

        def on_new():
            size = self.r32(uc.reg_read(UC_X86_REG_ESP) + 4)
            self._return(self.alloc(size))

        def on_free():
            self._return(0)

        def on_rand():
            ret = self.r32(uc.reg_read(UC_X86_REG_ESP))
            source = self.rand_current
            value = source.next()
            if source is self.rand_main:
                self.events.append({'rand': value, 'from': hex(ret), 'use': RAND_USES.get(ret, '?'),
                                    'phase': self.phase})
            self._return(value)

        def on_nothing():
            self._return(0)

        def on_map_error():
            self.events.append({'warning': '0x41fa40: la tabla de protección se llenó'})
            self._return(0)

        def on_after_step():
            cloth = uc.reg_read(UC_X86_REG_EDI)
            iteration = uc.reg_read(UC_X86_REG_ESI)
            ok = uc.reg_read(UC_X86_REG_EAX)
            self.events.append({'step': {'cloth': self.cloth_index(cloth), 'iteration': iteration, 'ok': ok,
                                         'state': self.dump_particles(cloth), 'wind': self.dump_wind(cloth)}})

        def on_init():
            esp = uc.reg_read(UC_X86_REG_ESP)
            args = [self.r32(esp + 4 + 4 * k) for k in range(12)]
            floats = [struct.unpack('<f', struct.pack('<I', a))[0] for a in args]
            self.events.append({'init': {
                'cloth_address': hex(uc.reg_read(UC_X86_REG_ECX)), 'object': hex(args[0]), 'bone': args[1],
                'x': floats[2], 'y': floats[3], 'z': floats[4], 'columns': args[5], 'rows': args[6],
                'width': floats[7], 'height': floats[8], 'texture_front': hex(args[9]),
                'texture_back': hex(args[10]), 'flags': hex(args[11])}})

        def on_sphere():
            esp = uc.reg_read(UC_X86_REG_ESP)
            args = [self.r32(esp + 4 + 4 * k) for k in range(5)]
            floats = [struct.unpack('<f', struct.pack('<I', a))[0] for a in args]
            self.events.append({'sphere': {'cloth_address': hex(uc.reg_read(UC_X86_REG_ECX)), 'x': floats[0],
                                           'y': floats[1], 'z': floats[2], 'radius': floats[3], 'bone': args[4]}})

        def on_render():
            cloth = uc.reg_read(UC_X86_REG_ECX)
            self.current_cloth = self.cloth_index(cloth)
            color_ptr = self.r32(uc.reg_read(UC_X86_REG_ESP) + 4)
            self.events.append({'render': {'cloth': self.current_cloth,
                                           'color': self.rfv(color_ptr, 3) if color_ptr else None}})

        def on_sprite():
            esp = uc.reg_read(UC_X86_REG_ESP)
            a = [self.r32(esp + 4 + 4 * k) for k in range(7)]
            self.events.append({'sprite_0x5491e0': {
                'texture': hex(a[0] & 0xffff), 'position': self.rfv(a[1], 3),
                'scale': struct.unpack('<f', struct.pack('<I', a[2]))[0], 'light': self.rfv(a[3], 3),
                'arg5_owner': a[4], 'arg6_rotation': struct.unpack('<f', struct.pack('<I', a[5]))[0],
                'arg7_subtype': a[6]}})

        def on_destroy():
            self.events.append({'destroy_0x4fb1a0': 'el paso devolvió 0: se sueltan las telas (+0x190 y +0x18c a 0)'})

        at(OP_NEW, on_new)
        at(OP_FREE, on_free)
        at(RAND, on_rand)
        at(ATEXIT, on_nothing)
        at(MAP_ERROR, on_map_error)
        at(AFTER_STEP, on_after_step)
        at(INIT_ENTRY, on_init)
        at(SPHERE_ENTRY, on_sphere)
        at(RENDER_ENTRY, on_render)
        at(SPRITE_ENTRY, on_sprite)
        at(DESTROY_ENTRY, on_destroy)

        def on_stub(_uc, address, _size, _data):
            dll, name = self.stubs.get(address, ('?', '?'))
            esp = uc.reg_read(UC_X86_REG_ESP)
            override = self.stub_overrides.get(name)
            if override is not None:
                override(esp)
                return
            if not dll.lower().startswith('opengl') or name not in GL_SIGNATURES:
                self.error = 'import sin stub: %s!%s' % (dll, name)
                uc.emu_stop()
                return
            signature = GL_SIGNATURES[name]
            args = []
            offset = 4
            count = 0
            k = 0
            while k < len(signature):
                code = signature[k]
                raw = self.r32(esp + offset)
                if code == 'p':
                    n = int(signature[k + 1])
                    args.append(self.rfv(raw, n))
                    k += 2
                else:
                    args.append(struct.unpack('<f', struct.pack('<I', raw))[0] if code == 'f' else raw)
                    k += 1
                offset += 4
                count += 1
            self.events.append({'gl': name, 'args': args})
            self._return(0, pop_args=count)

        uc.hook_add(UC_HOOK_CODE, on_stub, begin=STUB_BASE, end=STUB_BASE + 0x3fff)

        def on_unmapped(_uc, access, address, size, value, _data):
            self.error = 'memoria sin mapear %s en %s' % (hex(address), hex(uc.reg_read(UC_X86_REG_EIP)))
            return False
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)

    # --- ejecución -----------------------------------------------------------------------------
    def _load_segments(self):
        uc = self.uc
        # SS, DS y ES planos de 32 bits (anillo 0) y FS sobre el TEB.
        uc.reg_write(UC_X86_REG_SS, 2 << 3)
        uc.reg_write(UC_X86_REG_DS, 2 << 3)
        uc.reg_write(UC_X86_REG_ES, 2 << 3)
        uc.reg_write(UC_X86_REG_FS, (3 << 3) | 3)

    def _reset_cpu(self):
        self.uc.reg_write(UC_X86_REG_FPCW, self.fpu_control_word)
        self._load_segments()

    def call(self, address, args=(), ecx=0, rand=None):
        """Llama a `address` con los dwords de `args` en la pila y vuelve al centinela."""
        uc = self.uc
        self._reset_cpu()
        esp = STACK_BASE + STACK_SIZE - 0x4000
        for value in reversed(args):
            esp -= 4
            self.w32(esp, value)
        esp -= 4
        self.w32(esp, SENTINEL)
        uc.reg_write(UC_X86_REG_ESP, esp)
        uc.reg_write(UC_X86_REG_EBP, esp + 0x1000)
        uc.reg_write(UC_X86_REG_ECX, ecx)
        previous = self.rand_current
        self.rand_current = rand or self.rand_setup
        self._emulate(address, SENTINEL)
        self.rand_current = previous
        return uc.reg_read(UC_X86_REG_EAX)

    def _emulate(self, start, until):
        self.error = None
        try:
            self.uc.emu_start(start, until, count=50_000_000)
        except UcError as error:
            raise RuntimeError('la emulación cortó en %s: %s (%s)' % (hex(self.uc.reg_read(UC_X86_REG_EIP)), error,
                                                                      self.error))
        if self.error:
            raise RuntimeError(self.error)
        eip = self.uc.reg_read(UC_X86_REG_EIP)
        if eip != until:
            raise RuntimeError('la emulación paró en %s, no en %s' % (hex(eip), hex(until)))

    # --- lectura de la tela ----------------------------------------------------------------------
    def cloth_index(self, cloth):
        if cloth in getattr(self, 'direct_cloths', {}):
            return self.direct_cloths[cloth]
        array = self.r32(CHAR_BASE + 0x190)
        if array and cloth >= array and (cloth - array) % 0x60 == 0:
            return (cloth - array) // 0x60
        return hex(cloth)

    def dump_wind(self, cloth):
        return {'cloth_44': self.rf(cloth + 0x44), 'wind_7245f0': self.rf(0x7245f0),
                'wind_7245f4': self.rf(0x7245f4), 'wind_7245f8': self.rf(0x7245f8), 'wind_7245fc': self.rf(0x7245fc)}

    def dump_particles(self, cloth):
        count = self.ri32(cloth + 0x34)
        base = self.r32(cloth + 0x38)
        out = []
        for k in range(count):
            p = base + k * 0x3c
            out.append({'pos': self.rfv(p + 0x1c, 3), 'vel': self.rfv(p + 0x10, 3), 'force': self.rfv(p + 4, 3),
                        'pinned': self.r8(p + 0x28) & 1})
        return out

    def dump_cloth(self, cloth):
        c = {
            'address': hex(cloth), 'vtable': hex(self.r32(cloth)), 'object_04': hex(self.r32(cloth + 4)),
            'bone_08': self.ri32(cloth + 8), 'texture_front_0c': hex(self.r32(cloth + 0xc)),
            'texture_back_10': hex(self.r32(cloth + 0x10)), 'flags_14': hex(self.r32(cloth + 0x14)),
            'x_18': self.rf(cloth + 0x18), 'y_1c': self.rf(cloth + 0x1c), 'z_20': self.rf(cloth + 0x20),
            'width_24': self.rf(cloth + 0x24), 'height_28': self.rf(cloth + 0x28),
            'columns_2c': self.ri32(cloth + 0x2c), 'rows_30': self.ri32(cloth + 0x30),
            'particles_34': self.ri32(cloth + 0x34), 'springs_3c': self.ri32(cloth + 0x3c),
            'wind_44': self.rf(cloth + 0x44), 'wind_range_48': self.r8(cloth + 0x48),
            'wind_min_49': self.r8(cloth + 0x49), 'dx_4c': self.rf(cloth + 0x4c), 'dy_50': self.rf(cloth + 0x50),
            'spheres_54': self.ri32(cloth + 0x54),
        }
        springs = []
        base = self.r32(cloth + 0x40)
        for k in range(c['springs_3c']):
            s = base + k * 16
            springs.append([self.r16(s), self.r16(s + 2), self.rf(s + 4), self.rf(s + 8), self.r8(s + 0xc)])
        c['springs'] = springs
        spheres = []
        node = self.r32(self.r32(cloth + 0x58) + 8)
        tail = self.r32(cloth + 0x5c)
        while node and node != tail:
            sphere = self.r32(node)
            spheres.append({'vtable': hex(self.r32(sphere)), 'local': self.rfv(sphere + 4, 3),
                            'bone': self.ri32(sphere + 0x10), 'world': self.rfv(sphere + 0x14, 3),
                            'radius': self.rf(sphere + 0x20)})
            node = self.r32(node + 8)
        c['spheres'] = spheres
        c['state'] = self.dump_particles(cloth)
        return c

    def dump_cloths(self):
        if getattr(self, 'direct_cloths', None):
            return [self.dump_cloth(a) for a in sorted(self.direct_cloths, key=self.direct_cloths.get)]
        count = self.r8(CHAR_BASE + 0x18c)
        array = self.r32(CHAR_BASE + 0x190)
        return [self.dump_cloth(array + k * 0x60) for k in range(count)] if array else []

    # --- armado del estado ---------------------------------------------------------------------
    def construct_statics(self):
        """Los dos objetos globales que toca el bloque, con sus constructores reales."""
        self.call(MAP_CTOR, ecx=MAP_OBJECT)          # 0x4acef0 → 0x447f80 (usa un rand(): el de armado)
        self.call(MANAGER_CTOR, ecx=MANAGER)         # 0x622ad0 → 0x456990 (la lista vacía)
        # El registro de 0x7d3a0f0 en la tabla, como lo deja el primer acceso de 0x637700.
        self.frame_record = self.alloc(5)
        self.w8(self.frame_record + 4, 0)
        self.call(MAP_INSERT, args=(self.frame_record, FRAME_COUNTER), ecx=MAP_OBJECT)

    def set_frame_counter(self, value, lock_depth):
        """El valor de 0x7d3a0f0 para 0x4552a0. Con lock_depth 0 (sin otro acceso abierto) queda
        cifrado en su registro por 0x412890 real y la variable con basura; si no, en claro."""
        self.w32(FRAME_COUNTER, value)
        if lock_depth == 0:
            self.call(ENCRYPT, args=(self.frame_record, FRAME_COUNTER))
            self.w8(self.frame_record + 4, 0)
        else:
            self.w8(self.frame_record + 4, lock_depth)

    def write_character(self, spec):
        c = CHAR_BASE
        if not getattr(self, 'character_written', False):
            self.uc.mem_write(c, b'\0' * 0x1000)
            self.character_written = True
        self.w16(c + 0x2, spec.get('type_02', 0x20a))
        self.wf(c + 0xc, spec.get('scale_0c', 1.0))
        self.wfv(c + 0x10, spec.get('position_10', [0.0, 0.0, 0.0]))
        self.wfv(c + 0x1c, spec.get('angle_1c', [0.0, 0.0, 0.0]))
        self.w32(c + 0x78, spec.get('flags_78', 0))
        self.w32(c + 0x120, BONES_BASE)
        self.w8(c + 0x1d4, spec.get('class_1d4', 4))
        self.w16(c + 0x238, spec.get('armor_238', 0x122e))
        self.w16(c + 0x2f8, spec.get('wings_2f8', 0x1c30))
        self.w16(c + 0x376, spec.get('monster_376', -1))

    def write_bones(self, bones):
        flat = []
        for m in bones:
            if len(m) == 3:
                m = m[0] + m[1] + m[2]
            if len(m) != 12:
                raise ValueError('cada hueso: 12 floats (3 × 4 por filas, la traslación en la columna 3)')
            flat.extend(m)
        if len(flat) * 4 > BONES_SIZE:
            raise ValueError('demasiados huesos')
        self.wfv(BONES_BASE, flat)

    def write_model(self, model_type, spec, character):
        """El modelo del tipo del objeto: la escala (+0x68) y la posición (+0x6c) que copia 0x6001b0 del
        objeto (+0xc y +0x10) al posar el cuerpo; sin `model`, esas."""
        m = MODELS_BASE + model_type * MODEL_SIZE
        self.wf(m + 0x68, spec.get('body_scale_68', character.get('scale_0c', 1.0)))
        self.wfv(m + 0x6c, spec.get('body_origin_6c', character.get('position_10', [0.0, 0.0, 0.0])))

    def write_globals(self, values):
        for name, value in values.items():
            if name not in GLOBALS:
                raise ValueError('global desconocido: %s' % name)
            address, kind = GLOBALS[name]
            if kind == 'f':
                self.wf(address, value)
            elif kind == 'i':
                self.w32(address, value)
            else:
                self.w8(address, value)

    def write_texture_names(self, names):
        for slot, gl_name in names.items():
            self.w32(TEXTURE_TABLE + int(slot, 0) * 0x38, gl_name)

    # --- un frame ------------------------------------------------------------------------------
    def run_block(self, color, bvar23=1):
        uc = self.uc
        self._reset_cpu()
        ebp = STACK_BASE + STACK_SIZE - 0x8000
        uc.mem_write(ebp - 0x400, b'\0' * 0x420)
        self.w32(ebp + 8, CHAR_BASE)                    # param_1 (el personaje)
        self.w32(ebp + 0xc, CHAR_BASE)                  # param_2 (el objeto: el mismo puntero)
        self.w8(ebp + 0xf, bvar23)                      # bVar23, en el byte alto de param_2
        self.wfv(ebp - 0x5c, color)                     # local_60..local_58: el color de la tela
        self.w32(ebp - 4, 0xffffffff)
        uc.reg_write(UC_X86_REG_EBP, ebp)
        uc.reg_write(UC_X86_REG_ESP, ebp - 0x100)
        uc.reg_write(UC_X86_REG_EBX, CHAR_BASE)
        uc.reg_write(UC_X86_REG_EDI, CHAR_BASE)
        uc.reg_write(UC_X86_REG_ESI, 0)
        previous = self.rand_current
        self.rand_current = self.rand_main
        self._emulate(BLOCK_START, BLOCK_END)
        self.rand_current = previous


def create_direct_cloths(emu, definitions):
    """El modo directo: cada tela con sus parámetros, por las funciones reales, como las arma 0x50ba30:
    `operator new` de 0x60 bytes, el constructor 0x4548b0, el init de la vtable + 4 (0x454a50), los
    bytes del viento (+0x49 el mínimo, +0x48 el rango) y las esferas con 0x455cd0."""
    emu.direct_cloths = {}
    for index, d in enumerate(definitions):
        cloth = emu.alloc(0x60)
        emu.call(0x4548b0, ecx=cloth)
        i = d['init']
        fb = lambda v: struct.unpack('<I', struct.pack('<f', v))[0]  # noqa: E731
        args = (CHAR_BASE, i['bone'], fb(i['x']), fb(i['y']), fb(i['z']), i['columns'], i['rows'], fb(i['width']),
                fb(i['height']), i['front'], i['back'], i['flags'] & 0xffffffff)
        emu.call(emu.r32(emu.r32(cloth) + 4), args=args, ecx=cloth, rand=emu.rand_main)
        if 'wind_bytes' in d:
            emu.w8(cloth + 0x49, d['wind_bytes'][0])
            emu.w8(cloth + 0x48, d['wind_bytes'][1])
        for x, y, z, r, bone in d.get('spheres', []):
            emu.call(SPHERE_ENTRY, args=(fb(x), fb(y), fb(z), fb(r), bone), ecx=cloth, rand=emu.rand_main)
        emu.direct_cloths[cloth] = index


def run_direct_frame(emu, frame):
    """Cada tela viva: 0x455190(dt, 5) y, si da 1, 0x455930(color, 0); una que da 0 queda rota."""
    color = 0
    if frame.get('color', [1.0, 1.0, 1.0]) is not None:
        color = emu.alloc(12)
        emu.wfv(color, frame.get('color', [1.0, 1.0, 1.0]))
    dt = struct.unpack('<I', struct.pack('<f', frame.get('dt', struct.unpack('<f', struct.pack('<I', 0x3ba3d70a))[0])))[0]
    emu.broken = getattr(emu, 'broken', set())
    for cloth, index in sorted(emu.direct_cloths.items(), key=lambda kv: kv[1]):
        if cloth in emu.broken:
            continue
        ok = emu.call(0x455190, args=(dt, 5), ecx=cloth, rand=emu.rand_main)
        if ok:
            emu.call(RENDER_ENTRY, args=(color, 0), ecx=cloth, rand=emu.rand_main)
        else:
            emu.events.append({'broken': index})
            emu.broken.add(cloth)


def run(spec, exe_path=DEFAULT_EXE):
    """Corre los frames de `spec` y devuelve el resultado.

    spec = {
      "fpu_control_word": 0x27f,                  # el de Windows al arrancar un hilo (PC = 53 bits)
      "rand": [ ... ] | {"msvc_state": n},        # la fuente de rand() de todos los frames
      "texture_gl_names": {"0x256": 1001, ...},   # el nombre de GL de cada slot (0x7d3a1c4 + slot·0x38)
      "frames": [ {
          "character": {"type_02", "scale_0c", "position_10", "angle_1c", "flags_78", "class_1d4",
                        "armor_238", "wings_2f8", "monster_376"},
          "bones": [[12 floats] por hueso],         # objeto + 0x120: float[3][4] por hueso, por filas
          "model": {"body_scale_68", "body_origin_6c"},   # el modelo de type_02 (0x5769ad8 + tipo · 0xe0)
          "globals": {nombre: valor},               # ver GLOBALS
          "frame_counter_7d3a0f0": n, "frame_counter_lock_depth": 0,
          "wind_walk_calls": 5,                     # llamadas reales a 0x456b10 antes del bloque (0 = no)
          "color": [1, 1, 1],                       # local_60..local_58 de 0x50ba30
          "bvar23": 1,
          "dt": 0.005                               # solo en el modo directo
      }, ... ],
      "direct": [ {"init": {"bone", "x", "y", "z", "columns", "rows", "width", "height", "front", "back",
                            "flags"},
                   "wind_bytes": [mínimo, rango], "spheres": [[x, y, z, radio, hueso], ...]}, ... ]
                                                    # opcional: en vez del bloque de 0x50ba30, estas telas
    }
    """
    emu = ClothEmulator(exe_path, spec.get('fpu_control_word', 0x27f))
    emu.rand_main = Rand(spec.get('rand', {'msvc_state': 0}))
    emu.construct_statics()
    emu.write_texture_names(spec.get('texture_gl_names', {}))
    frames_out = []
    for number, frame in enumerate(spec['frames']):
        emu.events = []
        character = frame.get('character', {})
        emu.write_character(character)
        emu.write_bones(frame['bones'])
        emu.write_model(character.get('type_02', 0x20a), frame.get('model', {}), character)
        emu.write_globals(frame.get('globals', {}))
        emu.set_frame_counter(frame.get('frame_counter_7d3a0f0', 0), frame.get('frame_counter_lock_depth', 0))
        emu.phase = 'wind_walk'
        for _ in range(frame.get('wind_walk_calls', 0)):
            emu.call(WIND_WALK, args=(0x3ba3d70a,), ecx=MANAGER, rand=emu.rand_main)
        wind_after_walk = emu.rf(0x7245f0)
        emu.phase = 'cloth_block'
        if 'direct' in spec:
            if number == 0:
                create_direct_cloths(emu, spec['direct'])
            run_direct_frame(emu, frame)
        else:
            emu.run_block(frame.get('color', [1.0, 1.0, 1.0]), frame.get('bvar23', 1))
        frames_out.append({
            'frame': number,
            'wind_base_after_walk': wind_after_walk,
            'events': emu.events,
            'cloths_after': emu.dump_cloths(),
            'object_18c': emu.r8(CHAR_BASE + 0x18c),
            'draw': summarize_draw(emu.events),
        })
    return {'exe': exe_path, 'fpu_control_word': hex(spec.get('fpu_control_word', 0x27f)),
            'rand': emu.rand_main.describe(), 'frames': frames_out}


def summarize_draw(events):
    """Los vértices que manda cada dibujo: por tela y por cara, cada glVertex3f con su UV y su color."""
    out = []
    current = None
    color = None
    uv = None
    primitive = None
    for event in events:
        if 'render' in event:
            current = {'cloth': event['render']['cloth'], 'state_calls': [], 'faces': [], 'sprites': 0}
            out.append(current)
            continue
        if current is None:
            continue
        if 'sprite_0x5491e0' in event:
            current['sprites'] += 1
        name = event.get('gl')
        if name is None:
            continue
        args = event['args']
        if name in ('glColor3f', 'glColor4f'):
            color = args
            current['state_calls'].append([name, args])
        elif name == 'glColor3fv':
            color = args[0]
            current['state_calls'].append([name, args[0]])
        elif name == 'glBegin':
            primitive = {'mode': hex(args[0]), 'color': color, 'vertices': []}
            current['faces'].append(primitive)
        elif name == 'glEnd':
            primitive = None
        elif name == 'glTexCoord2f':
            uv = args
        elif name == 'glVertex3f':
            primitive['vertices'].append({'uv': uv, 'xyz': args})
        else:
            current['state_calls'].append([name, ['0x%x' % a if isinstance(a, int) else a for a in args]])
    return out


# --- el ejemplo: el Dark Lord del login ---------------------------------------------------------
def k16_decrypt(data):
    key = bytes([0xD1, 0x73, 0x52, 0xF6, 0xD2, 0x9A, 0xCB, 0x27, 0x3E, 0xAF, 0x59, 0x31, 0x37, 0xB3, 0xE7, 0xA2])
    out = bytearray(len(data))
    previous = 0x5E
    for i, b in enumerate(data):
        out[i] = ((key[i % 16] ^ b) - previous) & 0xff
        previous = (b + 0x3D) & 0xff
    return bytes(out)


def read_bmd_skeleton(path):
    """Los huesos y las keys de un .bmd (el formato de Client/src/Data/Models/Bmd.cpp)."""
    data = open(path, 'rb').read()
    if data[3] == 0x0C:
        size = struct.unpack_from('<I', data, 4)[0]
        body = k16_decrypt(data[8:8 + size])
    else:
        body = data[4:]
    pos = 32
    meshes, bones, actions = struct.unpack_from('<hhh', body, pos)
    pos += 6
    for _ in range(meshes):
        nv, nn, nt, ntri, _texture = struct.unpack_from('<hhhhh', body, pos)
        pos += 10 + nv * 16 + nn * 20 + nt * 8 + ntri * 64 + 32
    action_keys = []
    for _ in range(actions):
        keys = struct.unpack_from('<h', body, pos)[0]
        lock = body[pos + 2]
        pos += 3
        if lock:
            pos += keys * 12
        action_keys.append(keys)
    skeleton = []
    for _ in range(bones):
        dummy = body[pos]
        pos += 1
        if dummy:
            skeleton.append(None)
            continue
        name = body[pos:pos + 32].split(b'\0')[0].decode('latin-1')
        parent = struct.unpack_from('<h', body, pos + 32)[0]
        pos += 34
        per_action = []
        for keys in action_keys:
            positions = [struct.unpack_from('<3f', body, pos + 12 * k) for k in range(keys)]
            pos += 12 * keys
            rotations = [struct.unpack_from('<3f', body, pos + 12 * k) for k in range(keys)]
            pos += 12 * keys
            per_action.append((positions, rotations))
        skeleton.append({'name': name, 'parent': parent, 'actions': per_action})
    return skeleton, action_keys


def pose_with_binary_math(emu, skeleton, action, key, angles):
    """La pose de una key entera (sin interpolar) con las funciones de matemática del binario:
    0x5fc8f0 (la raíz con los ángulos del objeto, sin traslación), 0x5fcd20, 0x5fcdb0 y 0x5fca90.
    Es la entrada del ejemplo, no una emulación de 0x4eb860."""
    scratch = emu.alloc(0x100)
    root = scratch
    emu.wfv(scratch + 0x80, angles)
    emu.call(0x5fc8f0, args=(scratch + 0x80, root))
    out = []
    for index, bone in enumerate(skeleton):
        if bone is None:
            out.append([1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0])
            continue
        positions, rotations = bone['actions'][action]
        k = key if key < len(positions) else 0
        q, local, world = scratch + 0x90, scratch + 0xa0, scratch + 0xd0
        emu.wfv(scratch + 0x80, rotations[k])
        emu.call(ANGLE_QUATERNION, args=(scratch + 0x80, q))
        emu.call(QUATERNION_MATRIX, args=(q, local))
        emu.wf(local + 0x0c, positions[k][0])
        emu.wf(local + 0x1c, positions[k][1])
        emu.wf(local + 0x2c, positions[k][2])
        parent = root
        if bone['parent'] >= 0:
            parent = scratch + 0x40
            emu.wfv(parent, out[bone['parent']])
        emu.call(CONCAT_TRANSFORMS, args=(parent, local, world))
        out.append(emu.rfv(world, 12))
    return out


def example_input(exe_path=DEFAULT_EXE, frames=3):
    """El personaje de la escena 2 (0x517270(2, 4, …), 06 § El login, al entrar): la clase 4, la
    armadura 0x122e, las alas 0x1c30 (así la tercera tela se simula y se dibuja), en el mapa 0x37,
    en (10603, 14112, 580), girado 188° en z, con la escala 1,6, montando (la acción 0x4a)."""
    bmd = next((p for p in PLAYER_BMD_CANDIDATES if os.path.exists(p)), None)
    if bmd is None:
        raise RuntimeError('falta Player.bmd para armar los huesos del ejemplo')
    skeleton, _keys = read_bmd_skeleton(bmd)
    helper = ClothEmulator(exe_path)
    angles = [0.0, 0.0, 188.0]
    spec_frames = []
    for number in range(frames):
        bones = pose_with_binary_math(helper, skeleton, 0x4a, number, angles)
        spec_frames.append({
            'character': {'type_02': 0x20a, 'scale_0c': 1.6, 'position_10': [10603.0, 14112.0, 580.0],
                          'angle_1c': angles, 'flags_78': 0, 'class_1d4': 4, 'armor_238': 0x122e,
                          'wings_2f8': 0x1c30, 'monster_376': -1},
            'bones': bones,
            'model': {'body_scale_68': 1.6, 'body_origin_6c': [10603.0, 14112.0, 580.0]},
            'globals': {'map_6c9dd0': 0x37, 'world_time_5767b88': 40000.0 + 40.0 * number,
                        'gl_blend_7d366dc': 0, 'gl_alpha_test_7d366cd': 0, 'gl_texture_7d366d5': 1,
                        'gl_cull_7d366cc': 1, 'gl_depth_mask_7d36898': 1, 'gl_fog_7d368a0': 0,
                        'bound_texture_6d5d68': -1, **({'wind_base_7245f0': 0.0} if number == 0 else {})},
            'frame_counter_7d3a0f0': 1000 + number,
            'frame_counter_lock_depth': 0,
            'wind_walk_calls': 5,
            'color': [1.0, 1.0, 1.0],
            'bvar23': 1,
        })
    return {
        'fpu_control_word': 0x27f,
        'rand': {'msvc_state': 12345},
        'texture_gl_names': {'0x256': 0x256, '0x257': 0x257, '0x6ea': 0x6ea, '0x5ac': 0x5ac},
        'frames': spec_frames,
        'notes': ['Los huesos se posaron con las funciones de matemática del binario (0x5fc8f0, 0x5fcd20, '
                  '0x5fcdb0, 0x5fca90) sobre Player.bmd, la acción 0x4a, la key = el frame, sin interpolar: '
                  'son la entrada, no salen de emular 0x4eb860.',
                  'rand() sale de la fórmula de 0x69fd70 desde el estado 12345: en el original, antes del '
                  'bloque corren otros consumidores (faltantes.md).'],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('input', nargs='?', help='la entrada en JSON')
    parser.add_argument('--exe', default=DEFAULT_EXE)
    parser.add_argument('--example', action='store_true', help='el Dark Lord del login')
    parser.add_argument('--frames', type=int, default=3)
    parser.add_argument('--print-input', action='store_true', help='solo la entrada del ejemplo')
    args = parser.parse_args()
    if args.example:
        spec = example_input(args.exe, args.frames)
    elif args.input:
        spec = json.load(open(args.input))
    else:
        parser.error('falta la entrada o --example')
    if args.print_input:
        json.dump(spec, sys.stdout, indent=1)
        return
    result = run(spec, args.exe)
    result['input'] = {k: v for k, v in spec.items() if k != 'frames'}
    result['input']['frames'] = [{k: v for k, v in f.items()} for f in spec['frames']]
    json.dump(result, sys.stdout, indent=1)


if __name__ == '__main__':
    main()
