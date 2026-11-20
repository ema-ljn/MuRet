r"""reference_summary.py: el resumen de una referencia del cliente regenerada, en Markdown.

Lo usa la action de la referencia (.github/workflows/referencia.yml) para la página de la corrida:

  python3 -I Client/docs/research/scripts/tools/reference_summary.py \
      --nueva "$RUNNER_TEMP/referencia" --commiteada Client/docs/research/coverage/referencia

Tres secciones: cuánto falta para portar (las cotas de metricas.tsv), cuánto falta para cerrarlo contra el ASM (las
funciones vivas que cumplen todo lo de closed_against_asm) y el estado de las trazas (cortes, huecos y la diferencia
con la referencia commiteada). Solo la biblioteca estándar. Las columnas, en coverage/referencia/LEEME.md.
"""
import argparse
import collections
import csv
import json
import pathlib
import sys

# Los campos largos de las tablas (los textos de funciones.tsv). Con tope en el long de C de 32 bits: en Windows,
# sys.maxsize tira OverflowError.
csv.field_size_limit(2**31 - 1)

# Lo que cambia en cada commit sin decir nada del avance.
VOLATILE = {'serie', 'muret'}
LONG_LIST = 20
TOP_HOLES = 10
WAVES = [f'CL{i}' for i in range(1, 12)]
# Lo vivo sin citar que no hace falta portar (build_client_reference.py, EXPL): no frena ni queda por cerrar.
EXPLAINED = ('clon', 'portada sin cita', 'plataforma', 'herramienta', 'STL', 'sin uso')


def load_tsv(path):
    with open(path, encoding='utf-8', newline='') as fh:
        return list(csv.DictReader(fh, delimiter='\t'))


def number(value):
    try:
        return int(value or 0)
    except ValueError:
        return 0


def live(row):
    return row.get('clase') == 'game' and row.get('viva') == 'sí'


def closed_against_asm(row):
    """Vivo, portado sin cortes ni citas pendientes, con test, sin pasos de VM y sin filas de 08 abiertas."""
    return (live(row) and row.get('estado') == 'implemented' and row.get('falso_positivo') != 'sí'
            and not row.get('cortes_vivos') and number(row.get('citas_pendiente')) == 0
            and number(row.get('citas_test')) > 0 and not row.get('pasos_vm') and not row.get('ids_08_abiertas'))


def closure_by_wave(funciones):
    counts = collections.defaultdict(collections.Counter)
    for row in funciones:
        if not live(row):
            continue
        size = number(row.get('bytes'))
        cited = row.get('estado') in ('implemented', 'partial')
        uncited = row.get('estado') in ('unknown', 'not-started')
        explained = uncited and (row.get('categoria_faltante') or '').startswith(EXPLAINED)
        for key in ('total', row.get('ola') or 'sin ola'):
            c = counts[key]
            c['funciones'] += 1
            c['bytes'] += size
            if closed_against_asm(row):
                c['cerradas'] += 1
                c['bytes_cerrados'] += size
            c['sin_citar'] += uncited and not explained
            if explained:
                c['explicadas'] += 1
                c['bytes_explicados'] += size
            c['con_cortes'] += bool(row.get('cortes_vivos'))
            c['cierran'] += row.get('cierra_el_cliente') not in ('', 'no', None)
            c['sin_test'] += cited and number(row.get('citas_test')) == 0
            c['con_vm'] += bool(row.get('pasos_vm'))
            c['con_08'] += bool(row.get('ids_08_abiertas'))
    result = {}
    for key, c in counts.items():
        result[key] = dict(funciones=c['funciones'], kb=c['bytes'] / 1024, cerradas=c['cerradas'],
                           kb_cerrados=c['bytes_cerrados'] / 1024, sin_citar=c['sin_citar'],
                           con_cortes=c['con_cortes'], cierran=c['cierran'], sin_test=c['sin_test'],
                           con_vm=c['con_vm'], con_08=c['con_08'], explicadas=c['explicadas'],
                           kb_explicados=c['bytes_explicados'] / 1024,
                           por_cerrar=c['funciones'] - c['explicadas'],
                           kb_por_cerrar=(c['bytes'] - c['bytes_explicados']) / 1024)
    result.setdefault('total', dict(funciones=0, kb=0.0, cerradas=0, kb_cerrados=0.0, sin_citar=0, con_cortes=0,
                                    cierran=0, sin_test=0, con_vm=0, con_08=0, explicadas=0, kb_explicados=0.0,
                                    por_cerrar=0, kb_por_cerrar=0.0))
    return result


