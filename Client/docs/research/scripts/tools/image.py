"""La imagen del main.exe como la mapea Windows, para las herramientas que leen bytes del binario.

El exe sale de MURET_MAIN_EXE (el c32bbd5f; ver ../README.md § El entorno) o del --exe de cada
herramienta. Las direcciones son VAs (base 0x400000).
"""
import hashlib
import os
import struct
import sys

import pefile

C32BBD5F = 'c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a'


def exe_path(given=None):
    path = given or os.environ.get('MURET_MAIN_EXE')
    if not path:
        sys.exit('falta el main.exe: MURET_MAIN_EXE o --exe (ver scripts/README.md, El entorno)')
    if not os.path.isfile(path):
        sys.exit('no existe el main.exe: %s' % path)
    return path


def sha256(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()


class Image:
    """La imagen mapeada: `read(va, n)`, y los enteros, floats y strings de una VA."""

    def __init__(self, path=None):
        self.path = exe_path(path)
        self.pe = pefile.PE(self.path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.bytes = self.pe.get_memory_mapped_image()

    def contains(self, va):
        return self.base <= va < self.base + len(self.bytes)

    def read(self, va, n):
        return self.bytes[va - self.base:va - self.base + n]

    def u8(self, va):
        return self.bytes[va - self.base]

    def u32(self, va):
        return struct.unpack('<I', self.read(va, 4))[0]

    def i32(self, va):
        return struct.unpack('<i', self.read(va, 4))[0]

    def f32(self, va):
        return struct.unpack('<f', self.read(va, 4))[0]

    def f64(self, va):
        return struct.unpack('<d', self.read(va, 8))[0]

    def cstr(self, va, limit=256):
        return self.read(va, limit).split(b'\0')[0]
