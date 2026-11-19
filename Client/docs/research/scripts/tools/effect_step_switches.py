#!/usr/bin/env python3
"""Índice de las tablas de salto de 0x5208a0 y de sus llamadas directas a efectos."""

import argparse
import csv
from dataclasses import dataclass
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cg
from effect_sites import TARGETS, validate_source

ROOT = 0x5208a0
COMMON_TAIL = 0x52cb98
END = 0x52ce14


@dataclass(frozen=True)
class Table:
    site: int
    field: str
    first: int
    count: int
    pointers: int
    pointer_count: int
    selectors: int | None = None
    bypassed: tuple[int, ...] = ()


# Límites de los cmp que preceden cada jmp y bases de los lea/add del selector.
# 0x52b5a1 desvía el subtipo 4 a 0x52b5a3 antes del despacho de 0x52b60b.
TABLES = (
    Table(0x520965, 'tipo', 0x9, 0x10a, 0x52ce14, 56, 0x52cef4),
    Table(0x521877, 'subtipo', 1, 4, 0x52d000, 4),
    Table(0x522dc3, 'subtipo', 0, 8, 0x52d010, 8),
    Table(0x525e96, 'tipo', 0x114, 0x3b, 0x52d030, 36, 0x52d0c0),
    Table(0x52a18a, 'subtipo', 1, 5, 0x52d0fc, 5),
    Table(0x52acbc, 'tipo', 0x5da, 0x36, 0x52d110, 6, 0x52d128),
    Table(0x52b594, 'tipo', 0x61c, 0xd, 0x52d160, 7, 0x52d17c),
    Table(0x52b60b, 'subtipo', 0, 6, 0x52d18c, 6, bypassed=(4,)),
    Table(0x52ccb0, 'tipo en la cola', 0xd4, 0x68, 0x52d1a4, 5, 0x52d1b8),
)


def decode_table(read, table):
    """Decodifica punteros de 32 bits, con o sin mapa previo de bytes."""
    pointers = struct.unpack('<' + 'I' * table.pointer_count,
                             read(table.pointers, 4 * table.pointer_count))
    if table.selectors is None:
        indices = range(table.count)
    else:
        indices = read(table.selectors, table.count)
        if len(indices) != table.count:
            raise ValueError(f'mapa selector incompleto en {table.site:#x}')
    if any(index >= table.pointer_count for index in indices):
        raise ValueError(f'selector fuera de su tabla en {table.site:#x}')
    return {key: pointers[index] for key, index in enumerate(indices, table.first)
            if key not in table.bypassed}


def read_body(listing):
    instructions = {}
    in_text = False
    for raw in listing:
        if raw.startswith('Disassembly of section '):
            in_text = raw.strip() == 'Disassembly of section .text:'
        match = cg.LINE.match(raw) if in_text else None
        if match:
            address = int(match[1], 16)
            if ROOT <= address < END:
                instructions[address] = match[2].strip()
    return instructions


def reach_sites(instructions, jump_targets, start, stop=None):
    """Recorre el cuerpo sin entrar en las llamadas y sigue ambas salidas condicionales."""
    addresses = sorted(instructions)
    next_address = dict(zip(addresses, addresses[1:]))
    pending = [start]
    seen = set()
    calls = set()
    while pending:
        address = pending.pop()
        if address == stop or address in seen:
            continue
        text = instructions.get(address)
        if not text:
            raise ValueError(f'salto fuera del cuerpo en {address:#x}')
        seen.add(address)
        parts = text.split(maxsplit=1)
        op = parts[0]
        if op.startswith('ret') or op == 'int3':
            continue
        if address in jump_targets:
            pending.extend(jump_targets[address])
            continue
        if op.startswith(('j', 'loop')):
            if len(parts) != 2 or not parts[1].startswith('0x'):
                raise ValueError(f'salto indirecto sin tabla en {address:#x}')
            pending.append(int(parts[1].split()[0], 16))
            if op == 'jmp':
                continue
        call = cg.DIRECT_CALL.fullmatch(text)
        if call:
            target = int(call[1], 16)
            if target in TARGETS:
                calls.add((address, target))
        following = next_address.get(address)
        if following is None:
            raise ValueError(f'caída al final del listado en {address:#x}')
        pending.append(following)
    return calls


def write_index(image, instructions, output, by_site=False):
    tables = {table.site: decode_table(image.read, table) for table in TABLES}
    for table in TABLES:
        operation = instructions.get(table.site, '')
        if not operation.startswith('jmp') or f'+{table.pointers:#x}]' not in operation:
            raise ValueError(f'no coincide el despacho de {table.site:#x}')
    jumps = {site: set(entries.values()) for site, entries in tables.items()}
    direct = {(address, int(call[1], 16)) for address, text in instructions.items()
              if (call := cg.DIRECT_CALL.fullmatch(text)) and int(call[1], 16) in TARGETS}
    reached = reach_sites(instructions, jumps, ROOT)
    if len(direct) != 233 or reached != direct:
        raise ValueError(f'el paso debe reunir los 233 sitios del listado ({len(reached)} alcanzados)')

    writer = csv.writer(output, delimiter='\t', lineterminator='\n')
    calls = {}
    entries = []
    for table in TABLES:
        stop = COMMON_TAIL if table.site < COMMON_TAIL else None
        for key, entry in tables[table.site].items():
            if (entry, stop) not in calls:
                calls[entry, stop] = sorted(reach_sites(instructions, jumps, entry, stop))
            entries.append((table.site, table.field, key, entry, calls[entry, stop]))
    if by_site:
        by_call = {call: [] for call in direct}
        for dispatch, _, key, _, calls_in_arm in entries:
            for call in calls_in_arm:
                by_call[call].append(f'{dispatch:#x}/{key:#x}')
        writer.writerow(('site_va', 'target_va', 'familia', 'brazos_posibles'))
        for (site, target), arms in sorted(by_call.items()):
            writer.writerow((f'{site:#x}', f'{target:#x}', TARGETS[target][0], ','.join(arms)))
        return
    writer.writerow(('despacho_va', 'campo', 'entrada', 'bloque_va', 'sitios_posibles'))
    for dispatch, field, key, entry, calls_in_arm in entries:
        sites = ','.join(f'{site:#x}' for site, _ in calls_in_arm)
        writer.writerow((f'{dispatch:#x}', field, f'{key:#x}', f'{entry:#x}', sites))


def main():
    import image

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', help='main.exe c32bbd5f (o MURET_MAIN_EXE)')
    parser.add_argument('--sites', action='store_true', help='una fila por llamada directa')
    args = parser.parse_args()
    executable = image.exe_path(args.exe)
    try:
        with open(cg.cache_file(cg.SOURCE), encoding='utf-8') as source:
            validate_source(source.read())
        validate_source(image.sha256(executable))
        with open(cg.cache_file(cg.LISTING), encoding='utf-8') as listing:
            instructions = read_body(listing)
        write_index(image.Image(executable), instructions, sys.stdout, args.sites)
    except (OSError, ValueError, struct.error) as error:
        sys.exit(str(error))


if __name__ == '__main__':
    main()