def metric_deltas(new, old):
    before = {(r['ambito'], r['clave'], r['metrica']): r['valor'] for r in old}
    deltas = []
    for r in new:
        if r['ambito'] in VOLATILE:
            continue
        key = (r['ambito'], r['clave'], r['metrica'])
        if before.get(key) != r['valor']:
            deltas.append((*key, before.get(key), r['valor']))
    return deltas


def es(value, decimals=1):
    """Un número con la coma decimal."""
    if isinstance(value, float):
        return f'{value:,.{decimals}f}'.replace(',', ' ').replace('.', ',').replace(' ', '.')
    return f'{value:,}'.replace(',', '.')


def pct(part, whole):
    return '—' if not whole else es(100 * part / whole) + ' %'


def table(header, rows):
    lines = ['| ' + ' | '.join(header) + ' |', '|' + '|'.join(' --- ' for _ in header) + '|']
    shown = rows[:LONG_LIST]
    lines += ['| ' + ' | '.join(str(cell) for cell in row) + ' |' for row in shown]
    if len(rows) > len(shown):
        lines.append(f'\ny {len(rows) - len(shown)} más.')
    return '\n'.join(lines)


def metric_value(metrics, ambito, clave, metrica):
    for r in metrics:
        if (r['ambito'], r['clave'], r['metrica']) == (ambito, clave, metrica):
            return r['valor'].replace('.', ',')
    return '—'


def porting_section(metrics):
    names = [('funciones_vivas', 'Funciones vivas'), ('kb_vivos', 'KB vivos'), ('cota_alta', 'Cota alta %'),
             ('cota_media', 'Cota media %'), ('cota_baja', 'Cota baja %'), ('cota_baja_con_test', 'Baja con test %'),
             ('falta_cota_baja', 'Falta (KB)')]
    rows = [['**Total**'] + [metric_value(metrics, 'avance', 'total', m) for m, _ in names]]
    for wave in WAVES:
        values = [metric_value(metrics, 'avance-ola', wave, m) for m, _ in names]
        if any(v != '—' for v in values):
            rows.append([wave] + values)
    return '\n'.join(['## Cuánto falta para portar', '',
                      'Las cotas de la matriz CL.M: lo citado sin cortes (baja), más la mitad o todo lo que nadie '
                      'clasificó (media y alta). "Falta" son los KB vivos fuera de la cota baja.', '',
                      table(['Ola'] + [n for _, n in names], rows)])


def closure_section(funciones):
    waves = closure_by_wave(funciones)
    keys = ['total'] + [w for w in WAVES if w in waves] + sorted(k for k in waves if k not in WAVES and k != 'total')
    rows = []
    for key in keys:
        w = waves[key]
        rows.append(['**Total**' if key == 'total' else key, es(w['funciones']), es(w['explicadas']),
                     es(w['por_cerrar']), es(w['cerradas']), pct(w['cerradas'], w['por_cerrar']),
                     f"{es(w['kb_cerrados'])} de {es(w['kb_por_cerrar'])}", pct(w['kb_cerrados'], w['kb_por_cerrar']),
                     es(w['sin_citar']), es(w['con_cortes']), es(w['cierran']), es(w['sin_test']), es(w['con_vm']),
                     es(w['con_08'])])
    header = ['Ola', 'Vivas', 'Explicadas', 'Por cerrar', 'Cerradas', '% funciones', 'KB cerrados', '% KB',
              'Sin citar', 'Con cortes', 'Cierran el cliente', 'Sin test', 'Con VM', 'Con 08']
    return '\n'.join(['## Cuánto falta para cerrarlo contra el ASM', '',
                      'Cerrada es una función viva portada (`implemented`) sin falso positivo, sin cortes vivos, sin '
                      'citas pendientes, con al menos un test, sin pasos de VM y sin filas de 08 abiertas. Mide '
                      'citas, tests, cortes, VM y 08, no la fidelidad byte a byte: citar no es portar fiel '
                      '(LEEME.md, Límites). Las explicadas (clones, portadas sin cita, plataforma, herramientas, STL y sin uso) no hace falta portarlas y no entran en la base: los % son sobre las que quedan por cerrar. Las demás columnas cuentan lo que frena a las que no lo están.', '',
                      table(header, rows)])


