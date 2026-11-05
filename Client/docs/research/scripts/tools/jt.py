#!/usr/bin/env python3
"""Lee la tabla de saltos de un `switch` del main.exe: qué casos van a cada destino.

MSVC arma los `switch` grandes con dos tablas: una de bytes, indexada con el caso (`movzx r, BYTE PTR
[r+TB]`), que da el índice en una de punteros (`jmp DWORD PTR [r*4+TS]`). Con `-` en vez de la tabla de
bytes, el caso indexa directo la de punteros. `base` es el valor del caso 0 de la tabla (el `sub`/`add`
que la precede), para que la salida muestre los casos reales; los destinos con 40 casos o más se
resumen (suele ser el default).

Uso: jt.py tabla_de_bytes|- tabla_de_saltos cantidad [base] [--exe RUTA]
Ejemplo: jt.py 606ed4 606ea0 0x97      (los casos del mapa 0 de 0x606660, la creación de objetos:
         `cmp eax,0x96` en 0x6069a3, `mov cl,[eax+0x606ed4]` y `jmp [ecx*4+0x606ea0]`)
"""
import argparse
import os
import sys
from collections import OrderedDict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import image  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description='Lee la tabla de saltos de un switch.')
    ap.add_argument('bytes_table', help='la VA de la tabla de bytes, o - si no hay')
    ap.add_argument('jump_table', help='la VA de la tabla de punteros')
    ap.add_argument('count', help='cuántos casos (0x.. o decimal)')
    ap.add_argument('base', nargs='?', default='0', help='el caso del índice 0 (0x.. o decimal)')
    ap.add_argument('--exe', help='el main.exe (por defecto, MURET_MAIN_EXE)')
    args = ap.parse_args()
    img = image.Image(args.exe)
    jump = int(args.jump_table, 16)
    count, base = int(args.count, 0), int(args.base, 0)
    targets = OrderedDict()
    for i in range(count):
        k = i if args.bytes_table == '-' else img.u8(int(args.bytes_table, 16) + i)
        targets.setdefault(img.u32(jump + 4 * k), []).append(i + base)
    for t, cases in targets.items():
        text = ' '.join(hex(c) for c in cases) if len(cases) < 40 else '%d casos (default)' % len(cases)
        print(hex(t), text)


if __name__ == '__main__':
    main()
