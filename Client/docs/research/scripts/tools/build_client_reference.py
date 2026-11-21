#!/usr/bin/env python3
"""build_client_reference.py: arma la referencia del cliente (funciones, trazas, huecos, métricas) para un commit.

Se corre desde la raíz de MuRet después de build_roadmap_matrix.py (ver coverage/referencia/LEEME.md):

  python3 -I Client/docs/research/scripts/tools/build_roadmap_matrix.py --research R --ref C \
      --out /tmp/m.tsv --summary /tmp/s.json --rows /tmp/filas.tsv
  python3 -I Client/docs/research/scripts/tools/build_client_reference.py --research R --ref C \
      --matriz /tmp/m.tsv --filas /tmp/filas.tsv

Salidas (en --out, por defecto Client/docs/research/coverage/referencia):
  funciones.tsv    una fila por función del main.exe c32bbd5f (5.648): ola, estado, dónde está en MuRet, tests,
                   cortes, 07/08, VM, llamadores y lo que dijo la auditoría
  trazas.tsv       cada cita de una VA en Client/src y Client/tests, con la función que la contiene
  trazas-docs.tsv  lo mismo en Client/docs/research y docs/ (sin los TSV derivados)
  archivos.tsv     cada archivo de Client/src: qué funciones cita, de qué ola, sus cortes y su último commit
  ids.tsv          cada fila Q-C de 07 y P-C de 08: estado, clase u ola, y las funciones que nombra
  huecos.tsv       los bloques contiguos de funciones vivas sin citar ni explicar, con quién las llama desde código
                   citado y los textos de su pseudocódigo: dónde buscar lo que falta portar
  metricas.tsv     los números en formato largo (ref, ámbito, clave, métrica, valor), con la serie

Entradas:
  --muret     checkout de MuRet con historia (por defecto, la carpeta actual)
  --ref       commit de MuRet que se mide
  --research  clon de EmaNLJN/mu-server-repack-research-102i (el grafo de llamadas y el pseudocódigo)
  --matriz    la matriz CL.M regenerada en --ref (build_roadmap_matrix.py --out)
  --filas     el avance por fila de esa corrida (build_roadmap_matrix.py --rows)
  --entradas  carpeta con lo que hizo la auditoría del 2026-11-20 a mano (por defecto, referencia/entradas): triage.tsv, gaps.tsv, cortes-vivos.tsv,
              chat-funciones.tsv, numeros-chat.json, pendientes-08.tsv, vm.tsv, quirks-07.tsv, sustitutos.tsv,
              sin-cablear.tsv y serie/frozen-*.json

Sólo biblioteca estándar. Correr con python3 -I.
"""
import argparse
import bisect
import collections
import csv
import gzip
import json
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from client_reference_symbols import symbols  # noqa: E402
from reference_anchors import cut_sites, resolve_entries  # noqa: E402

REF_DIR = 'Client/docs/research/coverage/referencia'
# Los scripts de la referencia no son fuente de citas (tienen VA en sus tablas a mano).
SELF = {'Client/docs/research/scripts/tools/build_client_reference.py',
        'Client/docs/research/scripts/tools/client_reference_symbols.py',
        'Client/docs/research/scripts/tools/reference_anchors.py',
        'Client/docs/research/scripts/tools/test_reference_anchors.py'}
STRING = re.compile(r'"((?:[^"\\\n]|\\.){3,})"')

ADDR = re.compile(r'0[xX](?:00)?([4-6][0-9a-fA-F]{5})\b')
# La misma regex de cortes que build_roadmap_matrix.py: una línea así no cuenta como portada.
CUT = re.compile(r'cut\(|Unexpected\(|\bCL ?(9|10|11)\b|\bCL(9|10|11)\.|es de la CL|es del hito|\bcorte\b|'
                 r'cortad|sin portar|no se porta|queda para|llegan? con|\bstub\b|gancho|'
                 r'\btodavía no\b|\bMuRet no\b|\bno (?:lo |la |los |las )?porta', re.I)
# Citas que no son cortes pero dicen que la pieza no está: falsos positivos de "citada".
PEND = re.compile(r'no (?:está|están|estaba) portad|\bno portad|no est[áa] hech|sin hacer|son del área de mensajes',
                  re.I)
TEXT_START, TEXT_END = 0x401000, 0x6B8000
CLASES = 'POVXRSGDIH'
EXT = ('.h', '.cpp', '.inc', '.hpp', '.md', '.txt', '.py')


def git(repo, *args):
    return subprocess.run(['git', '-C', repo, *args], check=True, capture_output=True).stdout.decode('utf-8', 'replace')


