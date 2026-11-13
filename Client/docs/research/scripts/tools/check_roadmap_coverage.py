#!/usr/bin/env python3
"""CL.M: verifica la matriz que asigna cada función de main.exe a una ola.

Reconcilia Client/docs/research/coverage/mainexe-roadmap.tsv con el inventario fijo del decompilado
(mismo SHA-256, mismas VAs, mismas columnas originales fila por fila) y valida las columnas nuevas:
clasificación, ola responsable, consumidoras, propósito, evidencia y los tres estados. Sin
--require-complete acepta filas pending explícitas; con él exige que no quede ninguna.

Uso:
    python3 -I Client/docs/research/scripts/tools/check_roadmap_coverage.py \\
        --inventory "$RESEARCH/client-research/mainexe-vmxp-c32bbd5f/decompiled/inventory.tsv" \\
        --mapping Client/docs/research/coverage/mainexe-roadmap.tsv [--require-complete]

Devuelve 0 si la matriz es válida y 1 si no; los errores van a stderr. Solo biblioteca estándar.
"""
import argparse
import collections
import hashlib
import re
import sys

INV_SHA256 = '42cc47579259c0184e3e921e93ac7817176093a31bb7fb07f2d77734f073194c'
INV_ROWS = 5648
NEW_COLUMNS = ['classification', 'primary_wave', 'consumer_waves', 'purpose', 'evidence',
               'analysis_state', 'implementation_state', 'validation', 'next_action']
CLASSIFICATIONS = {'game', 'library', 'thunk', 'excluded', 'pending'}
WAVES = {f'CL{i}' for i in range(12)}
ANALYSIS = {'pending', 'inferred', 'static', 'verified'}
IMPLEMENTATION = {'unknown', 'not-started', 'partial', 'implemented', 'not-applicable'}
EVIDENCE_REF = re.compile(r'(\.(asm|c|md|tsv|h|cpp)\b|\.(asm|c|md|tsv|h|cpp):\d+|https?://)')
OBSERVATION = re.compile(r'registro-niveles|traza|VM|golden|observ')


def read_tsv(path):
    with open(path, 'rb') as fh:
        raw = fh.read()
    text = raw.decode('utf-8')
    lines = text.split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    return raw, [ln.split('\t') for ln in lines]


