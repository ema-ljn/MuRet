# Vectores de red

Paquetes sintéticos para los tests de `PacketWriter` y del XOR32, demasiado largos para escribirlos
en el test. Están en el repo para que corran en CI, que no tiene el cable del laboratorio. No son
paquetes del cable: salen de una fórmula, y ninguno lleva un login.

| Vector | Tamaño | Qué es | Para qué |
| --- | --- | --- | --- |
| `c2-long.bin` | 300 | un C2 con el opcode `0x42` y 296 bytes, el i-ésimo `(i * 37 + 11) mod 256`, cifrado con el XOR32 del motor | un C2 de tamaño con el byte alto distinto de 0 (`C2 01 2C`), y la clave que da la vuelta |

## De dónde salen

`tools/make_vectors.py` los arma con el motor de envío del original escrito de nuevo desde
[03](../../../docs/research/03-red-y-cifrado.md#envío-el-motor-c1c2-y-el-xor32), y no desde el
código de MuRet:

```bash
python3 tools/make_vectors.py .
```

El C2 largo es el `FIX_C2_PLAIN` de `test_net_send.c`, en `mu-client` (el repo de research), que lo
genera `tests/net/fixtures/gen_net_send_fixtures.py`, con el mismo contenido. No se copió: ahí el
opcode de un C2 se cifra con `key[3]`, sin evidencia, y en el binario va en claro (la ventana de
cartas, `0x498b2c..0x498b89`, el único emisor de un C2). Por eso los dos difieren desde el byte 3.
