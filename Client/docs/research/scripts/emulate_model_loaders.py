r"""Emula un cargador de modelos del main.exe de c32bbd5f y registra lo que hace (CL6.1).

Registra cada llamada a 0x60da30 (el modelo, la carpeta, el nombre y el número del archivo, con la
dirección de la llamada) y lo que el cargador escribe en la tabla de modelos y en las acciones de un
modelo. Las demás llamadas se saltean y devuelven un puntero a memoria limpia. De acá salen las tablas
de Client/src/Data/Models/PlayerModels.cpp y PlayerActions.cpp.

Necesita unicorn y pefile (pip install unicorn pefile) y el main.exe del laboratorio
(analysis/mainexe/originals/main.exe en EmaNLJN/mu-online-reverse-lab). Arrancá desde el principio
de la función, o desde un punto que no lea registros de antes: los que cargó antes quedan en 0 (en
0x60ff50, ebx vale 1 desde 0x60ff59, y 0x6109ed lo escribe en el byte del modo Data\).

    python emulate_model_loaders.py main.exe 60e8fc 60f75a 20a > personaje.json   # 213 cargas y 181 velocidades
    python emulate_model_loaders.py main.exe 60ff50 6117fd > items.json           # 243 cargas
    python emulate_model_loaders.py main.exe 61ca30 61cc34 > seleccion.json       # Logo05 y los NewFace

El del personaje arranca en 0x60e8fc, el modo Data\ de 0x60dbe0: lo de antes arma la tabla de
0x5769ad8, que el script arma él, y el modo Data\ no lee registros de antes.
"""

import json
import struct
import sys

import pefile
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_UNMAPPED
from unicorn.x86_const import UC_X86_REG_EIP, UC_X86_REG_ESP, UC_X86_REG_EBP, UC_X86_REG_EAX

LOAD_MODEL = 0x60da30
TABLE_POINTER = 0x5769AD8
MODEL_SIZE = 0xE0
MODEL_COUNT = 0x489D
TABLE_BASE = 0x20000000
ACTIONS_BASE = 0x30000000
ACTIONS_PER_MODEL = 0x400
ACTION_SIZE = 0x10
STACK_TOP = 0x7F000000
HEAP_BASE = 0x40000000


def page(value):
    return value & ~0xFFF


def map_pe(uc, pe):
    base = pe.OPTIONAL_HEADER.ImageBase
    for section in pe.sections:
        start = base + section.VirtualAddress
        size = max(section.Misc_VirtualSize, section.SizeOfRawData)
        uc.mem_map(page(start), page(start + size + 0xFFF) - page(start))
        uc.mem_write(start, section.get_data()[:size])
    headers = pe.header[:0x1000]
    uc.mem_map(base, 0x1000)
    uc.mem_write(base, bytes(headers))


def c_string(uc, address):
    data = bytes(uc.mem_read(address, 64))
    return data.split(b'\0', 1)[0].decode('latin-1')


def run(path, start, end, speed_model=None):
    pe = pefile.PE(path)
    uc = Uc(UC_ARCH_X86, UC_MODE_32)
    map_pe(uc, pe)
    uc.mem_map(TABLE_BASE, page(MODEL_SIZE * MODEL_COUNT + 0xFFF))
    uc.mem_write(TABLE_POINTER, struct.pack('<I', TABLE_BASE))
    # Cada modelo con su arreglo de acciones propio, para ver qué escribe el cargador en ellas.
    uc.mem_map(ACTIONS_BASE, page(ACTION_SIZE * ACTIONS_PER_MODEL * 4 + 0xFFF))
    if speed_model is not None:
        uc.mem_write(TABLE_BASE + speed_model * MODEL_SIZE + 0x30, struct.pack('<I', ACTIONS_BASE))
    uc.mem_map(STACK_TOP - 0x100000, 0x100000)
    uc.mem_map(HEAP_BASE, 0x100000)
    uc.reg_write(UC_X86_REG_ESP, STACK_TOP - 0x10000)
    uc.reg_write(UC_X86_REG_EBP, STACK_TOP - 0x8000)

    loads = []
    writes = []
    model_writes = []

    def on_code(uc, address, size, _):
        if not (start <= address < end):
            uc.emu_stop()
            return
        code = bytes(uc.mem_read(address, size))
        if code[0] == 0xE8:
            target = (address + 5 + struct.unpack('<i', code[1:5])[0]) & 0xFFFFFFFF
            esp = uc.reg_read(UC_X86_REG_ESP)
            if target == LOAD_MODEL:
                model, folder, name, number = struct.unpack('<IIIi', bytes(uc.mem_read(esp, 16)))
                loads.append({'call': hex(address), 'model': model, 'folder': c_string(uc, folder),
                              'name': c_string(uc, name), 'number': number})
            uc.reg_write(UC_X86_REG_EAX, HEAP_BASE)
            uc.reg_write(UC_X86_REG_EIP, address + 5)
        elif code[0] == 0xFF and (code[1] >> 3) & 7 == 2:
            # call indirecto (un import o un método virtual): se saltea, con eax en memoria limpia.
            uc.reg_write(UC_X86_REG_EAX, HEAP_BASE)
            uc.reg_write(UC_X86_REG_EIP, address + size)

    def on_write(uc, access, address, size, value, _):
        eip = uc.reg_read(UC_X86_REG_EIP)
        if ACTIONS_BASE <= address < ACTIONS_BASE + ACTION_SIZE * ACTIONS_PER_MODEL:
            offset = address - ACTIONS_BASE
            writes.append({'at': hex(eip), 'action': offset // ACTION_SIZE, 'field': offset % ACTION_SIZE,
                           'size': size, 'value': value})
        elif TABLE_BASE <= address < TABLE_BASE + MODEL_SIZE * MODEL_COUNT:
            offset = address - TABLE_BASE
            model_writes.append({'at': hex(eip), 'model': offset // MODEL_SIZE, 'field': hex(offset % MODEL_SIZE),
                                 'size': size, 'value': value})

    def on_unmapped(uc, access, address, size, value, _):
        print('memoria sin mapear', hex(address), 'en', hex(uc.reg_read(UC_X86_REG_EIP)), file=sys.stderr)
        return False

    uc.hook_add(UC_HOOK_CODE, on_code)
    uc.hook_add(UC_HOOK_MEM_WRITE, on_write)
    uc.hook_add(UC_HOOK_MEM_UNMAPPED, on_unmapped)
    try:
        uc.emu_start(start, end)
    except Exception as error:  # noqa: BLE001 - se informa dónde cortó
        print('cortó en', hex(uc.reg_read(UC_X86_REG_EIP)), error, file=sys.stderr)
    return {'loads': loads, 'writes': writes, 'model_writes': model_writes,
            'stopped_at': hex(uc.reg_read(UC_X86_REG_EIP))}


if __name__ == '__main__':
    args = sys.argv[1:]
    result = run(args[0], int(args[1], 16), int(args[2], 16), int(args[3], 16) if len(args) > 3 else None)
    json.dump(result, sys.stdout, indent=1)
