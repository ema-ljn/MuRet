#!/usr/bin/env python3
"""Lo que dice el cable del caminar: los A3 del cliente (el D3 de main.exe, que FoxAnti cambia de
opcode) y los D3 con que contesta el servidor.

1. Los A3: cuántos hay, cuántos de cada largo de camino (n de 0 a 15), si el largo del paquete es 6 +
   (n + 1) / 2, y cuántos llevan como dirección final la del último paso; los que no, uno por línea
   (son los del blanco al lado del final de la CL7.4 B).
2. El eco: cada D3 del servidor que cae en la celda final del último A3 de la misma conexión, por id,
   con la dirección igual o distinta a la que mandó el cliente.

Los A3 y los D3 son celdas, direcciones e ids de objeto: no llevan nombres.

Uso: walk_stats.py [cable]      (sin cable, el muproxy.log de MURET_CLIENT_WIRE_DIR)
"""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import wirelib  # noqa: E402


def main():
    log = sys.argv[1] if len(sys.argv) > 1 else None
    by_length = collections.Counter()
    total = bad_length = final_is_last = final_other = 0
    others = []
    last = {}
    echo = collections.Counter()
    for p in wirelib.read_log(log):
        if p.frame != 'C1' or p.data is None:
            continue
        op = p.data[2]
        if p.direction == 'C->S' and op == 0xA3:
            x, y, final, dirs = wirelib.walk_dirs(p.data)
            n = len(dirs)
            total += 1
            by_length[n] += 1
            if len(p.data) != 6 + ((n + 1) >> 1):
                bad_length += 1
            if n:
                if final == dirs[-1]:
                    final_is_last += 1
                else:
                    final_other += 1
                    others.append((p.conn, p.time, p.data.hex(' '), dirs))
            for d in dirs:
                x += wirelib.DIRS[d][0]
                y += wirelib.DIRS[d][1]
            last[p.conn] = (x, y, final)
        elif p.direction == 'S->C' and op == 0xD3 and p.conn in last:
            ident = (p.data[3] << 8) | p.data[4]
            if (p.data[5], p.data[6]) == last[p.conn][:2]:
                same = 'dir igual' if p.data[7] >> 4 == last[p.conn][2] else 'dir distinta'
                echo[(p.conn, ident, same, p.data[7] & 15)] += 1
    print('A3: %d, por largo %s, largos mal %d, final == último paso %d, distinto %d' % (
        total, sorted(by_length.items()), bad_length, final_is_last, final_other))
    for item in others:
        print('   final distinto:', *item)
    print('eco del servidor (conexión, id, dirección, nibble bajo): veces')
    for key, n in sorted(echo.items()):
        print('  ', key, n)


if __name__ == '__main__':
    main()
