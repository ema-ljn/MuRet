"""consultar.py: preguntas frecuentes sobre la referencia, sin abrir los TSV a mano.

  python3 -I consultar.py 0x5252e6          una VA (o el inicio de una función): la función, dónde está en MuRet,
                                            tests, cortes, 07/08 y llamadores
  python3 -I consultar.py World/GameModelEffects.cpp   un archivo de Client/src: qué funciones del original cita
  python3 -I consultar.py P-C232            una fila de 07 u 08 y el estado de sus funciones
  python3 -I consultar.py faltan CL9        lo vivo sin citar de una ola, de mayor a menor
  python3 -I consultar.py cierran           los cortes que cierran el cliente jugando
  python3 -I consultar.py buscar StepDebris un símbolo o un texto de MuRet: las VA que cita
  python3 -I consultar.py huecos [CL5] [30] los bloques sin citar ni explicar, de mayor a menor (de una ola)
  python3 -I consultar.py module game/Guild a module (or unassigned): its metrics, live cuts, gaps and their callers,
                                            VM steps and open 08 rows

Lee los TSV de la carpeta del script. Sólo biblioteca estándar.
"""
import bisect
import csv
import os
import sys

D = os.path.dirname(os.path.abspath(__file__))


def tsv(name):
    with open(os.path.join(D, name), encoding='utf-8') as fh:
        return list(csv.DictReader(fh, delimiter='\t'))


def show(r, keys):
    for k in keys:
        if r.get(k):
            print(f'  {k:20} {r[k]}')


def by_va(arg):
    F = tsv('funciones.tsv')
    starts = [int(r['va'], 16) for r in F]
    x = int(arg, 16)
    i = bisect.bisect_right(starts, x) - 1
    f = F[i]
    nxt = starts[i + 1] if i + 1 < len(starts) else 1 << 40
    if x >= nxt:
        print('no cae en ninguna función')
        return
    dentro = 'dentro de su tamaño' if x < int(f['fin'], 16) else 'después de su tamaño, antes de la siguiente'
    print(f"{arg} está en {f['va']} ({f['bytes']} B, {dentro})")
    show(f, ['clase', 'viva', 'ola', 'sub_ola', 'hito_cl9', 'estado', 'hecho', 'categoria_faltante', 'falso_positivo',
             'clon_de', 'chat', 'archivos_src', 'simbolos_muret', 'tests', 'casos_de_test', 'doc_research',
             'cortes_vivos', 'cierra_el_cliente', 'ids_07', 'ids_08_abiertas', 'pasos_vm', 'llamadores',
             'llamadores_citados', 'senal', 'hueco', 'proposito', 'pseudocodigo'])
    T = [r for r in tsv('trazas.tsv') if r['funcion'] == f['va']]
    exact = [r for r in T if int(r['va'], 16) == x]
    print(f"\n  {len(T)} citas de la función en src y tests ({len(exact)} de {arg} exacta):")
    for r in (exact or T)[:25]:
        print(f"    {r['tipo']:6} {r['archivo'].removeprefix('Client/')}:{r['linea']}  [{r['simbolo']}]  "
              f"{r['texto'][:90]}")
    C = [r for r in tsv('cortes.tsv') if r['función'] == f['va']]
    if C:
        print(f'\n  {len(C)} cortes vivos:')
        for r in C[:15]:
            print(f"    {r['clase']} {r['archivo']}:{r['línea']}  {r['qué_falta'][:90]}")


def by_file(arg):
    A = [r for r in tsv('archivos.tsv') if r['archivo'].endswith(arg)]
    for a in A:
        print(a['archivo'])
        show(a, ['lineas', 'citas', 'citas_corte', 'funciones', 'kb_funciones', 'ola_principal', 'olas', 'cortes_vivos',
                 'cierran', 'tests', 'ultimo_cambio', 'ultimo_commit'])
        T = [r for r in tsv('trazas.tsv') if r['archivo'] == 'Client/src/' + a['archivo'] and r['funcion']]
        fn = {}
        for r in T:
            fn.setdefault(r['funcion'], []).append(r['simbolo'])
        F = {r['va']: r for r in tsv('funciones.tsv')}
        print('  funciones que cita:')
        for va in sorted(fn, key=lambda v: int(v, 16)):
            f = F.get(va, {})
            syms = sorted({s for s in fn[va] if s})
            print(f"    {va} {f.get('bytes', ''):>6} B {f.get('ola', ''):5} {f.get('estado', ''):12} "
                  f"{', '.join(syms)[:70]}")


def by_id(arg):
    F = {r['va']: r for r in tsv('funciones.tsv')}
    for r in tsv('ids.tsv'):
        if r['id'] == arg:
            print(f"{r['id']} ({r['doc']}:{r['linea']}) {r['estado']}, {r['clase_o_tipo']}, {r['ola']}")
            print(f"  {r['titulo']}")
            if r['muret']:
                print(f"  MuRet: {r['muret']}")
            for va in r['funciones'].split():
                f = F.get(va, {})
                print(f"  {va} {f.get('ola', ''):5} {f.get('estado', ''):12} {f.get('archivos_src', '')[:80]}")


