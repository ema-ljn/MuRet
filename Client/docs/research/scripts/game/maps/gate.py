#!/usr/bin/env python3
"""Data\\Gate.bmd en claro, como lo lee 0x54a360 (desde 0x61ccb0, con la ruta de 0x6d2aec): 256 registros
de 9 bytes, cada uno con el XOR3 (FC CF AB) desde su primer byte.

Imprime los registros que no están vacíos: el número de gate y sus nueve campos, en el orden de
GateRecord de la CL7.5 B (flag, mapa, x1, y1, x2, y2, destino, dirección, nivel). Con el Gate.bmd del
laboratorio son 125 gates; los cuatro de Lorencia son los de los tests de la 5.B.

Uso: gate.py [Gate.bmd]      (por defecto, el del Data\\)
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402

RECORDS, SIZE = 256, 9
FIELDS = ('flag', 'mapa', 'x1', 'y1', 'x2', 'y2', 'destino', 'dir', 'nivel')


def gate_bmd():
    root = common.data_dir()
    for f in os.listdir(root):
        if f.lower() == 'gate.bmd':
            return os.path.join(root, f)
    sys.exit('no está Gate.bmd en %s' % root)


def read_gates(path=None):
    with open(path or gate_bmd(), 'rb') as f:
        data = f.read()
    return [common.xor3(data[i * SIZE:(i + 1) * SIZE]) for i in range(RECORDS)]


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else gate_bmd()
    print('tamaño', os.path.getsize(path))
    print('gate', ' '.join('%7s' % f for f in FIELDS))
    for i, record in enumerate(read_gates(path)):
        if any(record):
            print('%4d' % i, ' '.join('%7d' % b for b in record))


if __name__ == '__main__':
    main()
