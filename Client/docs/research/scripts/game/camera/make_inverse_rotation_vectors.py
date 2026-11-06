#!/usr/bin/env python3
"""Vectores de 0x5fcc30 real (VectorIRotate), CL7.3 A, tarea 1.

Reusa los 400 vectores y matrices de world55/rotation.txt y agrega tres
cancelaciones sintéticas con 2^70. Toda salida viene del código del main.exe,
emulado con el mismo arnés de login/make_rotation_vectors.py, en x87 PC53.
Uso: python -I /ruta/al/script/make_inverse_rotation_vectors.py salida
Requiere MURET_MAIN_EXE, unicorn y pefile; ver scripts/README.md, El entorno.
"""
import argparse
import hashlib
from pathlib import Path
import struct
import sys
import tempfile

SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parents[5]
ROTATION_VECTORS = REPO / 'Client/tests/vectors/world55/rotation.txt'
EXE_SHA256 = 'c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a'
ROTATION_CASES = 400
INVERSE_ROTATE = 0x5fcc30  # 06 § La cámara del juego: la transpuesta del giro 3 × 4.
VECTOR_FLOATS = 3
MATRIX_FLOATS = 12

sys.path.insert(0, str(SCRIPT_DIR.parent))
import common  # noqa: E402


def verify_exe():
    exe = common.main_exe()
    digest = hashlib.sha256(Path(exe).read_bytes()).hexdigest()
    if digest != EXE_SHA256:
        raise SystemExit('MURET_MAIN_EXE: SHA-256 distinto del build c32bbd5f: ' + digest)


def rotation_cases():
    lines = ROTATION_VECTORS.read_text(encoding='utf-8').splitlines()
    if lines[:2] != ['rotation 1', 'cases %d' % ROTATION_CASES] or lines[-1:] != ['end']:
        raise ValueError('rotation.txt: cabecera o cierre inesperados')
    cases = lines[2:-1]
    if len(cases) != ROTATION_CASES:
        raise ValueError('rotation.txt: se esperaban %d casos' % ROTATION_CASES)
    for line in cases:
        words = line.split()
        if len(words) != 22 or words[0] != 'case':
            raise ValueError('rotation.txt: caso con formato inesperado')
        # Descarta ángulos y salida de VectorRotate: solo reusa entradas existentes.
        yield words[4:7], words[7:19]


def float_bits(value):
    return '%08x' % struct.unpack('<I', struct.pack('<f', value))[0]


def cancellation_cases():
    huge = float.fromhex('0x1p70')
    for vector, indices in (((-huge, 1.0, huge), (0, 4, 8)),
                            ((huge, -huge, 1.0), (1, 9, 5)),
                            ((huge, -huge, 1.0), (2, 10, 6))):
        matrix = [0.0] * MATRIX_FLOATS
        for index in indices:
            matrix[index] = 1.0
        yield [float_bits(v) for v in vector], [float_bits(v) for v in matrix]


def write_words(emu, address, words):
    values = [int(word, 16) for word in words]
    emu.uc.mem_write(address, struct.pack('<%dI' % len(values), *values))


def generate(emu):
    vector_address = emu.alloc(VECTOR_FLOATS * 4)
    matrix_address = emu.alloc(MATRIX_FLOATS * 4)
    output_address = emu.alloc(VECTOR_FLOATS * 4)
    cases = list(rotation_cases()) + list(cancellation_cases())
    yield 'inverse_rotation 1'
    for vector, matrix in cases:
        write_words(emu, vector_address, vector)
        write_words(emu, matrix_address, matrix)
        emu.call(INVERSE_ROTATE, [vector_address, matrix_address, output_address])
        output = ['%08x' % emu.r32(output_address + 4 * i) for i in range(VECTOR_FLOATS)]
        yield 'case ' + ' '.join(vector + matrix + output)
    yield 'end'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='archivo inverse_rotation.txt que se genera')
    args = parser.parse_args()
    verify_exe()
    common.use_login_harness()
    from emu import Emu

    # Se completa antes de escribir: un fallo del oráculo no deja vectores parciales.
    text = '\n'.join(generate(Emu())) + '\n'
    # El archivo completo reemplaza al anterior en el mismo filesystem.
    with tempfile.TemporaryDirectory(prefix='.inverse-rotation-', dir=args.output.parent) as directory:
        temporary = Path(directory) / args.output.name
        temporary.write_text(text, encoding='utf-8', newline='\n')
        temporary.replace(args.output)
    print('casos 403, SHA-256 ' + hashlib.sha256(text.encode('utf-8')).hexdigest())


if __name__ == '__main__':
    main()