def check(inventory, mapping, require_complete):
    errors = []
    raw, inv = read_tsv(inventory)
    if hashlib.sha256(raw).hexdigest() != INV_SHA256:
        errors.append(f'{inventory}: el SHA-256 no es el del inventario fijado ({INV_SHA256[:12]}…)')
    inv_header, inv_rows = inv[0], inv[1:]
    for r in inv_rows:
        r.extend([''] * (len(inv_header) - len(r)))
    if len(inv_rows) != INV_ROWS:
        errors.append(f'{inventory}: {len(inv_rows)} filas, se esperaban {INV_ROWS}')
    _, mp = read_tsv(mapping)
    header, rows = mp[0], mp[1:]
    if header != inv_header + NEW_COLUMNS:
        errors.append(f'{mapping}: las columnas no son las del inventario más {", ".join(NEW_COLUMNS)}')
        return errors, {}
    width = len(header)
    n_orig = len(inv_header)
    by_va = {}
    for i, r in enumerate(rows, 2):
        if len(r) != width:
            errors.append(f'{mapping}:{i}: {len(r)} campos, se esperaban {width}')
            continue
        if r[0] in by_va:
            errors.append(f'{mapping}:{i}: VA repetida {r[0]}')
        by_va[r[0]] = (i, r)
    inv_by_va = {r[0]: r for r in inv_rows}
    for va in sorted(set(inv_by_va) - set(by_va)):
        errors.append(f'{mapping}: falta la VA {va} del inventario')
    for va in sorted(set(by_va) - set(inv_by_va)):
        errors.append(f'{mapping}:{by_va[va][0]}: la VA {va} no está en el inventario')
    stats = collections.Counter()
    sizes = collections.Counter()
    for va, (i, r) in sorted(by_va.items()):
        if va in inv_by_va and r[:n_orig] != inv_by_va[va][:n_orig]:
            cols = [inv_header[k] for k in range(n_orig) if r[k] != inv_by_va[va][k]]
            errors.append(f'{mapping}:{i}: {va} cambia columnas del inventario: {", ".join(cols)}')
        row = dict(zip(NEW_COLUMNS, r[n_orig:]))
        cls, wave = row['classification'], row['primary_wave']
        where = f'{mapping}:{i} ({va})'
        if cls not in CLASSIFICATIONS:
            errors.append(f'{where}: clasificación inválida "{cls}"')
            continue
        consumers = [c for c in row['consumer_waves'].split(';') if c] if row['consumer_waves'] else []
        for c in consumers:
            if c not in WAVES:
                errors.append(f'{where}: ola consumidora inválida "{c}"')
        if len(consumers) != len(set(consumers)):
            errors.append(f'{where}: olas consumidoras repetidas')
        if wave and wave in consumers:
            errors.append(f'{where}: la ola responsable {wave} también figura como consumidora')
        if wave and wave not in WAVES:
            errors.append(f'{where}: ola inválida "{wave}"')
        if cls == 'game' and not wave:
            errors.append(f'{where}: una función de juego necesita una ola responsable')
        if cls in ('library', 'excluded') and (wave or consumers):
            errors.append(f'{where}: {cls} va sin ola ni consumidoras')
        if row['analysis_state'] not in ANALYSIS:
            errors.append(f'{where}: analysis_state inválido "{row["analysis_state"]}"')
        if row['implementation_state'] not in IMPLEMENTATION:
            errors.append(f'{where}: implementation_state inválido "{row["implementation_state"]}"')
        if not row['validation']:
            errors.append(f'{where}: validation vacía (vale "none" o cita un test u observación)')
        if cls != 'pending':
            if not row['purpose'].strip():
                errors.append(f'{where}: falta el propósito')
            if not EVIDENCE_REF.search(row['evidence']):
                errors.append(f'{where}: la evidencia no cita un archivo, un tramo ni un enlace')
            if row['analysis_state'] == 'pending':
                errors.append(f'{where}: una fila clasificada no puede tener analysis_state=pending')
            if cls in ('library', 'excluded', 'thunk') and row['implementation_state'] != 'not-applicable':
                errors.append(f'{where}: {cls} va con implementation_state=not-applicable')
        else:
            if not row['next_action'].strip():
                errors.append(f'{where}: una fila pending necesita la lectura que la resuelve (next_action)')
            if require_complete:
                errors.append(f'{where}: queda pendiente y se pidió --require-complete')
        if row['analysis_state'] == 'verified' and not OBSERVATION.search(row['evidence']):
            errors.append(f'{where}: verified exige citar la observación que supera la lectura estática')
        stats[cls] += 1
        sizes[cls] += int(r[1]) if r[1].isdigit() else 0
        if cls == 'game':
            stats[f'game:{wave}'] += 1
            sizes[f'game:{wave}'] += int(r[1])
    inv_total = sum(int(r[1]) for r in inv_rows if r[1].isdigit())
    if sum(sizes[c] for c in CLASSIFICATIONS) != inv_total:
        errors.append(f'{mapping}: la suma de tamaños ({sum(sizes[c] for c in CLASSIFICATIONS)}) no es la del '
                      f'inventario ({inv_total})')
    return errors, {'rows': len(by_va), 'stats': stats, 'sizes': sizes}


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--inventory', required=True)
    ap.add_argument('--mapping', required=True)
    ap.add_argument('--require-complete', action='store_true')
    args = ap.parse_args(argv)
    errors, info = check(args.inventory, args.mapping, args.require_complete)
    for e in errors[:200]:
        print(e, file=sys.stderr)
    if len(errors) > 200:
        print(f'… y {len(errors) - 200} errores más', file=sys.stderr)
    if info:
        st, sz = info['stats'], info['sizes']
        print(f'{info["rows"]} filas; ' + ', '.join(f'{c} {st[c]} ({sz[c]} B)' for c in sorted(CLASSIFICATIONS)))
        waves = sorted((k for k in st if k.startswith('game:')), key=lambda k: int(k[7:]))
        print('juego por ola: ' + ', '.join(f'{k[5:]} {st[k]} ({sz[k]} B)' for k in waves))
        print(f'pendientes: {st["pending"]}; funciones de juego sin ola: 0' if not errors else
              f'{len(errors)} errores')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main())