def missing(wave):
    F = [r for r in tsv('funciones.tsv') if r['viva'] == 'sí' and r['ola'] == wave
         and (r['estado'] in ('unknown', 'not-started') or r['falso_positivo'])]
    F.sort(key=lambda r: -int(r['bytes']))
    print(f'{len(F)} funciones, {sum(int(r["bytes"]) for r in F) / 1024:.1f} KB')
    for r in F:
        print(f"  {r['va']} {r['bytes']:>6} B {r['sub_ola']:7} {r['categoria_faltante'][:60]:60} {r['proposito'][:50]}")


def closing():
    for r in tsv('cortes.tsv'):
        if r['cierra_el_cliente'].startswith('sí'):
            print(f"{r['clase']} {r['archivo']}:{r['línea']} ({r['función']}, {r['ola']}): {r['cierra_el_cliente']}")
            print(f"    falta: {r['qué_falta'][:110]}")
            print(f"    cómo: {r['cómo_se_llega'][:110]}")


def search(text):
    seen = {}
    for r in tsv('trazas.tsv'):
        if text in r['simbolo'] or text in r['texto']:
            seen.setdefault((r['funcion'], r['simbolo']), []).append(f"{r['archivo'].removeprefix('Client/')}:"
                                                                     f"{r['linea']}")
    for (fn, sym), where in sorted(seen.items()):
        print(f'  {fn:9} [{sym}] {", ".join(where[:4])}{" …" if len(where) > 4 else ""}')


def gaps(args):
    wave = next((x.upper() for x in args if x.upper().startswith('CL')), '')
    n = next((int(x) for x in args if x.isdigit()), 30)
    H = [h for h in tsv('huecos.tsv') if not wave or h['ola'] == wave]
    print(f"{len(H)} bloques, {sum(int(h['bytes']) for h in H) / 1024:.1f} KB")
    for h in H[:n]:
        print(f"{h['hueco']} {h['desde']}..{h['hasta']} {h['bytes']:>6} B {h['funciones']:>3} func {h['ola']:5} "
              f"{h['senal']}; {h['categorias'][:60]}")
        if h['entradas']:
            print(f"      la llaman: {h['entradas'][:110]}")
        if h['textos']:
            print(f"      textos: {h['textos'][:110]}")


def module(name):
    metrics = [r for r in tsv('metricas.tsv') if r['ambito'] == 'module' and r['clave'] == name]
    if not metrics:
        print(f'no module {name} in metricas.tsv')
        return
    key = '' if name == 'unassigned' else name
    print(name)
    for r in metrics:
        print(f"  {r['metrica']:16} {r['valor']} {r['unidad']}")
    cuts = [r for r in tsv('cortes.tsv') if r['module'] == key and r['archivo']]
    print(f'\n  {len(cuts)} live cuts:')
    for r in cuts[:30]:
        closes = ' (closes the client)' if r['cierra_el_cliente'].startswith('sí') else ''
        print(f"    {r['clase']} {r['archivo']}:{r['línea']}{closes}  {r['qué_falta'][:80]}")
    holes = [h for h in tsv('huecos.tsv') if h['module'] == key]
    print(f'\n  {len(holes)} gaps:')
    for h in holes[:20]:
        print(f"    {h['hueco']} {h['desde']}..{h['hasta']} {h['bytes']:>6} B {h['funciones']:>3} func  {h['senal']}")
        if h['entradas']:
            print(f"      called by: {h['entradas'][:100]}")
    steps = [r for r in tsv('vm.tsv') if r['module'] == key]
    print(f'\n  {len(steps)} VM steps:')
    for r in steps[:20]:
        print(f"    {r['fuente']}#{r['paso']} {r['titulo'][:90]}")
    rows = {r['id']: r for r in tsv('ids.tsv')
            if r['module'] == key and r['id'].startswith('P-') and r['estado'] != 'cerrada'}
    print(f'\n  {len(rows)} open 08 rows:')
    for r in list(rows.values())[:20]:
        print(f"    {r['id']} {r['titulo'][:90]}")


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
    elif a[0].lower().startswith('0x'):
        by_va(a[0])
    elif a[0] in ('faltan', 'falta') and len(a) > 1:
        missing(a[1].upper())
    elif a[0] in ('cierran', 'cierra'):
        closing()
    elif a[0] == 'huecos':
        gaps(a[1:])
    elif a[0] == 'module' and len(a) > 1:
        module(a[1])
    elif a[0] == 'buscar' and len(a) > 1:
        search(' '.join(a[1:]))
    elif a[0][:2] in ('P-', 'Q-'):
        by_id(a[0])
    else:
        by_file(a[0])


if __name__ == '__main__':
    main()
