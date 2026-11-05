"""Lee un cable del laboratorio y da cada paquete en claro, con la cabecera C1/C2 rearmada.

El cable es el muproxy.log de MURET_CLIENT_WIRE_DIR (o un log armado con pcap2log.py desde el PCAP de la
PR #45): una línea por segmento, "HH:MM:SS.mmm puerto sentido len=N hex", y "puerto ACC" en cada
conexión nueva. Por sentido, los segmentos se juntan y se cortan en paquetes (03, El corte):
- S->C: el XOR 0xAA del servidor del laboratorio se decide por paquete, por el primer byte (C1..C4 van
  en claro; 6B, 68, 69, 6E llevan 0xAA, Q-C18); C3 y C4 se descifran con Dec2 (la inversa de Enc2.dat);
- C->S: C1 y C2 llevan el XOR32 del envío desde el byte que sigue al opcode; C3 se descifra con
  Dec1.dat y adentro va un C1 con XOR32.
Los C3 y C4 necesitan además c3c4_reference.py, el SimpleModulus del repo de research, en
MURET_C3C4_REFERENCE, y el main.exe (la tabla XOR de 0x6d84fc) en MURET_MAIN_EXE. Sin la referencia,
se saltean con un aviso.

El cable lleva el login, el chat y los nombres de los personajes: nada de lo que sale de acá va a Git
salvo lo que pasa `is_safe` (los paquetes que el plan de la CL7 deja guardar).
"""
import collections
import importlib.util
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
import common  # noqa: E402

XOR32 = bytes.fromhex('e76d3a89bcb29f7323a8feb6495d395d8acb638dea7d2b5fc3b1e9832951e856')
DIRS = [(-1, -1), (0, -1), (1, -1), (1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0)]
GAME_PORT = '55901'
LINE = re.compile(r'(\S+) (\d+) (\S+) len=(\d+) ?([0-9a-f]*)')

# Lo que se puede guardar de un cable (plan de la CL7, Restricciones): la respuesta del F3 03, el F3 10,
# D3, D7, 1C, F3 04, F3 12, 0F y A3. Nunca el chat, el F3 03 que sale, las listas de personajes, el C2
# 12, el 02, el F3 01 ni el F3 02.
SAFE_HEADS = {(0xF3, 0x10), (0xD3,), (0xD7,), (0x1C,), (0xF3, 0x04), (0xF3, 0x12), (0x0F,), (0xA3,)}
SAFE_REPLY = (0xF3, 0x03)

# `wire` es el paquete como viajó (sin el 0xAA, todavía con el XOR32 o el C3/C4); `data`, en claro.
Packet = collections.namedtuple('Packet', 'conn time port direction frame serial data wire')


def unxor32(p):
    """Deshace el XOR32 de un C1 o C2 del cliente: desde el byte que sigue al opcode."""
    out = bytearray(p)
    first = 3 if p[0] in (0xC1, 0xC3) else 4
    for i in range(first, len(p)):
        out[i] = p[i] ^ XOR32[i & 31] ^ p[i - 1]
    return bytes(out)


def opcode(data):
    """(opcode, subcódigo) de un C1/C2 en claro (el subcódigo es None si el paquete no lo tiene)."""
    i = 2 if data[0] == 0xC1 else 3
    return data[i], (data[i + 1] if len(data) > i + 1 else None)


def is_safe(packet):
    if packet.data is None:
        return False
    op, sub = opcode(packet.data)
    if (op, sub) == SAFE_REPLY:
        return packet.direction == 'S->C'
    return (op,) in SAFE_HEADS or (op, sub) in SAFE_HEADS


