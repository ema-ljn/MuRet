# Arma los vectores de envío de CI (el README.md de los vectores): el motor C1/C2 del original con
# el XOR32, escrito de nuevo desde 03-red-y-cifrado.md, "Envío", y no desde el código de MuRet.
#
# Es el engine() de gen_net_send_fixtures.py, en mu-client/tests/net/fixtures del repo de research,
# con una diferencia: ahí el opcode de un C2 se cifra con key[3], sin evidencia, y en el binario va
# en claro (0x498b2c..0x498b89, la ventana de cartas, el único emisor de un C2).
#
# Uso: python3 make_vectors.py <carpeta de salida>
import os
import sys

OUT = sys.argv[1]

# La tabla del motor (03, "Envío"; XOR32_KEY en red_reference.py).
XOR32_KEY = bytes.fromhex('e76d3a89bcb29f7323a8feb6495d395d8acb638dea7d2b5fc3b1e9832951e856')

# Con 0x800 bytes el motor deja de agregar (Q-C06).
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


def write(name, data):
    with open(os.path.join(OUT, name), 'wb') as out:
        out.write(data)
    print(f'{name}: {len(data)} bytes')


# El C2 largo de test_net_send.c: opcode 0x42 y 296 bytes, el i-ésimo i * 37 + 11, que dan 300.
c2_long = engine(0xC2, 0x42, [(index * 37 + 11) & 0xFF for index in range(296)])
assert len(c2_long) == 300 and c2_long[:4] == bytes([0xC2, 0x01, 0x2C, 0x42])
write('c2-long.bin', c2_long)
