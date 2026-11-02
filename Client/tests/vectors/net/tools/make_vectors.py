# Arma los vectores de envío de CI (el README.md de los vectores): el motor C1/C2 del original con
# el XOR32, escrito de nuevo desde 03-red-y-cifrado.md, "Envío", y no desde el código de MuRet; y
# el envoltorio C3/C4 con el SimpleModulus, escrito de nuevo desde 03, "C3/C4", y desde
# encrypt_block de c3c4_reference.py.
#
# Es el engine() de gen_net_send_fixtures.py, en mu-client/tests/net/fixtures del repo de research,
# con dos diferencias. Ahí el opcode de un C2 se cifra con key[3], sin evidencia, y en el binario
# va en claro (0x498b2c..0x498b89, la ventana de cartas, el único emisor de un C2). Y ahí el tamaño
# provisorio de un C2 es 00 00, y acá 01 00, como en el binario: finish() lo pisa, así que no
# cambia la salida.
#
# Uso: python3 make_vectors.py <carpeta de salida>
import os
import struct
import sys

OUT = sys.argv[1]

# La tabla del motor (03, "Envío"; XOR32_KEY en red_reference.py).
XOR32_KEY = bytes.fromhex('e76d3a89bcb29f7323a8feb6495d395d8acb638dea7d2b5fc3b1e9832951e856')

# El tope del motor (Q-C06). El original lo mira antes de cada campo y saltea el que no entra; acá
# cada byte es un campo, y ningún vector llega al tope.
ENGINE_CAP = 0x800


def engine(head, opcode, payload):
    # begin: el tipo, el tamaño provisorio y el opcode en claro.
    buf = bytearray([head, 0, opcode]) if head == 0xC1 else bytearray([head, 0x01, 0x00, opcode])
    # put: cada byte con la clave de su índice y el anterior ya cifrado.
    for value in payload:
        index = len(buf)
        if index >= ENGINE_CAP:
            break
        buf.append(value ^ XOR32_KEY[index & 31] ^ buf[index - 1])
    # finish: el tamaño, en un C2 big-endian.
    size = len(buf)
    if head == 0xC1:
        buf[1] = size & 0xFF
    else:
        buf[1] = (size >> 8) & 0xFF
        buf[2] = size & 0xFF
    return bytes(buf)


# La clave identidad de mu-client (net_send_fixtures.h): módulo 65537, clave 1 y XOR 0 en los cuatro
# carriles. Con ella cada carril queda igual, y el vector sirve para ver el envoltorio.
IDENTITY = {'mod': [65537] * 4, 'enc': [1] * 4, 'xor': [0] * 4}

# Un bloque: 8 bytes en claro dan 11. La cola es (útiles ^ 0x3D ^ ck, ck), con ck = 0xF8 XOR los 8.
PLAIN_BLOCK = 8
CIPHER_BLOCK = 11
U32 = 0xFFFFFFFF


def add_bits(dst, dst_bit, src, src_bit, count):
    # OR de `count` bits de src desde src_bit, MSB primero, en dst desde dst_bit (0x685e20).
    for k in range(count):
        s = src_bit + k
        if (src[s >> 3] >> (7 - (s & 7))) & 1:
            d = dst_bit + k
            dst[d >> 3] |= 0x80 >> (d & 7)
    return dst_bit + count


def encrypt_block(key, plain, useful):
    # 0x685bb0: los cuatro carriles, la segunda pasada del 2 al 0, 18 bits por carril (los 16 de
    # abajo y después los bits 17 y 16, que en el dword little-endian están en la posición 22) y la
    # cola.
    out = bytearray(CIPHER_BLOCK)
    words = struct.unpack('<4H', plain)
    lanes, previous = [0] * 4, 0
    for i in range(4):
        lanes[i] = (((key['xor'][i] ^ words[i] ^ previous) * key['enc'][i]) & U32) % key['mod'][i]
        previous = lanes[i] & 0xFFFF
    following = lanes[3] & 0xFFFF
    for i in (2, 1, 0):
        old = lanes[i]
        lanes[i] = (old ^ key['xor'][i] ^ following) & U32
        following = old & 0xFFFF
    bit = 0
    for lane in lanes:
        packed = struct.pack('<I', lane)
        bit = add_bits(out, bit, packed, 0, 16)
        bit = add_bits(out, bit, packed, 22, 2)
    check = 0xF8
    for value in plain:
        check ^= value
    add_bits(out, bit, bytes([(useful ^ 0x3D ^ check) & 0xFF, check]), 0, 16)
    return bytes(out)


def wrap(key, packet, serial, padding):
    # El envoltorio de cada emisor (0x4e2198..0x4e2500): el serial pisa el tamaño (pkt[1] en un C1,
    # pkt[2] en un C2) y se cifra desde ahí; detrás de lo útil va el byte de relleno, y el resto del
    # último bloque en 0. Un C1 sale como C3 si lo cifrado mide menos de 0x100 bytes, y si no, como
    # C4. Un C2 sale siempre como C4, como en la ventana de cartas (0x498820), el único emisor de C2.
    start = 1 if packet[0] == 0xC1 else 2
    useful = bytearray(packet[start:])
    useful[0] = serial
    padded = bytes(useful) + bytes([padding]) + bytes(PLAIN_BLOCK)
    cipher = bytearray()
    for offset in range(0, len(useful), PLAIN_BLOCK):
        cipher += encrypt_block(key, padded[offset:offset + PLAIN_BLOCK], min(len(useful) - offset, PLAIN_BLOCK))
    if packet[0] == 0xC1 and len(cipher) < 0x100:
        return bytes([0xC3, len(cipher) + 2]) + bytes(cipher)
    size = len(cipher) + 3
    return bytes([0xC4, size >> 8, size & 0xFF]) + bytes(cipher)


def write(name, data):
    with open(os.path.join(OUT, name), 'wb') as out:
        out.write(data)
    print(f'{name}: {len(data)} bytes')


# El C2 largo de test_net_send.c: opcode 0x42 y 296 bytes, el i-ésimo i * 37 + 11, que dan 300.
c2_long = engine(0xC2, 0x42, [(index * 37 + 11) & 0xFF for index in range(296)])
assert len(c2_long) == 300 and c2_long[:4] == bytes([0xC2, 0x01, 0x2C, 0x42])
write('c2-long.bin', c2_long)

# El mismo C2 como C4, con la clave identidad, el serial 2 y el relleno en 0: es el FIX_C4_SERIAL2
# de test_net_send.c, armado sobre el C2 de arriba. 298 bytes útiles son 38 bloques: 418 cifrados y
# 3 de cabecera.
c4_serial2 = wrap(IDENTITY, c2_long, 2, 0)
assert len(c4_serial2) == 421 and c4_serial2[:3] == bytes([0xC4, 0x01, 0xA5])
write('c4-serial2.bin', c4_serial2)
