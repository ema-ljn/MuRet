#!/usr/bin/env python3
"""Enumera los CALL directos a la API de audio c32bbd5f a partir de la caché del research.

Uso: audio_sites.py > cl10-sitios.tsv
Armar primero la caché con tools/regen_cache.py y el binario indicado en el research.
"""
import bisect
import csv
import re
import sys
from pathlib import Path

import cg

REFERENCE_SHA256 = 'c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a'
ROADMAP = Path(__file__).resolve().parents[2] / 'coverage' / 'mainexe-roadmap.tsv'
SET_SOUND_VOLUME = 0x41EED0
SECTION = re.compile(r'^Disassembly of section (.+):$')


def direct_calls(listing):
    """Lee sólo la sección de código y conserva una dirección por instrucción CALL."""
    in_text = False
    for line in listing:
        section = SECTION.match(line)
        if section:
            in_text = section[1] == '.text'
        if not in_text:
            continue
        instruction = cg.LINE.match(line)
        if instruction:
            target = cg.DIRECT_CALL.fullmatch(instruction[2].strip())
            if target:
                yield int(instruction[1], 16), int(target[1], 16)


def sites(functions, calls):
    starts = sorted(functions)
    audio = {start for start, row in functions.items() if row['primary_wave'] == 'CL10'}
    audio.add(SET_SOUND_VOLUME)
    for address, target in calls:
        if target not in audio:
            continue
        index = bisect.bisect_right(starts, address)
        if index == 0:
            sys.exit(f'falta una función anterior al sitio {address:#x}')
        owner = starts[index - 1]
        row = functions[owner]
        wave = row['primary_wave']
        assignment = 'in_range' if address < owner + int(row['size_bytes']) else 'preceding_owner'
        if wave == 'CL10':
            scope = 'CL10'
        elif 'código muerto' in row['purpose'].lower():
            scope = 'dead_by_roadmap'
        else:
            scope = 'external_live_by_roadmap'
        yield f'{address:08x}', f'{target:08x}', f'{owner:08x}', wave, assignment, scope


def main():
    source = Path(cg.cache_file(cg.SOURCE))
    listing = Path(cg.cache_file(cg.LISTING))
    if not source.is_file() or not listing.is_file():
        sys.exit('falta la caché del binario. Correr tools/regen_cache.py primero.')
    digest = source.read_text(encoding='utf-8').partition(' ')[0]
    if digest != REFERENCE_SHA256:
        sys.exit('la caché debe salir del main.exe c32bbd5f')

    with ROADMAP.open(encoding='utf-8', newline='') as rows:
        functions = {int(row['address'], 16): row for row in csv.DictReader(rows, delimiter='\t')}
    writer = csv.writer(sys.stdout, delimiter='\t', lineterminator='\n')
    writer.writerow(['site_va', 'api_va', 'owner_va', 'wave', 'assignment', 'owner_scope'])
    with listing.open(encoding='utf-8', errors='replace') as instructions:
        writer.writerows(sites(functions, direct_calls(instructions)))


if __name__ == '__main__':
    main()
