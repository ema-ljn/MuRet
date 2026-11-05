#!/usr/bin/env python3
"""Arma la caché de las herramientas del binario desde el main.exe: el listado y el grafo de llamadas.

Lee el exe de MURET_MAIN_EXE (o --exe) y escribe en MURET_RESEARCH_CACHE (por defecto
.research-cache/ en la raíz del repo, que Git ignora):
- all.txt: `objdump -d -M intel --no-show-raw-insn <exe>` (GNU binutils; con el c32bbd5f da el
  mismo listado que usó el barrido de Lorencia, 894.896 líneas de .text). objdump se queja de la
  tabla de símbolos ("file truncated"): es normal en este exe y no cambia el listado;
- cg.pkl: el grafo de llamadas de cg.py (direcciones, texto y comienzos de función);
- source.txt: el SHA-256 y la ruta del exe.

Nada de esto va a Git: se vuelve a sacar del exe. Tarda unos segundos.

Uso: regen_cache.py [--exe RUTA]
"""
import argparse
import os
import pickle
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cg  # noqa: E402
import image  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description='Arma all.txt y cg.pkl desde el main.exe.')
    ap.add_argument('--exe', help='el main.exe (por defecto, MURET_MAIN_EXE)')
    args = ap.parse_args()
    exe = image.exe_path(args.exe)
    objdump = shutil.which('objdump')
    if not objdump:
        sys.exit('falta objdump (GNU binutils)')
    digest = image.sha256(exe)
    if digest != image.C32BBD5F:
        print('aviso: el exe no es el c32bbd5f (%s…): las direcciones del research son de ese' % digest[:8],
              file=sys.stderr)
    out = cg.cache_dir()
    os.makedirs(out, exist_ok=True)
    listing = os.path.join(out, cg.LISTING)
    with open(listing + '.tmp', 'w', encoding='utf-8') as f:
        run = subprocess.run([objdump, '-d', '-M', 'intel', '--no-show-raw-insn', exe], stdout=f,
                             stderr=subprocess.PIPE, text=True)
    with open(listing + '.tmp', encoding='utf-8', errors='replace') as f:
        complete = any(line.startswith('Disassembly of section .text') for line in f)
    # Con este exe objdump sale con 1 por la tabla de símbolos, pero el listado sale entero.
    if not complete:
        os.remove(listing + '.tmp')
        sys.exit('objdump falló (%d): %s' % (run.returncode, run.stderr.strip()))
    if run.returncode != 0:
        print('objdump (%d, se ignora): %s' % (run.returncode, run.stderr.strip().splitlines()[-1]),
              file=sys.stderr)
    os.replace(listing + '.tmp', listing)
    addrs, lines, starts = cg.parse_listing(listing)
    graph = os.path.join(out, cg.GRAPH)
    with open(graph + '.tmp', 'wb') as f:
        pickle.dump((addrs, lines, starts), f)
    os.replace(graph + '.tmp', graph)
    with open(os.path.join(out, cg.SOURCE), 'w', encoding='utf-8') as f:
        f.write('%s  %s\n' % (digest, exe))
    print('%s: %d instrucciones, %d comienzos de función' % (out, len(addrs), len(starts)))


if __name__ == '__main__':
    main()
