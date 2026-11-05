#!/usr/bin/env python3
"""Pasa un PCAP (Ethernet, IPv4, TCP) a líneas como las del muproxy.log, para leerlo con decode.py.

Rearma cada conexión y cada sentido del puerto por número de secuencia, sin repetidos, y escribe un
segmento por línea:
"HH:MM:SS.mmm puerto S->C len=N hex" (la hora en UTC), "puerto ACC" en cada SYN del cliente y "GAP" donde
falta un tramo. Es lo que usó el barrido D con la sesión de la PR #45 del laboratorio
(`capture_20261030T192219Z_8920d1.tar.gz`, con `network.pcap` adentro): los cuatro 1C y sus F3 12 de la
CL7.5 C.

La salida lleva el login y el chat de esa sesión: queda fuera de Git, como el PCAP.

Uso: pcap2log.py network.pcap [puerto] > pr45.log      (el puerto del GS por defecto, 55901)
"""
import datetime
import struct
import sys


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 55901
    with open(sys.argv[1], 'rb') as f:
        data = f.read()
    magic = struct.unpack_from('<I', data, 0)[0]
    if magic not in (0xa1b2c3d4, 0xa1b23c4d):
        sys.exit('no es un pcap clásico (little-endian): %s' % hex(magic))
    nano = magic == 0xa1b23c4d
    link = struct.unpack_from('<I', data, 20)[0]
    if link != 1:
        sys.exit('el enlace no es Ethernet (%d)' % link)
    off = 24
    segments = {}                  # (puerto del cliente, sentido) → [(hora, seq, datos, syn)]
    accepts = []
    while off + 16 <= len(data):
        ts, frac, incl, _ = struct.unpack_from('<IIII', data, off)
        off += 16
        frame = data[off:off + incl]
        off += incl
        if len(frame) < 14:
            continue
        ether, p = struct.unpack_from('>H', frame, 12)[0], 14
        if ether == 0x8100:
            ether, p = struct.unpack_from('>H', frame, 16)[0], 18
        if ether != 0x0800:
            continue
        ip = frame[p:]
        ihl, proto, total = (ip[0] & 15) * 4, ip[9], struct.unpack_from('>H', ip, 2)[0]
        if proto != 6:
            continue
        tcp = ip[ihl:total]
        sport, dport, seq = struct.unpack_from('>HHI', tcp, 0)
        payload, flags = tcp[(tcp[12] >> 4) * 4:], tcp[13]
        if sport == port:
            key = (dport, 'S->C')
        elif dport == port:
            key = (sport, 'C->S')
        else:
            continue
        t = ts + (frac / 1e9 if nano else frac / 1e6)
        if flags & 2:
            segments.setdefault(key, []).append((t, seq + 1, b'', True))
            if key[1] == 'C->S':
                accepts.append(t)
            continue
        if payload:
            segments.setdefault(key, []).append((t, seq, payload, False))
    out = [(t, 'ACC', None) for t in accepts]
    for (_, direction), items in segments.items():
        base, got = None, {}
        for t, seq, payload, syn in items:
            if syn:
                base = seq
                continue
            if base is None:
                base = seq
            rel = (seq - base) & 0xffffffff
            if rel not in got:
                got[rel] = (t, payload)
        pos = 0
        for rel in sorted(got):
            t, payload = got[rel]
            if rel + len(payload) <= pos:
                continue
            if rel < pos:
                payload, rel = payload[pos - rel:], pos
            if rel > pos:
                out.append((t, direction, None))
            out.append((t, direction, payload))
            pos = rel + len(payload)
    out.sort(key=lambda item: item[0])
    for t, direction, payload in out:
        hhmm = datetime.datetime.fromtimestamp(t, datetime.timezone.utc).strftime('%H:%M:%S.%f')[:-3]
        if direction == 'ACC':
            print(hhmm, port, 'ACC', 'len=0')
        elif payload is None:
            print(hhmm, port, direction, 'GAP')
        else:
            print(hhmm, port, direction, 'len=%d' % len(payload), payload.hex())


if __name__ == '__main__':
    main()
