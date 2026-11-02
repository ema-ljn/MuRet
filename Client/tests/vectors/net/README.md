# Vectores de red

Paquetes sintéticos para los tests de `PacketWriter`, del XOR32 y de los C3/C4, demasiado largos
para escribirlos en el test. Están en el repo para que corran en CI, que no tiene el cable del
laboratorio. No son paquetes del cable: salen de una fórmula, y ninguno lleva un login.

| Vector | Tamaño | Qué es | Para qué |
| --- | --- | --- | --- |
| `c2-long.bin` | 300 | un C2 con el opcode `0x42` y 296 bytes, el i-ésimo `(i * 37 + 11) mod 256`, cifrado con el XOR32 del motor | un C2 de tamaño con el byte alto distinto de 0 (`C2 01 2C`), y la clave que da la vuelta |
| `c4-serial2.bin` | 421 | `c2-long.bin` como C4, con la clave identidad de `mu-client` (módulo 65537, clave 1 y XOR 0), el serial 2 y el relleno en 0 | un C4 de 38 bloques, armado y descifrado por `EncryptPacket` y `DecryptPacket` |

## De dónde salen

`tools/make_vectors.py` los arma con el motor de envío del original escrito de nuevo desde
[03](../../../docs/research/03-red-y-cifrado.md#envío-el-motor-c1c2-y-el-xor32), y el
envoltorio C3/C4 desde
[03](../../../docs/research/03-red-y-cifrado.md#c3c4-cifrado-por-bloques-con-enc1dat-y-dec2dat) y
`encrypt_block` de `c3c4_reference.py` (en el repo de research), no desde el código de MuRet:

```bash
python3 tools/make_vectors.py .
```

El C2 largo es el `FIX_C2_PLAIN` de `test_net_send.c`, en `mu-client` (el repo de research), que lo
genera `tests/net/fixtures/gen_net_send_fixtures.py`, con el mismo contenido. No se copió: ahí el
opcode de un C2 se cifra con `key[3]`, sin evidencia, y en el binario va en claro (la ventana de
cartas, `0x498b2c..0x498b89`, el único emisor de un C2). Por eso los dos difieren desde el byte 3.

El C4 es el `FIX_C4_SERIAL2` de `test_net_send.c`, armado sobre este C2 en vez del de `mu-client`.
El envoltorio del script da los cuatro paquetes cifrados de `net_send_fixtures.h` a partir de los
suyos (`FIX_C3_SERIAL0`, `FIX_C3_RANDA5`, `FIX_C3_SERIALFF` y `FIX_C4_SERIAL2`), y su
`encrypt_block` da lo mismo que el de `c3c4_reference.py`. Un C2 lo arma siempre como C4, como la
ventana de cartas del original; `mu-client` decide por el largo cifrado también en un C2, y en el
de 300 bytes da lo mismo, porque lo cifrado pasa de `0x100`.