class _Keys:
    """Las claves de C3/C4, cargadas la primera vez que hacen falta."""

    def __init__(self):
        self.loaded = False
        self.c3 = None
        self.warned = False

    def get(self):
        if not self.loaded:
            self.loaded = True
            path = os.environ.get('MURET_C3C4_REFERENCE')
            if path and os.path.isfile(path):
                spec = importlib.util.spec_from_file_location('c3c4_reference', path)
                c3 = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(c3)
                table = c3.read_xor_table(common.main_exe())
                self.dec1 = c3.load_key_file(common.wire_file('Dec1.dat'), c3.DEC_FLAGS, table)
                enc2 = c3.load_key_file(common.wire_file('Enc2.dat'), c3.ENC_FLAGS, table)
                self.dec2 = dict(enc2)
                self.dec2['dec'] = [pow(e, -1, m) for e, m in zip(enc2['enc'], enc2['mod'])]
                self.c3 = c3
        if self.c3 is None and not self.warned:
            self.warned = True
            print('aviso: sin MURET_C3C4_REFERENCE (c3c4_reference.py) los C3 y C4 se saltean', file=sys.stderr)
        return self.c3


_KEYS = _Keys()


def _length(b):
    if len(b) < 3:
        return None
    if b[0] in (0xC1, 0xC3):
        return b[1]
    if b[0] in (0xC2, 0xC4):
        return (b[1] << 8) | b[2]
    return -1


def _plain(direction, p):
    """(frame, serial, paquete en claro) de un paquete entero del cable."""
    frame = 'C%d' % (p[0] - 0xC0)
    if p[0] in (0xC1, 0xC2):
        return frame, None, unxor32(p) if direction == 'C->S' else p
    c3 = _KEYS.get()
    if c3 is None:
        return frame, None, None
    if p[0] == 0xC3:
        body, n = c3.decrypt(_KEYS.dec1 if direction == 'C->S' else _KEYS.dec2, p[2:p[1]])
        body = body[:n]
        inner = bytes([0xC1, len(body) + 1]) + bytes(body[1:])
        return frame, body[0], unxor32(inner) if direction == 'C->S' else inner
    size = (p[1] << 8) | p[2]
    body, n = c3.decrypt(_KEYS.dec1 if direction == 'C->S' else _KEYS.dec2, p[3:size])
    body = body[:n]
    total = len(body) + 2
    inner = bytes([0xC2, total >> 8, total & 0xff]) + bytes(body[1:])
    return frame, body[0], unxor32(inner) if direction == 'C->S' else inner


def read_log(path=None, port=GAME_PORT):
    """Cada paquete del puerto `port` (el GS por defecto; 'all' para todos), en orden.

    `conn` cuenta las líneas ACC de todos los puertos (la primera conexión es la 1). Un paquete que no
    se puede cortar sale con frame '?' y data None, y se descarta lo que queda de ese segmento."""
    path = path or common.wire_file()
    conn = 0
    buffers = collections.defaultdict(bytes)
    with open(path, encoding='latin1') as f:
        for line in f:
            fields = line.split()
            if len(fields) >= 3 and fields[2] == 'ACC':
                conn += 1
                for key in [k for k in buffers if k[0] == fields[1]]:
                    del buffers[key]
                continue
            if len(fields) >= 4 and fields[3] == 'GAP':
                buffers.pop((fields[1], fields[2]), None)
                continue
            m = LINE.match(line)
            if not m:
                continue
            time, p, direction, _, hexdata = m.groups()
            if port != 'all' and p != port:
                continue
            key = (p, direction)
            b = buffers[key] + bytes.fromhex(hexdata)
            while b:
                masked = direction == 'S->C' and b[0] not in (0xC1, 0xC2, 0xC3, 0xC4)
                view = bytes(x ^ 0xAA for x in b[:3]) if masked else b[:3]
                n = _length(view)
                if n is None or n > len(b):
                    break
                if n <= 0:
                    yield Packet(conn, time, p, direction, '?', None, None, b)
                    b = b''
                    break
                raw = bytes(x ^ 0xAA for x in b[:n]) if masked else b[:n]
                b = b[n:]
                frame, serial, data = _plain(direction, raw)
                yield Packet(conn, time, p, direction, frame, serial, data, raw)
            buffers[key] = b


def walk_dirs(data):
    """Del A3 (el D3 del cliente) en claro: la celda de inicio, la dirección final y las n direcciones."""
    x, y, n, final = data[3], data[4], data[5] & 15, data[5] >> 4
    nibbles = []
    for b in data[6:]:
        nibbles += [b >> 4, b & 15]
    return x, y, final, nibbles[:n]
