#!/usr/bin/env python3
"""Los paquetes del cable en claro, uno por línea: "conexión hora puerto sentido frame [ser=N] hex".

Por defecto sale solo lo que el plan de la CL7 deja guardar (wirelib.is_safe: la respuesta del F3 03,
el F3 10, D3, D7, 1C, F3 04, F3 12, 0F y A3), así lo que da se puede llevar a un vector. Con --heads
se achica a unos opcodes (`1c,f304,f312`: el cambio de mapa, los "vectores-cable" del barrido D).
Con --wire, cada línea lleva al final el paquete como viajó (sin el 0xAA, con el XOR32 o el C3: el
`C1 04 F3 68` del F3 12). Con --count da solo cuántos paquetes hay de cada sentido, frame y opcode, sin
bytes. --all saca todo, con el login, el chat y los nombres: esa salida no va a Git ni a un informe.

Uso:
    decode.py [cable] [--port 55901|all] [--heads 1c,f304] [--wire] [--count] [--all]
    (sin cable, el muproxy.log de MURET_CLIENT_WIRE_DIR; un PCAP pasa antes por pcap2log.py)
"""
import argparse
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wirelib  # noqa: E402


def parse_heads(text):
    heads = set()
    for item in text.split(','):
        item = item.strip().lower()
        if item:
            code = bytes.fromhex(item)
            heads.add(tuple(code))
    return heads


def main():
    ap = argparse.ArgumentParser(description='Los paquetes del cable en claro.')
    ap.add_argument('log', nargs='?', help='el cable (por defecto, el muproxy.log de MURET_CLIENT_WIRE_DIR)')
    ap.add_argument('--port', default=wirelib.GAME_PORT, help='el puerto (55901 el GS; all, todos)')
    ap.add_argument('--heads', help='solo estos opcodes, en hexadecimal: 1c,f304,f312')
    ap.add_argument('--wire', action='store_true', help='agregar el paquete como viajó')
    ap.add_argument('--count', action='store_true', help='solo cuántos de cada sentido, frame y opcode')
    ap.add_argument('--all', action='store_true', help='todo, con nombres y login: no va a Git')
    args = ap.parse_args()
    heads = parse_heads(args.heads) if args.heads else None
    if args.all and not args.count:
        print('aviso: --all saca el login, el chat y los nombres; la salida no va a Git', file=sys.stderr)
    counts = collections.Counter()
    for p in wirelib.read_log(args.log, args.port):
        if p.data is None:
            counts[(p.direction, p.frame, None)] += 1
            continue
        op, sub = wirelib.opcode(p.data)
        if heads is not None and (op,) not in heads and (op, sub) not in heads:
            continue
        if args.count:
            counts[(p.direction, p.frame, '%02x%s' % (op, '%02x' % sub if op == 0xF3 and sub is not None else ''))] += 1
            continue
        if not args.all and not wirelib.is_safe(p):
            continue
        serial = ' ser=%d' % p.serial if p.serial is not None else ''
        extra = [p.wire.hex()] if args.wire else []
        print(p.conn, p.time, p.port, p.direction, p.frame + serial, p.data.hex(), *extra)
    for key, n in sorted(counts.items(), key=lambda kv: -kv[1]):
        print(*[k for k in key if k is not None], n)


if __name__ == '__main__':
    main()
