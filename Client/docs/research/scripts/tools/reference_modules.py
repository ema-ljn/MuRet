"""Which client module each file, function, cut, gap, VM step and 08 row of the reference belongs to, and the
per-module totals of metricas.tsv. Modules come from Client/modules.tsv and coverage/module-map.tsv, read through
check_layers.py so the reference and the layer check agree. Standard library only.
"""
import collections
import csv
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[4] / 'tools' / 'architecture'))
from check_layers import module_map_from_rows, module_of, modules_from_rows  # noqa: E402

UNASSIGNED = 'unassigned'
METRICS = (('lines', 'lines'), ('cited_functions', 'func'), ('cited_kb', 'KB'), ('live_cuts', 'sites'),
           ('closing_cuts', 'sites'), ('gaps', 'blocks'), ('gap_kb', 'KB'), ('vm_steps', 'steps'),
           ('open_08_ids', 'rows'))


def _rows(text):
    return list(csv.DictReader(text.splitlines(), delimiter='\t', quoting=csv.QUOTE_NONE))


def load(modules_text, map_text):
    """The module map of a commit, from the text of both tables; None when the commit lacks either."""
    if not modules_text or not map_text:
        return None
    return module_map_from_rows(_rows(map_text), modules_from_rows(_rows(modules_text)))


def module_of_file(path, module_map):
    """The module of a file of Client/src (path relative to it), or '' when the map does not place it."""
    return (module_of(path, module_map) or '') if module_map else ''


def majority(modules):
    """The most frequent non-empty module; a tie goes to the first in alphabetical order."""
    counts = collections.Counter(m for m in modules if m)
    return min(counts, key=lambda m: (-counts[m], m)) if counts else ''


def module_of_citations(files, module_map):
    """A cited function's module: that of the files citing it, one vote per citation."""
    return majority(module_of_file(f, module_map) for f in files)


def module_of_callers(callers, caller_module):
    """An uncited function's or a gap's module: that of its cited callers, one vote per caller."""
    return majority(caller_module.get(c, '') for c in set(callers))


def module_totals(funciones, cortes, huecos, archivos, vm_steps, open_08_ids):
    """The module metrics, per module; rows without one add up under UNASSIGNED, so each metric sums to its total.

    vm_steps and open_08_ids are the module of each VM step and of each open 08 row."""
    totals = collections.defaultdict(lambda: dict.fromkeys((m for m, _ in METRICS), 0))

    def at(module):
        return totals[module or UNASSIGNED]

    for r in archivos:
        at(r['module'])['lines'] += int(r['lineas'])
    for r in funciones:
        if r['viva'] == 'sí' and r['estado'] in ('implemented', 'partial'):
            at(r['module'])['cited_functions'] += 1
            at(r['module'])['cited_kb'] += int(r['bytes'] or 0) / 1024
    for r in cortes:
        at(r['module'])['live_cuts'] += bool(r['archivo'])
        at(r['module'])['closing_cuts'] += r['cierra_el_cliente'].startswith('sí')
    for r in huecos:
        at(r['module'])['gaps'] += 1
        at(r['module'])['gap_kb'] += int(r['bytes']) / 1024
    for module in vm_steps:
        at(module)['vm_steps'] += 1
    for module in open_08_ids:
        at(module)['open_08_ids'] += 1
    for t in totals.values():
        t['cited_kb'] = round(t['cited_kb'], 1)
        t['gap_kb'] = round(t['gap_kb'], 1)
    return dict(totals)
