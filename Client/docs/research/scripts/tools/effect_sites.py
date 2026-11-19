#!/usr/bin/env python3
"""Censo crudo CL11.R: calls directos a siete destinos del c32bbd5f, solo en .text.

Lee all.txt y source.txt de regen_cache.py; emite TSV a stdout, sin atribuir funciones,
propiedad ni alcanzabilidad. Uso: python3 -I tools/effect_sites.py > /tmp/effect-sites.tsv
"""
from collections import Counter
import csv
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cg  # noqa: E402

C32_SHA256 = 'c32bbd5f16ca400c7cfc71fa5f4cd573c57f146dd19a6da52fdd1007abaca26a'
SECTION_PREFIX = 'Disassembly of section '
TARGETS = {
    0x518cf0: ('modelo', 'alta/actualización'),
    0x530860: ('joints', 'alta/actualización'),
    0x53fe70: ('partículas', 'alta/actualización'),
    0x5491e0: ('sprites', 'alta/actualización'),
    0x52e470: ('cintas', 'alta/actualización'),
    0x52e500: ('cintas', 'paso'),
    0x52e590: ('cintas', 'dibujo'),
}
EXPECTED_COUNTS = {
    0x518cf0: 378, 0x530860: 269, 0x53fe70: 435, 0x5491e0: 378,
    0x52e470: 22, 0x52e500: 1, 0x52e590: 3,
}



def validate_source(source):
    """Las VAs de este censo solo corresponden al hash completo del research."""
    fields = source.split()
    if not fields or fields[0] != C32_SHA256:
        raise ValueError('source.txt debe identificar el SHA-256 c32bbd5f completo; '
                         'regenerar la caché con el exe del research')


def scan_sites(listing):
    sites = []
    in_text = False
    for raw in listing:
        if raw.startswith(SECTION_PREFIX):
            in_text = raw.strip() == SECTION_PREFIX + '.text:'
            continue
        if not in_text:
            continue
        instruction = cg.LINE.match(raw)
        if not instruction:
            continue
        call = cg.DIRECT_CALL.fullmatch(instruction.group(2).rstrip())
        if not call:
            continue
        target = int(call.group(1), 16)
        if target in TARGETS:
            family, phase = TARGETS[target]
            sites.append((int(instruction.group(1), 16), target, family, phase))
    return sorted(sites)


def validate_census(sites):
    if len({site for site, *_ in sites}) != len(sites):
        raise ValueError('el listado repite VAs de sitios. Regenerar la caché')
    counts = Counter(target for _, target, *_ in sites)
    if counts != EXPECTED_COUNTS:
        raise ValueError('el listado no reproduce los conteos del c32bbd5f. '
                         'Regenerar la caché con objdump sin bytes crudos')


def main():
    try:
        with open(cg.cache_file(cg.SOURCE), encoding='utf-8') as source:
            validate_source(source.read())
        with open(cg.cache_file(cg.LISTING), encoding='utf-8') as listing:
            sites = scan_sites(listing)
        validate_census(sites)
    except (OSError, ValueError) as error:
        sys.exit(str(error))
    writer = csv.writer(sys.stdout, delimiter='\t', lineterminator='\n')
    writer.writerow(('site_va', 'target_va', 'familia', 'fase'))
    for site, target, family, phase in sites:
        writer.writerow(('0x%08x' % site, '0x%08x' % target, family, phase))


if __name__ == '__main__':
    main()