def traces_section(metrics, committed, cortes, huecos):
    out = ['## El estado de las trazas', '']
    by_class = [[r['clave'], r['valor']] for r in metrics if r['ambito'] == 'cortes' and r['metrica'] == 'sitios']
    out += ['### Cortes vivos', '', table(['Clase', 'Sitios'], by_class) if by_class else 'Ninguno.', '']
    closing = [[f"`{r['archivo']}:{r['línea']}`", r.get('función', ''), r.get('clase', ''), r['cierra_el_cliente']]
               for r in cortes if r.get('cierra_el_cliente') not in ('', 'no', None)]
    out += ['### Los que cierran el cliente jugando', '',
            table(['Dónde', 'Función', 'Clase', 'Cómo'], closing) if closing else 'Ninguno.', '']
    hole_metrics = [[r['clave'], r['metrica'], r['valor'].replace('.', ',')] for r in metrics if r['ambito'] == 'huecos']
    biggest = sorted(huecos, key=lambda r: number(r.get('bytes')), reverse=True)[:TOP_HOLES]
    out += ['### Huecos (código vivo sin citar ni explicar)', '']
    if hole_metrics:
        out += [table(['Señal', 'Métrica', 'Valor'], hole_metrics), '']
    if biggest:
        out += [table(['Hueco', 'Desde', 'Hasta', 'Funciones', 'Bytes', 'Ola', 'Señal'],
                      [[r['hueco'], r['desde'], r['hasta'], r['funciones'], es(number(r['bytes'])), r['ola'],
                        r['senal']] for r in biggest]), '']
    else:
        out += ['Ninguno.', '']
    deltas = metric_deltas(metrics, committed)
    out += ['### Contra la referencia commiteada en la rama', '']
    if deltas:
        out += ['Difiere: hay que regenerarla y commitearla (LEEME.md, Cómo regenerarla).', '',
                table(['Ámbito', 'Clave', 'Métrica', 'Commiteada', 'Nueva'],
                      [[a, k, m, '—' if o is None else o.replace('.', ','), n.replace('.', ',')]
                       for a, k, m, o, n in deltas])]
    else:
        out += ['La referencia commiteada está al día.']
    return '\n'.join(out)


def render(new_dir, committed_dir):
    new_dir, committed_dir = pathlib.Path(new_dir), pathlib.Path(committed_dir)
    metrics = load_tsv(new_dir / 'metricas.tsv')
    committed_file = committed_dir / 'metricas.tsv'
    committed = load_tsv(committed_file) if committed_file.exists() else []
    title = '# La referencia del cliente'
    generated = new_dir / 'generado.json'
    if generated.exists():
        info = json.loads(generated.read_text(encoding='utf-8'))
        title += f" sobre `{str(info.get('ref', ''))[:8]}` ({info.get('fecha_commit', '')})"
    return '\n\n'.join([title, porting_section(metrics), closure_section(load_tsv(new_dir / 'funciones.tsv')),
                        traces_section(metrics, committed, load_tsv(new_dir / 'cortes.tsv'),
                                       load_tsv(new_dir / 'huecos.tsv'))]) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--nueva', required=True, help='la carpeta de la referencia regenerada')
    parser.add_argument('--commiteada', required=True, help='la carpeta de la referencia commiteada en la rama')
    args = parser.parse_args()
    sys.stdout.write(render(args.nueva, args.commiteada))


if __name__ == '__main__':
    main()
