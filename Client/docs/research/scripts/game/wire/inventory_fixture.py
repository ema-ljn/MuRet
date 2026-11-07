#!/usr/bin/env python3
"""Genera solo los siete pares F3/10 del cable local, fuera del repositorio.

Requiere MURET_CLIENT_WIRE_DIR, MURET_C3C4_REFERENCE y MURET_MAIN_EXE; el EXE se
lee como datos para la referencia matemática, nunca se ejecuta. No imprime bytes.
"""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import sys

# Al invocarlo desde cualquier carpeta, incluso con -I, importa el wirelib vecino.
# No deja __pycache__ en el checkout ni junto a la referencia externa.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition):
    if not condition:
        raise ValueError('captura de inventario inválida')


def check_record(packet, wirelib):
    """Comprueba checksum y layout antes de permitir que un par llegue al disco."""
    clear = packet.data
    require(clear[0] == 0xC2 and len(clear) >= 6)
    require(int.from_bytes(clear[1:3], 'big') == len(clear))
    require(packet.wire[0] == 0xC4)
    require(int.from_bytes(packet.wire[1:3], 'big') == len(packet.wire))
    count = clear[5]
    require(len(clear) == 6 + 8 * count)
    reference = wirelib._KEYS.get()
    body, useful = reference.decrypt(wirelib._KEYS.dec2, packet.wire[3:])
    require(useful >= 0 and useful == len(clear) - 2)
    require(clear[3:] == body[1:useful])
    slots = list(clear[6::8])
    expected_slots = {2: [0, 1], 5: [0, 12, 14, 17, 23], 6: [0, 12, 14, 17, 23, 28]}
    require(slots == expected_slots.get(count))
    return count, slots


def collect(source, wirelib):
    """Decodifica el flujo completo; conserva únicamente inventario S->C/C4/F3/10."""
    records = []
    pairs = []
    for packet in wirelib.read_log(str(source)):
        if packet.direction != 'S->C' or packet.frame != 'C4' or packet.data is None:
            continue
        if wirelib.opcode(packet.data) != (0xF3, 0x10):
            continue
        count, slots = check_record(packet, wirelib)
        index = len(records)
        capture_name = f'inventory-capture-{index}.c4'
        reference_name = f'inventory-reference-{index}.c2'
        records.append({
            'index': index, 'connection_index': packet.conn,
            'capture_file': capture_name, 'capture_size': len(packet.wire),
            'capture_sha256': sha256(packet.wire),
            'reference_file': reference_name, 'reference_size': len(packet.data),
            'reference_sha256': sha256(packet.data),
            'count': count, 'slots': slots, 'stride': 8, 'checksum_valid': True,
        })
        pairs.extend(((capture_name, packet.wire), (reference_name, packet.data)))
    require(len(records) == 7)
    require(Counter(record['count'] for record in records) == {2: 1, 5: 5, 6: 1})
    return records, pairs


def inputs(log):
    required = ('MURET_CLIENT_WIRE_DIR', 'MURET_C3C4_REFERENCE', 'MURET_MAIN_EXE')
    for name in required:
        if not os.environ.get(name):
            raise ValueError('falta ' + name)
    wire_dir = Path(os.environ['MURET_CLIENT_WIRE_DIR'])
    source = log or wire_dir / 'muproxy.log'
    files = {
        'log': source,
        'dec1': wire_dir / 'Dec1.dat',
        'enc2': wire_dir / 'Enc2.dat',
        'reference': Path(os.environ['MURET_C3C4_REFERENCE']),
        'main_exe': Path(os.environ['MURET_MAIN_EXE']),
        'wirelib': Path(__file__).resolve().with_name('wirelib.py'),
    }
    for path in files.values():
        require(path.is_file())
    return source, {name + '_sha256': sha256(path.read_bytes()) for name, path in files.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, help='log alternativo; las claves siguen en MURET_CLIENT_WIRE_DIR')
    parser.add_argument('--output-dir', type=Path, required=True, help='directorio externo para los sidecars privados')
    args = parser.parse_args()
    try:
        output = args.output_dir.resolve()
        repository = Path(__file__).resolve().parents[6]
        require(output != repository and repository not in output.parents)
        source, provenance = inputs(args.log)
        import wirelib
        records, pairs = collect(source, wirelib)
        # Todos los pares están comprobados: no se vuelcan otros frames ni claves del flujo.
        output.mkdir(parents=True, exist_ok=True)
        for name, data in pairs:
            (output / name).write_bytes(data)
        manifest = {'schema': 1, 'provenance': provenance, 'records': records}
        manifest_bytes = (json.dumps(manifest, indent=2) + '\n').encode('utf-8')
        (output / 'inventory-fixture-manifest.json').write_bytes(manifest_bytes)
    except (OSError, ValueError, TypeError, IndexError, KeyError, AssertionError):
        parser.exit(1, 'no se pudo generar el fixture: revisar entradas, checksum, layout y salida externa\n')
    print(json.dumps({'record_count': len(records), 'manifest_sha256': sha256(manifest_bytes)}))


if __name__ == '__main__':
    main()