def cat_files(repo, ref, paths):
    """Lee muchos archivos de un commit con un solo git cat-file --batch."""
    p = subprocess.Popen(['git', '-C', repo, 'cat-file', '--batch'], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    out, _ = p.communicate(''.join(f'{ref}:{x}\n' for x in paths).encode())
    res, pos = {}, 0
    for path in paths:
        nl = out.index(b'\n', pos)
        head = out[pos:nl].split()
        if len(head) < 3 or head[1] == b'missing':
            pos = nl + 1
            continue
        size = int(head[2])
        res[path] = out[nl + 1:nl + 1 + size].decode('utf-8', 'replace')
        pos = nl + 1 + size + 1
    return res


def tsv(path):
    with open(path, encoding='utf-8') as fh:
        return list(csv.DictReader(fh, delimiter='\t'))


def write_tsv(path, cols, rows):
    with open(path, 'w', encoding='utf-8', newline='') as fh:
        w = csv.writer(fh, delimiter='\t', lineterminator='\n', quoting=csv.QUOTE_MINIMAL)
        w.writerow(cols)
        for r in rows:
            w.writerow([clean(r.get(c, '')) for c in cols])


def clean(v, n=240):
    if isinstance(v, float):
        v = f'{v:.4f}'.rstrip('0').rstrip('.') if v else '0'
    v = str(v).replace('\t', ' ').replace('\r', ' ').replace('\n', ' ')
    return v if len(v) <= n else v[:n - 1] + '…'


def hx(a):
    return f'0x{a:x}'


def pct(x, y):
    return round(100 * x / y, 1) if y else ''


class Funcs:
    def __init__(self, inv):
        self.size = {int(r['address'], 16): int(r['size_bytes'] or 0) for r in inv}
        self.starts = sorted(self.size)

    def find(self, x):
        """La función que contiene x: ('sí' si cae en su tamaño, 'cola' si cae antes de la siguiente)."""
        if not TEXT_START <= x < TEXT_END:
            return None, ''
        i = bisect.bisect_right(self.starts, x) - 1
        if i < 0:
            return None, ''
        s = self.starts[i]
        if x < s + max(self.size[s], 1):
            return s, 'sí'
        return s, 'cola'


def where(line, pos):
    """Dónde está la VA en la línea: comentario, cadena o código (aproximado)."""
    c = line.find('//')
    if 0 <= c < pos or line.lstrip().startswith(('*', '/*')):
        return 'comentario'
    if line[:pos].count('"') % 2 == 1:
        return 'cadena'
    return 'código'


def area_of(path):
    if path.startswith('Client/src/'):
        return 'src'
    if path.startswith('Client/tests/'):
        return 'tests'
    if path.startswith('Client/docs/research/'):
        return 'docs-research'
    return 'docs'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--muret', default='.')
    ap.add_argument('--ref', required=True)
    ap.add_argument('--research', required=True)
    ap.add_argument('--matriz', required=True)
    ap.add_argument('--filas', required=True)
    ap.add_argument('--entradas', default=os.path.join(REF_DIR, 'entradas'))
    ap.add_argument('--out', default=REF_DIR)
    ap.add_argument('--guardar-serie', action='store_true',
                    help='guarda el avance total de esta corrida en entradas/serie/frozen-<ref>nm.json')
    a = ap.parse_args()
    E = a.entradas
    ref = git(a.muret, 'rev-parse', a.ref).strip()
    short = ref[:8]
    fecha = git(a.muret, 'show', '-s', '--format=%cs', ref).strip()

    # --- inventario: la matriz congelada del commit (olas) y la regenerada (estados) ---
    files = [f for f in git(a.muret, 'ls-tree', '-r', '--name-only', ref).split('\n') if f]
    frozen_txt = cat_files(a.muret, ref, ['Client/docs/research/coverage/mainexe-roadmap.tsv'])
    frozen = list(csv.DictReader(frozen_txt['Client/docs/research/coverage/mainexe-roadmap.tsv'].splitlines(),
                                 delimiter='\t'))
    FZ = {int(r['address'], 16): r for r in frozen}
    NEW = {int(r['address'], 16): r for r in tsv(a.matriz)}
    F = Funcs(frozen)
    ROWS = {}
    with open(a.filas) as fh:
        for line in fh:
            if line.startswith('va\t'):
                continue
            va, size, st, w, ncut, nimp, left, later = line.rstrip('\n').split('\t')
            ROWS[int(va, 16)] = dict(size=int(size), st=st, wave=w, ncut=int(ncut), nimp=int(nimp),
                                     left=float(left), later=later)

    # --- planes de la CL8 y la CL9 (sub-ola, hito) ---
    plans = cat_files(a.muret, ref, ['docs/superpowers/plans/2026-11-13-ola-cl9-funciones.tsv',
                                     'docs/superpowers/plans/2026-11-12-ola-cl8-funciones.tsv'])
    CL9 = {}
    for r in csv.DictReader(plans.get('docs/superpowers/plans/2026-11-13-ola-cl9-funciones.tsv', '').splitlines(),
                            delimiter='\t'):
        CL9[int(r['va'], 16)] = r
    CL8 = {}
    for r in csv.DictReader(plans.get('docs/superpowers/plans/2026-11-12-ola-cl8-funciones.tsv', '').splitlines(),
                            delimiter='\t'):
        subs = sorted(set(re.findall(r'CL8\.\d', r.get('olas_por_lector', ''))))
        CL8[int(r['va'], 16)] = ' '.join(subs)

    # --- citas: src, tests y docs ---
    scan = [f for f in files if f.startswith(('Client/', 'docs/')) and f.endswith(EXT)
            and not f.endswith('Input/Key.h') and not f.startswith('Client/docs/research/coverage/')
            and not f.startswith('Client/assets/') and f not in SELF]
    texts = cat_files(a.muret, ref, scan)
    traz, traz_docs = [], []
    src_lines = {}
    sites = []
    for f in scan:
        text = texts.get(f)
        if text is None:
            continue
        ar = area_of(f)
        lines = text.split('\n')
        if ar == 'src':
            src_lines[f] = len(lines)
        sym = symbols(lines) if ar in ('src', 'tests') and f.endswith(('.h', '.cpp', '.inc', '.hpp')) else None
        if ar == 'src' and sym is not None:
            sites += cut_sites(f.removeprefix('Client/src/'), text, sym)
        for i, line in enumerate(lines, 1):
            ms = list(ADDR.finditer(line))
            if not ms:
                continue
            cut = bool(CUT.search(line))
            pend = (not cut) and bool(PEND.search(line))
            seen = set()
            for m in ms:
                va = int(m.group(1), 16)
                if va in seen:
                    continue
                seen.add(va)
                fn, dentro = F.find(va)
                seccion = 'código' if TEXT_START <= va < TEXT_END else ('datos' if va >= TEXT_END else 'cabecera')
                if ar == 'src':
                    tipo = 'corte' if cut else 'impl'
                elif ar == 'tests':
                    tipo = 'test-corte' if cut else 'test'
                else:
                    tipo = 'doc'
                row = dict(va=hx(va), funcion=hx(fn) if fn is not None else '', dentro=dentro, seccion=seccion,
                           area=ar, archivo=f, linea=i, tipo=tipo, pendiente='sí' if pend else '',
                           en=where(line, m.start()) if ar in ('src', 'tests') else '',
                           simbolo=sym[i - 1] if sym else '',
                           texto=line.strip(), _fn=fn)
                (traz if ar in ('src', 'tests') else traz_docs).append(row)

    by_fn = collections.defaultdict(list)
    for r in traz:
        if r['_fn'] is not None:
            by_fn[r['_fn']].append(r)
    docs_fn = collections.defaultdict(list)
    for r in traz_docs:
        if r['_fn'] is not None:
            docs_fn[r['_fn']].append(r)

    # --- grafo de llamadas ---
    callers, callees = collections.defaultdict(set), collections.defaultdict(set)
    gdir = os.path.join(a.research, 'client-research/analysis/mainexe/evidence/coverage/grafo-2f16a9c6')
    with gzip.open(os.path.join(gdir, 'edges.txt.gz'), 'rt') as fh:
        for line in fh:
            p = line.rstrip('\n').split('|')
            if len(p) == 3 and p[2] == 'C' and re.fullmatch(r'[0-9a-f]{8}', p[1]):
                s, d = int(p[0], 16), int(p[1], 16)
                if s != d:
                    callers[d].add(s)
                    callees[s].add(d)

    # --- los textos del pseudocódigo de cada función ---
    ddir = os.path.join(a.research, 'client-research/mainexe-vmxp-c32bbd5f/decompiled/functions')

    def pseudo_strings(base):
        path = os.path.join(ddir, base + '.c')
        if not base or not os.path.exists(path):
            return ''
        with open(path, encoding='utf-8', errors='replace') as fh:
            found = []
            for t in STRING.findall(fh.read()):
                t = t.strip()
                if t and t not in found and any(ch.isalpha() for ch in t):
                    found.append(t[:60])
        return ' | '.join(found[:4])

    # --- lo que hizo la auditoría a mano ---
    TRI = {int(r['va'], 16): r for r in tsv(f'{E}/triage.tsv')}
    GAP = {int(r['va'], 16): r for r in tsv(f'{E}/gaps.tsv')}
    # Faltas que la auditoría confirmó en funciones sin citar: valen mientras sigan sin citar (cuando alguien las
    # porta y las cita, la fila pasa sola a implemented).
    OVERRIDE_CAT = {
        0x4d4020: 'falta: el C3 29 (tiempo de la bebida), MuRet no registra el 0x29'}
    # Falsos positivos de la cota: citadas, pero lo que las cita no las porta. Con las citas de implementación que
    # tenían en la auditoría: si aparecen más, alguien las portó y la marca se cae (hay que revisarla a mano).
    # (0x65dc30 y 0x65dd70, el cartel del nombre del mapa, salieron al portarlo: Scenes/MapBanner, 07 Q-C1829.)
    FALSE_POSITIVE = {
        0x59eb90: (5, 'falta: elegir los objetos del mapa con el mouse; las citas dicen que no está portada (falso '
                      'positivo de la cota)')}
    # Los globos de la selección de personaje (los nameplates): la clase de vtable 0x6ba7d0 (el dueño, en UI + 0x8dc8)
    # y sus cinco elementos de 0xf0 bytes (vtable 0x6ba7cc). La P-C27 (b) los anotó; la auditoría los dejó entre las
    # chicas sin clasificar. Encontrados por el hilo de los nameplates (2026-11-20).
    # Portados (#383, UI/CharacterBalloons): los que hacen algo quedan citados y pasan solos a implemented. Los
    # constructores y destructores de C++ (poner la vtable, llamar al de la base, el delete escalar o del arreglo) no
    # tienen cita: en MuRet los cubre CharacterBalloons con RAII y Release (0x63ce50). Mientras el resto de los globos
    # no esté citado, siguen como falta.
    balloons = 'los globos de la selección de personaje (nameplates, P-C27 b)'
    raii = ROWS.get(0x63ce50, {}).get('st') in ('implemented', 'partial') and \
        ROWS.get(0x63ce70, {}).get('st') in ('implemented', 'partial')
    for va, what, cxx in (
            (0x63c940, 'constructor de un globo', True), (0x63c990, 'destructor de un globo', True),
            (0x63c9a0, 'arma un globo con character_ex.tga', False), (0x63ca20, 'dibuja un globo', False),
            (0x63cc60, 'copia el nombre, el rango del gremio y la clase y el nivel de un registro', False),
            (0x63ce00, 'constructor del dueño', True), (0x63ce10, 'destructor del dueño (con delete)', True),
            (0x63ce40, 'destructor del dueño', True), (0x63ce50, 'libera los cinco globos', False),
            (0x63ce70, 'crea los cinco globos', False), (0x63cf20, 'destructor del arreglo de globos', True),
            (0x63cf80, 'dibuja los cinco, desde 0x65fe00', False), (0x63cfb0, 'copia los cinco registros', False)):
        if cxx and raii:
            OVERRIDE_CAT[va] = (f'portada sin cita: {balloons}: {what}; lo cubren CharacterBalloons (RAII) y '
                                f'Release (0x63ce50)')
        else:
            OVERRIDE_CAT[va] = f'falta: {balloons}: {what}'

    CHAT, CHAT_TRAMOS = {}, collections.defaultdict(list)
    for r in tsv(f'{E}/chat-funciones.tsv'):
        va = int(r['va'], 16)
        if r['fin'].strip():
            fn, _ = F.find(va)
            if fn is not None:
                CHAT_TRAMOS[fn].append(r)
        else:
            CHAT[va] = r
    # Each hand row takes the cut with its anchor, wherever it is in this commit (reference_anchors.py).
    CORTES = resolve_entries([r for r in tsv(f'{E}/cortes-vivos.tsv')
                              if not r['origen'].startswith(('CL11-sin', 'CL11-cerrado'))], sites)
    cortes_fn = collections.defaultdict(list)
    for r in CORTES:
        if r['función'].startswith('0x'):
            cortes_fn[int(r['función'], 16)].append(r)

    # --- 07 y 08: las filas y las funciones que nombran ---
    P08 = {r['id']: r for r in tsv(f'{E}/pendientes-08.tsv')}
    Q07 = {r['id']: r for r in tsv(f'{E}/quirks-07.tsv')}
    ids_rows, ids_fn = [], collections.defaultdict(list)
    idre = re.compile(r'^\|\s*(~~)?\s*\**\s*([PQ]-C\d+[a-z]?)\b')
    for doc in ('Client/docs/research/07-quirks-y-bugs.md', 'Client/docs/research/08-pendientes.md'):
        text = texts.get(doc, '')
        for i, line in enumerate(text.split('\n'), 1):
            m = idre.match(line)
            if not m:
                continue
            idd = m.group(2)
            closed = bool(m.group(1)) or line.split('|')[1].strip().startswith('~~')
            fns = []
            for h in ADDR.findall(line):
                fn, _ = F.find(int(h, 16))
                if fn is not None and fn not in fns:
                    fns.append(fn)
            cells = [c.strip() for c in line.split('|')]
            if idd.startswith('P-'):
                info = P08.get(idd, {})
                estado = info.get('estado') or ('cerrada' if closed else 'abierta')
                clase, ola = info.get('tipo', ''), info.get('ola', '')
                titulo = info.get('titulo', '') or (cells[2] if len(cells) > 2 else '')
            else:
                info = Q07.get(idd, {})
                estado = ('retirada' if (info.get('cerrada') == '1' or closed)
                          else 'con algo pendiente' if info.get('pend') == '1' else 'registrada')
                clase, ola = info.get('clase', ''), info.get('ola', '')
                titulo = info.get('que', '') or (cells[3] if len(cells) > 3 else '')
            row = dict(id=idd, doc=doc.rsplit('/', 1)[1], linea=i, estado=estado, clase_o_tipo=clase, ola=ola,
                       vm='sí' if (info.get('vm') == '1' or info.get('vm_en_texto') == '1') else '',
                       funciones=' '.join(hx(x) for x in fns[:40]), n_funciones=len(fns), titulo=titulo,
                       muret=info.get('muret', '') if idd.startswith('Q-') else info.get('bloquea', ''))
            ids_rows.append(row)
            for fn in fns:
                ids_fn[fn].append(row)
    VM = tsv(f'{E}/vm.tsv')
    vm_fn = collections.defaultdict(list)
    ids_by = collections.defaultdict(list)
    for r in ids_rows:
        ids_by[r['id']].append(r)
    for st in VM:
        src = st['fuente'].replace('.md', '')
        tag = (st['ids'].split()[0] if st['paso'] == 'fila' and st['ids'] else f"{src}#{st['paso']}")
        tag += ' (bloquea)' if st['bloquea'] == 'sí' else ''
        for idd in st['ids'].split():
            for r in ids_by.get(idd, []):
                for h in r['funciones'].split():
                    if tag not in vm_fn[int(h, 16)]:
                        vm_fn[int(h, 16)].append(tag)

    # --- funciones.tsv ---
    def top(counter, n):
        return '; '.join(f'{k}×{v}' if v > 1 else k for k, v in counter.most_common(n))

    out_rows = []
    for va in F.starts:
        fr, nw = FZ[va], NEW.get(va, {})
        rw = ROWS.get(va)
        clase = fr['classification']
        viva = ('sí' if rw else 'no') if clase == 'game' else ''
        cits = by_fn.get(va, [])
        imp = [r for r in cits if r['tipo'] == 'impl']
        cut = [r for r in cits if r['tipo'] == 'corte']
        tst = [r for r in cits if r['tipo'] == 'test']
        pend = [r for r in imp if r['pendiente']]
        src_c = collections.Counter(r['archivo'].removeprefix('Client/src/') for r in imp)
        tst_c = collections.Counter(r['archivo'].removeprefix('Client/tests/') for r in tst)
        sym_c = collections.Counter(f"{r['simbolo']} ({r['archivo'].rsplit('/', 1)[1].rsplit('.', 1)[0]})"
                                    for r in imp if r['simbolo'])
        case_c = collections.Counter(r['simbolo'] for r in tst if r['simbolo'] and '.' in r['simbolo'])
        first = ''
        if src_c:
            best = src_c.most_common(1)[0][0]
            ln = min(r['linea'] for r in imp if r['archivo'].endswith(best))
            first = f'{best}:{ln}'
        dd = docs_fn.get(va, [])
        doc_first = ''
        rs = [r for r in dd if r['area'] == 'docs-research']
        if rs:
            doc_first = f"{rs[0]['archivo'].removeprefix('Client/docs/research/')}:{rs[0]['linea']}"
        st = rw['st'] if rw else (nw.get('implementation_state', '') if clase == 'game' else 'not-applicable')
        done = (1 - rw['left']) if rw and st in ('implemented', 'partial') else 0.0
        tri = TRI.get(va)
        cat = OVERRIDE_CAT.get(va, tri['categoria'] if tri else '')
        fp = va in FALSE_POSITIVE and st in ('implemented', 'partial') and len(imp) <= FALSE_POSITIVE[va][0]
        if fp:
            cat = FALSE_POSITIVE[va][1]
        gap = GAP.get(va)
        clon = ''
        if gap and gap.get('clon_o_olas', '').startswith('exact:'):
            clon = gap['clon_o_olas'][6:]
        ch = CHAT.get(va)
        chat = f"{ch['bloque']} ({ch['alcance']})" if ch else ''
        if CHAT_TRAMOS.get(va):
            tb = sum(int(r['bytes']) for r in CHAT_TRAMOS[va])
            chat = (chat + '; ' if chat else '') + f'tramos {len(CHAT_TRAMOS[va])} ({tb} B)'
        cc = collections.Counter(r['clase'] for r in cortes_fn.get(va, []))
        cortes = ' '.join(f'{k}{cc[k]}' for k in CLASES if cc.get(k))
        cierra = [f"{r['archivo']}:{r['línea']}" for r in cortes_fn.get(va, [])
                  if r['cierra_el_cliente'].startswith('sí')]
        ids = ids_fn.get(va, [])
        i07 = sorted({r['id'] for r in ids if r['id'].startswith('Q-')}, key=lambda s: int(re.sub(r'\D', '', s)))
        i08a = sorted({r['id'] for r in ids if r['id'].startswith('P-') and r['estado'] != 'cerrada'},
                      key=lambda s: int(re.sub(r'\D', '', s)))
        i08c = sorted({r['id'] for r in ids if r['id'].startswith('P-') and r['estado'] == 'cerrada'},
                      key=lambda s: int(re.sub(r'\D', '', s)))
        c9 = CL9.get(va, {})
        cl_by = sorted(callers.get(va, ()))
        textos = pseudo_strings(fr.get('file_base', ''))
        wave_of = lambda x: FZ[x]['primary_wave'] if x in FZ else ''
        out_rows.append(dict(
            va=hx(va), fin=hx(va + int(fr['size_bytes'] or 0)), bytes=fr['size_bytes'], clase=clase, viva=viva,
            ola=fr['primary_wave'], sub_ola=c9.get('sub_ola') or CL8.get(va, ''), hito_cl9=c9.get('hito', ''),
            ola_blame=nw.get('primary_wave', ''), olas_consumidoras=fr.get('consumer_waves', ''),
            estado=st, hecho=round(done, 4) if rw else '', bytes_hechos=round(done * rw['size']) if rw else '',
            olas_de_sus_cortes=rw['later'] if rw else '',
            citas_impl=len(imp), citas_corte=len(cut), citas_pendiente=len(pend), citas_test=len(tst),
            citas_docs=len(dd), archivos_src=top(src_c, 3), primera_cita=first, simbolos_muret=top(sym_c, 3),
            tests=top(tst_c, 2), casos_de_test=top(case_c, 3),
            doc_research=doc_first,
            categoria_faltante=cat if st in ('unknown', 'not-started') or fp else '',
            falso_positivo='sí' if fp else '',
            clon_de=clon, chat=chat, cortes_vivos=cortes, cierra_el_cliente='; '.join(cierra),
            ids_07=' '.join(i07[:12]), ids_08_abiertas=' '.join(i08a[:12]), ids_08_cerradas=' '.join(i08c[:12]),
            pasos_vm='; '.join(vm_fn.get(va, [])[:4]),
            llamadores_n=len(cl_by), llamadores=' '.join(f'{hx(x)}({wave_of(x)})' for x in cl_by[:5]),
            llamados_n=len(callees.get(va, ())),
            plan_cl9=(f"{c9.get('como', '')} {c9.get('opcode_que_atiende', '')} {c9.get('paquetes_que_manda', '')}"
                      .strip() if c9 else ''),
            proposito=fr.get('purpose', '') or nw.get('purpose', ''),
            nota_auditoria=(ch['que_hace'] if ch else (tri['proposito'] if tri else '')),
            pseudocodigo=fr.get('file_base', ''), textos=textos, _va=va,
        ))
    # --- huecos: lo vivo sin citar que la auditoría no explica, en bloques contiguos ---
    EXPL = ('clon', 'portada sin cita', 'plataforma', 'herramienta', 'STL', 'sin uso')
    by_va = {r['_va']: r for r in out_rows}
    cited = {r['_va'] for r in out_rows if r['estado'] in ('implemented', 'partial') and not r['falso_positivo']}
    open_ = {r['_va'] for r in out_rows if r['viva'] == 'sí' and not r['categoria_faltante'].startswith(EXPL)
             and (r['estado'] in ('unknown', 'not-started') or r['falso_positivo'])}
    for r in out_rows:
        va = r['_va']
        if va not in open_:
            continue
        cc_ = [c for c in callers.get(va, ()) if c in cited]
        r['llamadores_citados'] = len(cc_)
        r['senal'] = ('la llama código citado' if cc_ else
                      'la llama sólo código sin citar' if callers.get(va) else 'sin llamador directo')
    blocks, cur = [], []
    for va in F.starts:
        r = by_va[va]
        if va in open_:
            cur.append(va)
        elif r['viva'] == 'sí':
            if cur:
                blocks.append(cur)
            cur = []
    if cur:
        blocks.append(cur)
    blocks.sort(key=lambda b: (-sum(int(by_va[x]['bytes']) for x in b), b[0]))
    huecos = []
    for n, b in enumerate(blocks, 1):
        hid = f'h{n:03d}'
        entries = collections.Counter()
        for x in b:
            by_va[x]['hueco'] = hid
            for c in callers.get(x, ()):
                if c in cited:
                    entries[c] += 1
        ent_txt = '; '.join(f"{hx(c)} ({by_va[c]['ola']}, {by_va[c]['archivos_src'].split(';')[0].split('×')[0]})"
                            for c, _ in entries.most_common(5))
        cats = collections.Counter(re.sub(r' \(.*|:.*', '', by_va[x]['categoria_faltante']) or 'sin clasificar'
                                   for x in b)
        waves = collections.Counter()
        for x in b:
            waves[by_va[x]['ola']] += int(by_va[x]['bytes'])
        txt = []
        for x in b:
            for t in by_va[x]['textos'].split(' | '):
                if t and t not in txt:
                    txt.append(t)
        ids = sorted({i for x in b for i in by_va[x]['ids_08_abiertas'].split()})
        huecos.append(dict(
            hueco=hid, desde=hx(b[0]), hasta=hx(b[-1]), funciones=len(b),
            bytes=sum(int(by_va[x]['bytes']) for x in b), ola=waves.most_common(1)[0][0],
            sub_ola=' '.join(sorted({by_va[x]['sub_ola'] for x in b} - {''})),
            categorias='; '.join(f'{k} ×{v}' if v > 1 else k for k, v in cats.most_common()),
            senal=('la llama código citado' if entries else
                   'la llama sólo código sin citar' if any(callers.get(x) for x in b) else 'sin llamador directo'),
            entradas_citadas=len(entries), entradas=ent_txt, ids_08=' '.join(ids[:8]),
            textos=' | '.join(txt[:6]), vas=' '.join(hx(x) for x in b[:40])))
    COLS_H = ['hueco', 'desde', 'hasta', 'funciones', 'bytes', 'ola', 'sub_ola', 'categorias', 'senal',
              'entradas_citadas', 'entradas', 'ids_08', 'textos', 'vas']
    write_tsv(os.path.join(a.out, 'huecos.tsv'), COLS_H, huecos)
    COLS_F = ['va', 'fin', 'bytes', 'clase', 'viva', 'ola', 'sub_ola', 'hito_cl9', 'ola_blame', 'olas_consumidoras',
              'estado', 'hecho', 'bytes_hechos', 'olas_de_sus_cortes', 'citas_impl', 'citas_corte', 'citas_pendiente',
              'citas_test', 'citas_docs', 'archivos_src', 'primera_cita', 'simbolos_muret', 'tests', 'casos_de_test',
              'doc_research',
              'categoria_faltante', 'falso_positivo', 'clon_de', 'chat', 'cortes_vivos', 'cierra_el_cliente', 'ids_07',
              'ids_08_abiertas', 'ids_08_cerradas', 'pasos_vm', 'llamadores_n', 'llamadores', 'llamados_n',
              'llamadores_citados', 'senal', 'hueco', 'plan_cl9', 'textos', 'proposito', 'nota_auditoria',
              'pseudocodigo']
    write_tsv(os.path.join(a.out, 'funciones.tsv'), COLS_F, out_rows)

    # --- trazas ---
    COLS_T = ['va', 'funcion', 'dentro', 'seccion', 'area', 'archivo', 'linea', 'simbolo', 'tipo', 'pendiente', 'en',
              'texto']
    key = lambda r: (r['_fn'] if r['_fn'] is not None else 1 << 40, int(r['va'], 16), r['archivo'], r['linea'])
    write_tsv(os.path.join(a.out, 'trazas.tsv'), COLS_T, sorted(traz, key=key))
    write_tsv(os.path.join(a.out, 'trazas-docs.tsv'), [c for c in COLS_T if c not in ('en', 'simbolo')],
              sorted(traz_docs, key=key))

    # --- archivos.tsv ---
    last = {}
    log = git(a.muret, 'log', '--format=%x01%h%x00%cs%x00%s', '--name-only', ref, '--', 'Client/src')
    for rec in log.split('\x01')[1:]:
        head, _, names = rec.partition('\n')
        h, d, s = (head.split('\x00') + ['', ''])[:3]
        for n in names.split('\n'):
            n = n.strip()
            if n and n not in last:
                last[n] = (d, h, s)
    per_file = collections.defaultdict(list)
    for r in traz:
        per_file[r['archivo']].append(r)
    test_fns = collections.defaultdict(set)
    for r in traz:
        if r['area'] == 'tests' and r['_fn'] is not None:
            test_fns[r['archivo'].removeprefix('Client/tests/')].add(r['_fn'])
    cortes_file = collections.defaultdict(list)
    for r in CORTES:
        cortes_file['Client/src/' + r['archivo']].append(r)
    arch = []
    for f in sorted(src_lines):
        rs = per_file.get(f, [])
        fns = {r['_fn'] for r in rs if r['_fn'] is not None and r['tipo'] == 'impl'}
        wb = collections.Counter()
        for fn in fns:
            wb[FZ[fn]['primary_wave']] += int(FZ[fn]['size_bytes'] or 0)
        kb = sum(wb.values()) / 1024
        ov = collections.Counter({t: len(fns & s) for t, s in test_fns.items() if fns & s})
        cc = collections.Counter(r['clase'] for r in cortes_file.get(f, []))
        d, h, s = last.get(f, ('', '', ''))
        arch.append(dict(
            archivo=f.removeprefix('Client/src/'), lineas=src_lines[f],
            citas=sum(1 for r in rs if r['tipo'] == 'impl'), citas_corte=sum(1 for r in rs if r['tipo'] == 'corte'),
            funciones=len(fns), kb_funciones=round(kb, 1),
            ola_principal=wb.most_common(1)[0][0] if wb else '',
            olas='; '.join(f'{w} {b / 1024:.1f} KB' for w, b in wb.most_common(4)),
            cortes_vivos=' '.join(f'{k}{cc[k]}' for k in CLASES if cc.get(k)),
            cierran=sum(1 for r in cortes_file.get(f, []) if r['cierra_el_cliente'].startswith('sí')),
            tests=top(ov, 3), ultimo_cambio=d, ultimo_commit=f'{h} {s}'))
    COLS_A = ['archivo', 'lineas', 'citas', 'citas_corte', 'funciones', 'kb_funciones', 'ola_principal', 'olas',
              'cortes_vivos', 'cierran', 'tests', 'ultimo_cambio', 'ultimo_commit']
    write_tsv(os.path.join(a.out, 'archivos.tsv'), COLS_A, arch)

    # --- ids.tsv ---
    COLS_I = ['id', 'doc', 'linea', 'estado', 'clase_o_tipo', 'ola', 'vm', 'n_funciones', 'funciones', 'titulo',
              'muret']
    write_tsv(os.path.join(a.out, 'ids.tsv'), COLS_I, ids_rows)

    # --- metricas.tsv ---
    M = []

    def m(ambito, clave, metrica, valor, unidad='', fuente='', nota='', r=short):
        M.append(dict(ref=r, ambito=ambito, clave=clave, metrica=metrica, valor=valor, unidad=unidad, fuente=fuente,
                      nota=nota))

    inv = collections.defaultdict(lambda: [0, 0])
    for va, fr in FZ.items():
        k = fr['classification'] + ('' if fr['classification'] != 'game' else (' viva' if va in ROWS else ' muerta'))
        inv[k][0] += 1
        inv[k][1] += int(fr['size_bytes'] or 0)
    for k in sorted(inv):
        m('inventario', k, 'funciones', inv[k][0], 'func', 'matriz congelada')
        m('inventario', k, 'kb', round(inv[k][1] / 1024, 1), 'KB', 'matriz congelada')

    def agg(sel):
        X = collections.Counter()
        for va, rw in ROWS.items():
            if FZ[va]['classification'] != 'game' or not sel(va):
                continue
            size, st = rw['size'], rw['st']
            X['live'] += size
            X['f_live'] += 1
            d = size * (1 - rw['left']) if st in ('implemented', 'partial') else 0
            X['done'] += d
            if st == 'unknown':
                X['unk'] += size
            if NEW.get(va, {}).get('validation', '').strip() not in ('', 'none'):
                X['done_t'] += d
            if st in ('implemented', 'partial'):
                X['f_cited'] += 1
            X['f_' + st] += 1
        return X

    def emit(ambito, clave, X, fuente):
        if not X['live']:
            return
        L = X['live']
        m(ambito, clave, 'funciones_vivas', X['f_live'], 'func', fuente)
        m(ambito, clave, 'kb_vivos', round(L / 1024, 1), 'KB', fuente)
        m(ambito, clave, 'funciones_citadas', X['f_cited'], 'func', fuente)
        m(ambito, clave, 'cota_alta', pct(X['done'] + X['unk'], L), '%', fuente)
        m(ambito, clave, 'cota_media', pct(X['done'] + X['unk'] / 2, L), '%', fuente)
        m(ambito, clave, 'cota_baja', pct(X['done'], L), '%', fuente)
        m(ambito, clave, 'cota_baja_con_test', pct(X['done_t'], L), '%', fuente)
        m(ambito, clave, 'falta_cota_baja', round((L - X['done']) / 1024, 1), 'KB', fuente)

    T = agg(lambda va: True)
    emit('avance', 'total', T, 'matriz regenerada sin MENTION_ONLY, ola congelada')
    for w in [f'CL{i}' for i in range(1, 12)]:
        emit('avance-ola', w, agg(lambda va, w=w: FZ[va]['primary_wave'] == w), 'ola congelada')
    for h in sorted({r.get('hito', '') for r in CL9.values()} - {''}, key=int):
        emit('avance-cl9-hito', f'hito {h}', agg(lambda va, h=h: CL9.get(va, {}).get('hito') == h), 'plan CL9')
    for s in sorted({x for v in CL8.values() for x in v.split()}):
        emit('avance-cl8-subola', s, agg(lambda va, s=s: s in CL8.get(va, '').split() and FZ[va]['primary_wave'] ==
                                         'CL8'), 'plan CL8 (olas_por_lector), sólo funciones de ola CL8')
    chat_whole = {va for va, r in CHAT.items() if r['bloque'] in ('A', 'B')}
    X = agg(lambda va: va not in chat_whole)
    for k in ('cota_alta', 'cota_baja', 'cota_baja_con_test'):
        pass
    L = X['live']
    m('avance', 'sin el chat', 'cota_alta', pct(X['done'] + X['unk'], L), '%', 'sin las funciones enteras de A y B')
    m('avance', 'sin el chat', 'cota_baja', pct(X['done'], L), '%', 'sin las funciones enteras de A y B')
    m('avance', 'sin el chat', 'cota_baja_con_test', pct(X['done_t'], L), '%', 'sin las funciones enteras de A y B')

    # faltante por categoría (sin citar)
    cat = collections.defaultdict(lambda: [0, 0])
    expl = 0
    for r in out_rows:
        if r['viva'] != 'sí' or r['estado'] not in ('unknown', 'not-started'):
            continue
        c = r['categoria_faltante'] or 'sin clasificar'
        c0 = re.sub(r' \(.*', '', c.split(':')[0]) if not c.startswith('pendiente CL9') else 'pendiente CL9'
        if c.startswith('clon'):
            c0 = 'clon byte a byte de una citada'
        if c.startswith('portada sin cita'):
            c0 = 'portada sin cita 0x'
        cat[c0][0] += 1
        cat[c0][1] += int(r['bytes'])
        if c.startswith(EXPL):
            expl += int(r['bytes'])
    for c in sorted(cat, key=lambda c: -cat[c][1]):
        m('faltante', c, 'funciones', cat[c][0], 'func', 'triage de la auditoría')
        m('faltante', c, 'kb', round(cat[c][1] / 1024, 1), 'KB', 'triage de la auditoría')
    partial_left = sum(rw['size'] * rw['left'] for rw in ROWS.values() if rw['st'] == 'partial')
    m('faltante', 'fracción de cortes de las citadas', 'kb', round(partial_left / 1024, 1), 'KB', 'matriz')
    m('avance', 'total', 'explicado', pct(T['done'] + expl, T['live']), '%',
      'cota baja + clones, portadas sin 0x, plataforma, herramientas, STL y sin uso')

    # huecos
    for sen in ('la llama código citado', 'la llama sólo código sin citar', 'sin llamador directo'):
        hs = [h for h in huecos if h['senal'] == sen]
        m('huecos', sen, 'bloques', len(hs), 'bloques', 'huecos.tsv')
        m('huecos', sen, 'funciones', sum(h['funciones'] for h in hs), 'func', 'huecos.tsv')
        m('huecos', sen, 'kb', round(sum(h['bytes'] for h in hs) / 1024, 1), 'KB', 'huecos.tsv')

    # cortes
    cc = collections.Counter(r['clase'] for r in CORTES if r['archivo'])
    for k in CLASES:
        m('cortes', k, 'sitios', cc.get(k, 0), 'sitios', 'censo de cortes (auditoría)')
    m('cortes', 'total', 'sitios', sum(cc.values()), 'sitios', 'censo de cortes (auditoría)')
    m('cortes', 'cierran el cliente', 'sitios', sum(1 for r in CORTES if r['cierra_el_cliente'].startswith('sí')),
      'sitios', 'censo + informe 4.1')
    m('entradas', 'cortes-vivos', 'unresolved-entries', sum(1 for r in CORTES if r['resolved_by'] == 'unresolved'),
      'filas', 'hand rows whose cut reference_anchors.py did not find')
    cw = collections.Counter(r['ola'] for r in CORTES if r['archivo'] and r['clase'] != 'H')
    for w in sorted(cw, key=lambda w: (len(w), w)):
        m('cortes-ola', w, 'sitios', cw[w], 'sitios', 'ola de la función del original que contiene la VA')

    # VM, 08, 07
    for w, n in sorted(collections.Counter(r['ola'] for r in VM).items()):
        m('vm', w, 'pasos', n, 'pasos', 'lista única (cota superior)')
    m('vm', 'total', 'pasos', len(VM), 'pasos', 'lista única (cota superior)')
    m('vm', 'total', 'bloquean_codigo', sum(1 for r in VM if r['bloquea'] == 'sí'), 'pasos', 'lista única')
    for (w, e), n in sorted(collections.Counter((r['ola'], r['estado']) for r in P08.values()).items()):
        m('08', w, e, n, 'filas', 'pendientes-08')
    for t, n in sorted(collections.Counter(r['tipo'] for r in P08.values() if r['estado'] != 'cerrada').items()):
        m('08-abiertas', t, 'filas', n, 'filas', 'pendientes-08 (código es cota superior)')
    q = [r for r in ids_rows if r['id'].startswith('Q-')]
    qc = collections.Counter(r['clase_o_tipo'] for r in q)
    big = {c for c, n in qc.items() if n >= 5 and c}
    for (c, e), n in sorted(collections.Counter((r['clase_o_tipo'] if r['clase_o_tipo'] in big else 'otras',
                                                 r['estado']) for r in q).items()):
        m('07', c, e, n, 'filas', '07-quirks-y-bugs')

    # chat
    nj = json.load(open(f'{E}/numeros-chat.json'))
    ab = {k: 0 for k in ('n', 'bytes', 'cit', 'test')}
    for b in ('A', 'B'):
        v = nj['bloques'][b]['vivo']
        ab['n'] += v['total']['n']
        ab['bytes'] += v['total']['bytes']
        ab['cit'] += v['citadas_sin_corte']['bytes']
        ab['test'] += v['con_test']['bytes']
    m('chat', 'A+B vivo', 'entradas', ab['n'], 'entradas', 'auditoría del chat')
    m('chat', 'A+B vivo', 'bytes', ab['bytes'], 'B', 'auditoría del chat')
    m('chat', 'A+B vivo', 'pct_codigo_juego', pct(ab['bytes'], nj['game_bytes']), '%', 'sobre 2.273 KB con lo muerto')
    m('chat', 'A+B vivo', 'citado_sin_corte', pct(ab['cit'], ab['bytes']), '%', 'auditoría del chat')
    m('chat', 'A+B vivo', 'con_test', pct(ab['test'], ab['bytes']), '%', 'auditoría del chat')
    for b, name in (('C', 'amigos (C)'),):
        v = nj['bloques'][b]['vivo'] if 'vivo' in nj['bloques'][b] else nj['bloques'][b]['todo']
        m('chat', name, 'bytes', v['total']['bytes'], 'B', 'auditoría del chat')
        m('chat', name, 'citado_sin_corte', pct(v['citadas_sin_corte']['bytes'], v['total']['bytes']), '%',
          'auditoría del chat')
        m('chat', name, 'con_test', pct(v['con_test']['bytes'], v['total']['bytes']), '%', 'auditoría del chat')

    # sustitutos y sin cablear
    for c, n in sorted(collections.Counter(r['categoria'] for r in tsv(f'{E}/sustitutos.tsv')).items()):
        m('sustitutos', c, 'filas', n, 'filas', 'auditoría')
    for (t, v), n in sorted(collections.Counter((r['tipo'], r['veredicto']) for r in tsv(f'{E}/sin-cablear.tsv'))
                            .items()):
        m('sin-cablear', t, v, n, 'filas', 'auditoría')

    # tamaño de MuRet y citas
    m('muret', 'Client/src', 'archivos', len(src_lines), 'archivos', 'ls-tree')
    m('muret', 'Client/src', 'lineas', sum(src_lines.values()), 'líneas', 'ls-tree')
    tests_files = [f for f in scan if f.startswith('Client/tests/') and f.endswith('.cpp')]
    ntest = sum(len(re.findall(r'^\s*TEST(?:_F|_P)?\(', texts.get(f, ''), re.M)) for f in tests_files)
    m('muret', 'Client/tests', 'archivos', len(tests_files), 'archivos', 'ls-tree')
    m('muret', 'Client/tests', 'tests', ntest, 'TEST', 'TEST, TEST_F y TEST_P')
    for ar in ('src', 'tests'):
        for t in sorted({r['tipo'] for r in traz if r['area'] == ar}):
            m('citas', ar, t, sum(1 for r in traz if r['area'] == ar and r['tipo'] == t), 'citas', 'trazas')
    m('citas', 'docs', 'doc', len(traz_docs), 'citas', 'trazas-docs')
    m('citas', 'src', 'pendiente', sum(1 for r in traz if r['pendiente']), 'citas',
      'citas que no son corte pero dicen que no está portado')

    # serie: los puntos guardados en entradas/serie (por fecha del commit) y esta corrida
    here = f'{ref[:7]}nm'
    if a.guardar_serie:
        with open(f'{E}/serie/frozen-{here}.json', 'w', encoding='utf-8') as fh:
            json.dump({'Total': dict(T)}, fh, indent=1)
            fh.write('\n')
    serie = {}
    for fjs in os.listdir(f'{E}/serie'):
        r0 = fjs.removeprefix('frozen-').removesuffix('.json')
        with open(f'{E}/serie/{fjs}', encoding='utf-8') as fh:
            serie[r0] = (json.load(fh)['Total'], 'frozen.py')
    serie.setdefault(here, (T, 'esta corrida'))

    def when(r0):
        try:
            return git(a.muret, 'show', '-s', '--format=%cI', r0.removesuffix('nm')).strip()
        except Exception:  # un commit que este clon no tiene: al final
            return '~'

    for r0 in sorted(serie, key=lambda r: (when(r), r)):
        d, fuente = serie[r0]
        L = d['live']
        nota = 'MENTION_ONLY corregida' if r0.endswith('nm') else 'MENTION_ONLY vieja (comparable con la serie)'
        m('serie', 'total', 'cota_baja', pct(d['done'], L), '%', fuente, nota, r=r0)
        m('serie', 'total', 'cota_media', pct(d['done'] + d['unk'] / 2, L), '%', fuente, nota, r=r0)
        m('serie', 'total', 'cota_alta', pct(d['done'] + d['unk'], L), '%', fuente, nota, r=r0)
        m('serie', 'total', 'cota_baja_con_test', pct(d['done_t'], L), '%', fuente, nota, r=r0)

    # --- las tablas de la auditoría, con los V corregidos ---
    cols_c = list(tsv(f'{E}/cortes-vivos.tsv')[0].keys()) + ['resolved_by']
    write_tsv(os.path.join(a.out, 'cortes.tsv'), cols_c, CORTES)
    for src, dst in (('vm.tsv', 'vm.tsv'), ('sustitutos.tsv', 'sustitutos.tsv'), ('sin-cablear.tsv', 'sin-cablear.tsv'),
                     ('chat-funciones.tsv', 'chat.tsv')):
        rows = tsv(f'{E}/{src}')
        write_tsv(os.path.join(a.out, dst), list(rows[0].keys()), rows)

    COLS_M = ['ref', 'ambito', 'clave', 'metrica', 'valor', 'unidad', 'fuente', 'nota']
    write_tsv(os.path.join(a.out, 'metricas.tsv'), COLS_M, M)
    with open(os.path.join(a.out, 'generado.json'), 'w') as fh:
        json.dump(dict(ref=ref, fecha_commit=fecha, funciones=len(out_rows), trazas=len(traz),
                       trazas_docs=len(traz_docs), archivos=len(arch), ids=len(ids_rows), metricas=len(M)), fh,
                  indent=1)
    print(json.dumps(dict(ref=short, funciones=len(out_rows), trazas=len(traz), trazas_docs=len(traz_docs),
                          archivos=len(arch), ids=len(ids_rows), metricas=len(M))))


if __name__ == '__main__':
    main()
