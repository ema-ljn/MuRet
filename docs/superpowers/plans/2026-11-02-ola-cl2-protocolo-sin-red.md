# Plan de la ola CL2: Protocolo sin red

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md).
> Escrito el 2026-11-02, con lo aprendido en la CL1. Cada sub-ola trae su contrato, sus nombres y
> sus tests; lo que se aprenda en una se escribe acá antes de arrancar la siguiente.

**Objetivo:** pasar del cable a paquetes y de paquetes al cable igual que el `main.exe`
`c32bbd5f`, sin abrir un socket: el corte C1/C2/C3/C4, el XOR32 de lo que manda el cliente, el
cifrado C3/C4 con sus seriales, la capa de FoxAnti y los paquetes del login. Todo se prueba con
bytes literales (siempre corren) y contra el cable real del laboratorio (si está).

**Arquitectura:** dos capas nuevas en `ClientLib`, como dice el roadmap. `Client::Network`
(`Client/src/Network/`) corta, escribe, cifra y ofusca paquetes: recibe bytes y devuelve bytes,
sin sockets, que son de la CL4. `Client::Protocol` (`Client/src/Protocol/`) pasa de paquetes a
structs y de structs a paquetes, siempre en el formato que lee `main.exe`. `Network` no sabe qué
significa cada opcode y `Protocol` no sabe de cifrado. Como toda N.0, la CL2.0 vive en `labs/`.

**Stack:** C++20 (`std::byte`, `std::span`, `std::array`, `std::optional`, aritmética sin signo
de 32 bits, `std::mt19937` con semilla fija), GoogleTest. No entra ninguna biblioteca
(`CODING_RULES.md` §10): el corte, el XOR32, el `SimpleModulus` y FoxAnti son del MU, y
empaquetar bits son unas pocas líneas.

**Guía de estudio:** `docs/learning/ola-cl2.0-flujos-y-bloques.md`, que se escribe al arrancar la
CL2.0 (L-10). El flujo TCP y la máquina de estados ya están en la 1.2 de la
[guía de la ola 1 del servidor](../../learning/ola-1-protocolo.md): la guía nueva la usa y suma lo
del cliente. De la CL2.1 en adelante, la sección de cada sub-ola de este plan.

**Research que fija esta ola:**
- [03-red-y-cifrado](../../../Client/docs/research/03-red-y-cifrado.md): la cadena de recepción,
  el corte, el motor C1/C2 con el XOR32, C3/C4 y la capa de FoxAnti.
- [04-protocolo](../../../Client/docs/research/04-protocolo.md): el flujo de conexión y los
  paquetes del login, con sus bytes del cable.
- Los quirks Q-C03, Q-C04, Q-C05, Q-C06, Q-C09, Q-C10, Q-C13, Q-C17 y Q-C18 de
  [07](../../../Client/docs/research/07-quirks-y-bugs.md).
- En `client-research/` del repo de research: `analysis/mainexe/scripts/red_reference.py` (la
  recepción; `--selftest` corre 8 casos de contrato) y `c3c4_reference.py` (C3/C4); la evidencia
  de `analysis/mainexe/evidence/s35-red/agentes/` (el corte en `004e893d-corte-paquetes.md`, el
  descifrado en `004e1d50-protocol-compiler.md`); la reimplementación en C de `mu-client`
  (`src/net/`, `src/packet/` y sus tests en `tests/net/` y `tests/packet/`, con los fixtures de
  `tests/net/net_send_fixtures.h`, armados con claves y credenciales sintéticas), y la FoxAnti
  mínima (`analysis/foxanti/minimal/src/packet.c`). Son oráculos para comparar: no se copian.

**Terminada cuando** (roadmap):
- pasan en CI los vectores del cable y los fixtures de `mu-client` para el corte, el XOR32, C3/C4,
  FoxAnti, el login y el keepalive;
- con el cable del laboratorio (pregunta 4), sus 72 conexiones se cortan sin bytes de más, los 183
  C3 del cliente se descifran y se vuelven a cifrar con los mismos bytes, y los 25 C3/C4 del
  servidor se descifran con su checksum bien y sus seriales en orden.

## Preguntas para decidir al empezar

1. **Quién escribe la CL2.** En la CL1 pediste seguir con el cliente y Claude la escribió como
   ejemplo trabajado. *Recomendación:* igual, con la CL2.0 en `labs/` para vos y un paso
   `TODO(human)` chico por sub-ola (marcados abajo). Si la querés escribir vos, los pasos ya están
   en el orden en que se hace: primero el test.
2. **Código compartido con los servidores** (pregunta 2 del roadmap). El corte C1/C2 lo van a
   necesitar el DataServer (el `FrameParser` de su ola 1) y el ConnectServer (ola 10). C3/C4, el
   XOR32 y FoxAnti, solo el cliente: el GameServer queda fuera del roadmap del servidor.
   *Recomendación:* en esta ola no nace `Common/`. `Network` depende solo del estándar y de
   `Client::Core` (`Expected`, `ByteReader`), así que pasar el corte a un `Common/` en la raíz es
   mover archivos. Se decide en la 1.2 del servidor, el segundo que lo usa: reusarlo, o escribir
   el `FrameParser` como el ejercicio que es en su plan.
3. **Hasta qué tamaño se lee un paquete** (Q-C05). El original recibe en un buffer de `0x2000`
   bytes: un paquete más grande nunca entra entero, el buffer se llena y la conexión se cierra
   ("Receive Packet Buffer Overflow", `0x4e88a0`). Además descifra en ~2.046 bytes de la pila, y un
   C4 de más de ~2,8 KB la desborda. *Recomendación:* `MaxPacketSize = 0x2000` para los cuatro
   tipos, como error apenas llega la cabecera, y el descifrado escribe en un buffer del tamaño de
   lo que entra. En el cable del laboratorio el más grande mide 254 bytes, un C2 del GameServer.
4. **Dónde está el cable para los tests golden.** El `muproxy.log` del laboratorio (72 conexiones,
   en `mu-online-reverse-lab/analysis/mainexe/evidence/`) y las claves del servidor (`Dec1.dat` y
   `Enc2.dat`, en `gameserver/evidence/keyfiles/` del repo de research) no entran en el Git de
   MuRet: el cable lleva el login. *Recomendación:* una variable `MURET_CLIENT_WIRE_DIR` con una
   carpeta que tenga esos tres archivos, con la regla de la golden (sin variable se saltea,
   incompleta falla). `Enc1.dat` y `Dec2.dat` del cliente ya están en la carpeta golden. Los tests
   con el cable no imprimen un payload, tampoco al fallar: dicen conexión, índice, cabecera,
   opcode, serial y offset, nunca el valor de un byte.
5. **Qué lleva el relleno del último bloque C3/C4.** El original completa el bloque de 8 bytes
   con un byte de `rand()` y lo que haya en la pila, y con FoxAnti `send_forward` procesa también
   ese primer byte (llega hasta el índice n, uno más que el paquete). El servidor no lo lee:
   `Decrypt` devuelve solo el largo útil. *Recomendación:* el primer byte de relleno es un
   parámetro (0 por defecto; los tests fijan el de `rand()`, como `mu-client`), el resto va en
   cero, y FoxAnti lo procesa como el original. Así los fixtures dan los mismos bytes, y en el
   juego el cable difiere del original solo en el relleno, que el original tampoco repite.

## Lo que más puede morder

Cada uno tiene su test en la sub-ola que lo dice, salvo el último, que es una regla de los tests
con el cable. En la review se miran primero:

- Un C2/C4 cortado después de su primer o su segundo byte (Q-C03, CL2.1).
- En una misma lectura, un paquete exento del `0xAA` al lado de uno enmascarado (Q-C18, CL2.4).
- Un largo o una cantidad que no entra en el paquete: la lista `F4 02` con su cantidad de más, un
  `C1 04 F3 11` (Q-C09, CL2.4), un C3 que no trae bloques enteros (CL2.3).
- Un producto que da la vuelta en 32 bits, con un carril cifrado mayor que su módulo, y un
  `std::uint16_t * std::uint16_t`, que se promueve a `int` y puede desbordar (CL2.3).
- El serial que da la vuelta de 255 a 0, en el envío y en la recepción (CL2.3).
- Un test con el cable que al fallar imprime bytes del login (pregunta 4).

---

## Sub-ola CL2.0: flujos y bloques (N.0, en `labs/`)

**Concepto:**
- TCP entrega un flujo de bytes, no mensajes: cortarlo en paquetes es una máquina de estados (un
  autómata finito), y un paquete puede llegar partido en cualquier byte;
- un cifrado de flujo encadenado (el XOR32: cada byte depende del anterior ya cifrado) y uno por
  bloques (`SimpleModulus`: 8 bytes en claro dan 11 cifrados, con relleno y checksum), y por qué
  ninguno protege nada si la clave está en el binario: es ofuscación;
- aritmética modular: multiplicar módulo m, el inverso modular con el algoritmo de Euclides
  extendido, y por qué la clave de cifrado del cliente es la inversa de la de descifrado del
  servidor;
- bits MSB primero: campos de 18 bits empaquetados en un arreglo de bytes.

Del C++: operadores de bits sobre `std::byte` y `std::to_integer`; la aritmética sin signo que da
la vuelta (definida) contra la con signo (comportamiento indefinido), y la promoción de
`std::uint16_t` a `int` antes de multiplicar; `enum class` para los estados; `std::mt19937` con
semilla fija para un test de propiedad.

**Archivos:**
- Crear: `docs/learning/ola-cl2.0-flujos-y-bloques.md` (la guía; Claude)
- Crear: `labs/cl2.0-flujos/FrameCutter.h/.cpp`, `BitStream.h/.cpp` y `ModularInverse.h/.cpp`,
  sus tests y `jefe/OldCutTest.cpp` (Emanuel)
- Modificar: `labs/CMakeLists.txt`, `labs/README.md`

Necesita GoogleTest en `labs/`, el paso 1 de la CL1.0, que todavía no se hizo: va primero.

- [ ] **Paso 1 (Claude):** la guía, justo antes de empezar.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** `Labs::FrameCutter`, alimentado de a un byte, con los
  estados explícitos en un `enum class` (tipo, tamaño, cuerpo). Primero los tests: los casos de
  `red_reference.py --selftest` que no son del cifrado (corte, resto parcial, cabecera incompleta,
  cabecera inválida y tamaño 0), y uno de propiedad: el mismo flujo partido al azar da los mismos
  paquetes.
- [ ] **Paso 3 (Emanuel):** `Labs::BitWriter` y `Labs::BitReader`, MSB primero:
  `Write(value, bits)` y `Read(bits)`. Tests: el primer bit escrito es el `0x80` del byte 0, y
  cuatro campos de 18 bits más uno de 16 ocupan justo 11 bytes y se leen iguales.
- [ ] **Paso 4 (Emanuel):** `Labs::ModularInverse(a, m)`, con Euclides extendido, que devuelve
  `std::nullopt` si no hay inversa. Tests: uno a mano (3 módulo 7 es 5), uno sin inversa, y que
  para un módulo de 18 bits el producto de los dos módulo m da 1.
- [ ] **Paso 5 (Emanuel, jefe):** Q-C03. El corte del original sobre un buffer que se reusa: con
  los dos primeros bytes de un C2 al final de una lectura, el tamaño se arma con un byte viejo; si
  da 0, lo que queda no se corre al principio y la lectura siguiente se pega detrás de bytes ya
  usados. El test reproduce el caso 1b de `red_reference.py`: `C1 05 10 01 02` y
  `C2 00 06 20 03 04`, partidos después de `C2 00`, dan en el original un segundo paquete
  `C1 05 06 20 03`. Después, que el `FrameCutter` espera los bytes que faltan.
- [ ] **Paso 6 (carril R):** en Ghidra, el corte de `0x4e893d`: las lecturas de `buf[off + 1]` y
  `buf[off + 2]` sin mirar cuántos bytes quedan, y el `cmp eax,0x2000` de `0x4e88a0`.
- [ ] **Paso 7:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "FrameCutter|BitStream|ModularInverse|OldCut"
  ```

## Sub-ola CL2.1: el corte C1/C2/C3/C4

**Concepto:** un lector de paquetes: entra un flujo de bytes, salen paquetes enteros, y nunca se
lee un byte que no llegó.

**Contrato** ([03 § Cadena de recepción](../../../Client/docs/research/03-red-y-cifrado.md#cadena-de-recepción)
y `004e893d-corte-paquetes.md`):

| Qué | Original | MuRet |
| --- | --- | --- |
| tamaño | `C1` y `C3` lo llevan en `pkt[1]`; `C2` y `C4`, en `pkt[1..2]` big-endian. Es el del paquete entero | igual |
| cabecera incompleta (Q-C03) | con 1 o 2 bytes de un C2/C4 al final, arma el tamaño con bytes viejos; si da 0, el flujo queda corrido | espera los bytes que faltan |
| tipo inválido | descarta todo lo acumulado, también los paquetes buenos que vengan detrás, y sigue | `PacketError::InvalidType`, y el error queda: la CL4 cierra la conexión |
| tamaño menor que la cabecera y el opcode | con 0 no avanza nunca, y el buffer se llena hasta cerrar; con 1 o 2, saca un paquete sin opcode | `PacketError::SizeTooSmall` (C1 y C3 miden al menos 3; C2 y C4, 4) |
| tamaño de más (Q-C05) | un paquete de más de `0x2000` bytes no entra en el buffer, que se llena y cierra | `PacketError::TooLarge` apenas llega la cabecera (pregunta 3) |
| copia | cada paquete va a la cola en un buffer de `0x2004` bytes con un `0xFD` detrás | cada `Packet` es dueño de sus bytes, sin centinela |

Las tres reglas de MuRet corrigen bugs internos (07): ningún servidor bueno manda esos paquetes,
y lo que hace el original con ellos no es contrato.

**Archivos:**
- **PR A:** `ByteReader` pasa de `Client::Data` a `Client::Core`
  (`Client/src/Core/ByteReader.h/.cpp`, y su test a `Client/tests/core/`), con `ReadU16Be` para el
  tamaño de un C2, el primer entero big-endian del cliente. Lo van a usar `Data`, `Network` y
  `Protocol`; en `Data` cambia el include.
- **PR B:** crear `Client/src/Network/Packet.h/.cpp`, `PacketError.h` y `PacketReader.h/.cpp`;
  `Client/tests/network/PacketTest.cpp`, `PacketReaderTest.cpp` y `GoldenPacketReaderTest.cpp`;
  `Client/tests/support/WireCapture.h/.cpp`, que lee el `muproxy.log` (una línea por segmento TCP:
  hora, puerto, sentido, `len=` y los bytes en hex; `ACC` abre una conexión). Modificar
  `Client/tests/support/GoldenDir.h/.cpp` (`MURET_CLIENT_WIRE_DIR`), `SameBytes.h`
  (`SameBytesWithoutValues`, que dice dónde difieren y no qué valen), `Client/CMakeLists.txt`,
  `Client/README.md`, PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Network
{
enum class PacketType : std::uint8_t { C1 = 0xC1, C2 = 0xC2, C3 = 0xC3, C4 = 0xC4 };

// El tipo y el tamaño: 2 bytes en C1 y C3, 3 en C2 y C4.
std::size_t HeaderSize(PacketType type);

// El paquete más grande que se lee o se escribe (pregunta 3).
inline constexpr std::size_t MaxPacketSize = 0x2000;

class Packet
{
public:
    // Valida el tipo y que el tamaño que declara sea el que mide.
    static Expected<Packet, PacketError> FromBytes(std::vector<std::byte> bytes);
    PacketType Type() const;
    std::span<const std::byte> Bytes() const; // con la cabecera
    // pkt[2] en un C1 y pkt[3] en un C2; std::nullopt en C3 y C4, que lo llevan cifrado.
    std::optional<std::uint8_t> Opcode() const;
};

class PacketReader
{
public:
    void Feed(std::span<const std::byte> bytes);
    // El paquete que sigue, std::nullopt si falta que llegue, o el error, que se repite desde ahí.
    [[nodiscard]] Expected<std::optional<Packet>, PacketError> Next();
};
} // namespace Client::Network
```

- [ ] **Paso 1 (PR A):** mover `ByteReader` y sumar `ReadU16Be`. Test: `01 2C` da `0x012C`.
- [ ] **Paso 2:** los tests de `Packet`: `FromBytes` acepta `C1 04 20 AA` y `C2 00 05 52 AA`, y
  rechaza un tamaño distinto del que mide, un tipo `C5` y nada; `Opcode()` en C1 y C2, y
  `std::nullopt` en C3 y C4.
- [ ] **Paso 3:** `Packet`.
- [ ] **Paso 4 (Emanuel, `TODO(human)`):** la tabla de vectores del lector, antes del código:
  - `C1 04 20 AA` da un paquete, y `C2 01 2C` con 297 bytes más, uno de 300;
  - un C3 y un C4 se cortan como un C1 y un C2, sin descifrarlos;
  - el caso 1 de `red_reference.py`: `C1 05 10 01 02` y los 3 primeros bytes de
    `C2 00 06 20 03 04` en un `Feed`, y el resto en otro, dan los dos paquetes;
  - Q-C03: el mismo par partido después de `C2` y después de `C2 00` da los mismos dos paquetes
    (el original, en el segundo caso, da `C1 05 06 20 03`, el jefe de la CL2.0);
  - `C1 03` y después `00` dan un paquete;
  - varios paquetes en un `Feed` (en el cable hubo 337 lecturas con 2 a 7);
  - `00 05 01 02 03` seguido de un paquete bueno es `InvalidType`, y el bueno no sale;
  - `C1 00`, `C1 01`, `C1 02`, `C2 00 00` y `C2 00 03` son `SizeTooSmall`;
  - `C2 20 00` espera sus `0x2000` bytes, y `C2 20 01` es `TooLarge` con solo la cabecera;
  - después de un error, `Next()` lo repite aunque llegue más;
  - el mismo flujo partido al azar (con semilla fija) da los mismos paquetes.
- [ ] **Paso 5:** `PacketReader`.
- [ ] **Paso 6:** el golden con el cable: el sentido C->S de las 72 conexiones se corta en 689
  paquetes sin bytes de más (al ConnectServer, 144 C1; al GameServer, 362 C1 y 183 C3), y el S->C
  de las 64 conexiones al ConnectServer, que va en claro, en 144 (72 C1 y 72 C2). El S->C del
  GameServer lleva el `0xAA`: va en la CL2.4.
- [ ] **Paso 7:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "ByteReader|Packet"
  ```

## Sub-ola CL2.2: `PacketWriter` y XOR32

**Concepto:** escribir un paquete campo por campo, y un cifrado encadenado como etapa aparte.

**Contrato** ([03 § Envío](../../../Client/docs/research/03-red-y-cifrado.md#envío-el-motor-c1c2-y-el-xor32)
y `mu-client/docs/component-net-envio.md`):

| Qué | Original | MuRet |
| --- | --- | --- |
| empezar | C1: `buf[0] = C1`, `buf[2] = op`, tamaño 3. C2: `buf[0] = C2`, `buf[3] = op ^ key[3]`, tamaño 4 | `PacketWriter::C1(op)` y `C2(op)` arman el paquete sin XOR32 |
| XOR32 | cada byte nuevo en el índice `i >= 3`: `valor ^= key[i & 31] ^ buf[i - 1]`, con `buf[i - 1]` ya cifrado. En un C2 el byte 3 se encadena con un `buf[2]` que todavía vale 0, porque el tamaño se escribe al final | `Xor32Encode`, aparte; en un C2, el byte 3 con un previo 0 y no con el tamaño (*estático*: en el cable del laboratorio el cliente no manda C2) |
| terminar | escribe el tamaño, en un C2 big-endian | `Finish()` |
| tope (Q-C06) | `0x800` bytes, y lo que sobra se ignora en silencio; en un C1 el tamaño es un byte, y con más de 255 sale truncado (`test_engine_cap`: con `0x800` da `C1 00`) | `PacketError::TooLarge` con más de 255 en un C1 y más de `0x800` en un C2: nunca sale un paquete truncado |
| quién lo usa | casi todo lo que manda el cliente; los `F4` al ConnectServer no: `0x4c6070` arma el pedido sin el motor, y el `F4 03` llega con su `03` en claro | cada paquete de `Protocol` aplica el XOR32 si lo lleva (CL2.5) |

**Archivos:** crear `Client/src/Network/PacketWriter.h/.cpp` y
`Client/src/Network/Crypto/Xor32.h/.cpp`; `Client/tests/network/PacketWriterTest.cpp` y
`Xor32Test.cpp`, y los fixtures largos de
`mu-client` en `Client/tests/vectors/net/` como binarios, con un README que dice de dónde sale cada
uno (`tests/net/fixtures/gen_net_send_fixtures.py` del research) y su línea en `.gitattributes`.
Modificar `Client/CMakeLists.txt`, 03 (el byte 3 del C2), PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Network
{
class PacketWriter
{
public:
    static PacketWriter C1(std::uint8_t opcode);
    static PacketWriter C2(std::uint8_t opcode);
    PacketWriter& PutU8(std::uint8_t value);
    PacketWriter& PutU16Le(std::uint16_t value);
    PacketWriter& PutU16Be(std::uint16_t value);
    PacketWriter& PutU32Le(std::uint32_t value);
    PacketWriter& PutBytes(std::span<const std::byte> bytes);
    [[nodiscard]] Expected<Packet, PacketError> Finish() const;
};
} // namespace Client::Network

namespace Client::Network::Crypto
{
// La tabla de 32 bytes del motor (03 § Envío).
inline constexpr std::array<std::uint8_t, 32> Xor32Key{/* e7 6d 3a 89 ... e8 56 */};
// Un C1 o un C2 entero; un C3 o un C4 vuelve igual.
[[nodiscard]] Packet Xor32Encode(Packet packet);
[[nodiscard]] Packet Xor32Decode(Packet packet);
} // namespace Client::Network::Crypto
```

- [ ] **Paso 1:** los tests:
  - los dos del cable del 04: `C1 04 F3 00` sale como `C1 04 F3 7A`, y `C1 06 A9 00 00 01`, como
    `C1 06 A9 20 9C 2F`;
  - los de `test_net_send.c`: `FIX_F1_PLAIN` (el aviso `C1 06 F1 03 06 00`, CL2.3) y
    `FIX_C2_PLAIN` (opcode `0x42` y 296 bytes, el i-ésimo `i * 37 + 11`, que dan `C2 01 2C`), y los
    tamaños 255, 256, 300 y `0x800` en big-endian;
  - la clave vuelve a empezar en el byte 32: el 35 usa `key[3]`;
  - `Xor32Decode(Xor32Encode(p))` es `p` en C1 y en C2, y el byte 2 de un C1 no cambia;
  - Q-C06: un C1 de 255 bytes entra y uno de 256 no; un C2 de `0x800` entra y uno de `0x801` no.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** el byte 3 y el 4 de `C1 06 A9 00 00 01`, a mano con la
  tabla, antes de correr el test.
- [ ] **Paso 3:** `PacketWriter`, `Xor32Encode` y `Xor32Decode`.
- [ ] **Paso 4:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "PacketWriter|Xor32"
  ```

## Sub-ola CL2.3: C3/C4 y los seriales

**Concepto:** un cifrado por bloques con aritmética modular, bits MSB primero, y un contador por
conexión que detecta paquetes perdidos o repetidos.

**Contrato** ([03 § C3/C4](../../../Client/docs/research/03-red-y-cifrado.md#c3c4-cifrado-por-bloques-con-enc1dat-y-dec2dat),
`c3c4_reference.py` y `004e1d50-protocol-compiler.md`):

| Qué | Original | MuRet |
| --- | --- | --- |
| archivo de clave | `0x686240`: magic `0x1112` (u16), tamaño total (u32) y bloques de 4 dwords, todo little-endian; cada dword con XOR de la tabla de `0x6d84fc` (`0x3F08A79B 0xE25CC287 0x93D27AB9 0x20DEA7BF`, la misma en los dos builds del laboratorio). `Enc1.dat` trae módulo, clave de cifrado y XOR; `Dec2.dat`, módulo, clave de descifrado y XOR: 54 bytes | `LoadEncryptionKey` y `LoadDecryptionKey`; otro magic u otro tamaño es `CryptoError::InvalidKeyFile` |
| bloque | 8 bytes en claro dan 11: cuatro carriles de 16 bits, cada uno `((xor ^ w ^ previo) * clave) mod módulo`; una segunda pasada con XOR; 18 bits por carril, MSB primero; y 2 bytes de cola con el largo útil y un checksum (`0xF8` XOR los 8 bytes; `0x3D` en la cola) | `EncryptBlock` y `DecryptBlock`, con los productos en `std::uint32_t`, que dan la vuelta como el `IMUL` |
| bits de un carril | de los 16 de abajo van sus dos bytes en orden little-endian, cada uno con su bit más alto primero; después los bits 16 y 17 | igual: el algoritmo exacto es `encrypt_block` y `decrypt_block` de `c3c4_reference.py` |
| paquete | `0x685ac0`: el C1/C2 ya con XOR32, con el serial en lugar del tamaño (`[1]` en un C1, `[2]` en un C2), cifrado desde ahí: `size - 1` bytes útiles en un C1, `size - 2` en un C2. Si lo cifrado mide menos de `0x100` sale como C3; si no, como C4 | `EncryptPacket` |
| relleno | un byte de `rand()` y la pila (pregunta 5) | un parámetro y ceros |
| serial de envío | un byte global (`0x05767913`): 0 al conectar al GameServer (`0x4c1340`), +1 por C3/C4, de 255 vuelve a 0 | `PacketSerials`, uno por conexión |
| descifrado | `0x685b30`: de a 11 bytes; con un checksum malo, -1 | `DecryptPacket`; `CryptoError::ChecksumMismatch` |
| largo útil | suma el de cada bloque sin mirarlo | un bloque que dice más de 8, o uno que no es el último con menos de 8, es `CryptoError::InvalidBlock` |
| bloques enteros | lee de a 11 sin mirar si alcanzan; el checksum casi seguro falla (*análisis*) | `CryptoError::NotWholeBlocks` |
| serial de recepción (Q-C04) | `0x05767914`: si no coincide, guarda el recibido sin sumar 1, loguea y despacha igual; el desfase queda para siempre | se resincroniza con el recibido + 1, despacha igual y lo informa (`SerialCheck::Resynced`) para que la CL4 lo loguee una vez |
| checksum malo | no despacha, suma 1 al serial de recepción y manda `C1 06 F1 03 06 00` (con XOR32) como C3 (`004e1d50-protocol-compiler.md`, confirmado en el asm; qué hace el servidor con él no se sabe) | igual con cualquier `CryptoError` del descifrado: el serial avanza y vuelve el error; el aviso lo arma la CL2.5 y lo manda la CL4. Es nuevo para el 03: entra con esta sub-ola |
| rearmado (Q-C10) | `[C1][n]` y lo descifrado sin el serial: declara n y ocupa n + 1 (C4: n + 2) | declara lo que ocupa; menos de 2 bytes descifrados (serial y opcode) es `CryptoError::TooShort` |

Las claves reales cumplen lo que supone `c3c4_reference.py`: módulos de 17 y 18 bits, claves de
15 bits como mucho, y `clave de descifrado * (módulo - 1)` entra en 32 bits. `Enc1.dat` es la
inversa de `Dec1.dat` (mismo módulo, mismo XOR, y el producto de las claves es 1 módulo m), y
`Dec2.dat` la de `Enc2.dat`: medido con los archivos del laboratorio.

**Archivos:**
- **PR A:** crear `Client/src/Network/Crypto/CryptoError.h`, `SimpleModulus.h/.cpp` (la clave y
  los bloques) y `SimpleModulusKeyFile.h/.cpp`; `Client/tests/network/SimpleModulusTest.cpp`,
  `SimpleModulusKeyFileTest.cpp` y `GoldenSimpleModulusKeyFileTest.cpp`.
- **PR B:** crear `Client/src/Network/PacketSerials.h/.cpp` y `EncryptedPacket.h/.cpp`;
  `Client/tests/network/PacketSerialsTest.cpp`, `EncryptedPacketTest.cpp` y
  `GoldenEncryptedPacketTest.cpp`. Modificar 03 (el aviso `F1 03`, la regla de C3 o C4 por el
  largo cifrado y el relleno) y 07 (Q-C04 y Q-C10 con sus tests).
- En los dos: `Client/CMakeLists.txt`, PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Network::Crypto
{
struct SimpleModulusKey
{
    std::array<std::uint32_t, 4> modulus;
    std::array<std::uint32_t, 4> key; // de cifrado o de descifrado, según el archivo
    std::array<std::uint32_t, 4> xorKey;
};
// Enc1.dat del cliente y Enc2.dat del servidor.
Expected<SimpleModulusKey, CryptoError> LoadEncryptionKey(std::span<const std::byte> file);
// Dec2.dat del cliente y Dec1.dat del servidor.
Expected<SimpleModulusKey, CryptoError> LoadDecryptionKey(std::span<const std::byte> file);

std::array<std::byte, 11> EncryptBlock(const SimpleModulusKey& key, std::span<const std::byte, 8> plain,
                                       std::uint8_t usefulSize);
struct DecryptedBlock
{
    std::array<std::byte, 8> bytes; // los 8, relleno incluido
    std::uint8_t usefulSize;
};
// std::nullopt si el checksum no coincide.
std::optional<DecryptedBlock> DecryptBlock(const SimpleModulusKey& key, std::span<const std::byte, 11> cipher);
} // namespace Client::Network::Crypto

namespace Client::Network
{
enum class SerialCheck { InOrder, Resynced };

class PacketSerials
{
public:
    std::uint8_t TakeSend();                       // el que sale; suma 1
    SerialCheck Receive(std::uint8_t received);    // Q-C04
    void SkipReceive();                            // un descifrado que falló: suma 1
    void Reset();                                  // al conectar al GameServer
};

// Un C1/C2 (con XOR32 si lo lleva) como C3/C4. El serial se usa solo si sale.
Expected<Packet, Crypto::CryptoError> EncryptPacket(const Packet& plain, const Crypto::SimpleModulusKey& key,
                                                    PacketSerials& serials, std::byte padding = {});
struct DecryptedPacket
{
    Packet packet; // el C1/C2 rearmado, con su tamaño real
    SerialCheck serial;
};
Expected<DecryptedPacket, Crypto::CryptoError> DecryptPacket(const Packet& encrypted, const Crypto::SimpleModulusKey& key,
                                                             PacketSerials& serials);
} // namespace Client::Network
```

`CryptoError` tiene `InvalidKeyFile`, `WrongPacketType` (un C3/C4 a `EncryptPacket`, o un C1/C2 a
`DecryptPacket`), `ChecksumMismatch`, `InvalidBlock`, `NotWholeBlocks` y `TooShort`.

- [ ] **Paso 1 (PR A):** los tests de los bloques:
  - con dos pares de claves sintéticas escritos como literales en el test (calculados con
    `pow(d, -1, m)` de Python, un módulo de 17 bits y uno de 18), cifrar y descifrar de 1 a 64
    bytes al azar (semilla fija) devuelve lo mismo;
  - con la clave identidad de `mu-client` (módulo 65537, clave 1 y XOR 0 en los cuatro carriles),
    los bloques de sus fixtures;
  - un bloque con un solo bit en 1, contra lo que da `c3c4_reference.py`: fija el orden de los
    bits;
  - un byte cambiado hace fallar el checksum;
  - un carril cifrado de 18 bits en 1, mayor que su módulo, descifra como `decrypt_block`, con el
    producto que da la vuelta.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** el bloque `01 00 00 00 00 00 00 00` con la clave
  identidad, a mano: los cuatro carriles antes y después de la segunda pasada, y dónde caen sus
  bits en los 11 bytes.
- [ ] **Paso 3 (PR A):** `EncryptBlock` y `DecryptBlock`.
- [ ] **Paso 4 (PR A):** los archivos de clave. Tests con uno sintético de 54 bytes armado en el
  test (magic, tamaño y tres bloques con el XOR de la tabla): carga; otro magic, otro tamaño y uno
  corto son `InvalidKeyFile`. El golden carga `Enc1.dat` y `Dec2.dat` de la carpeta golden, y con
  el cable, `Dec1.dat` y `Enc2.dat`, y mide que cada par es inverso. Después `LoadEncryptionKey` y
  `LoadDecryptionKey`.
- [ ] **Paso 5 (PR B):** los tests de los paquetes y los seriales:
  - con la clave identidad, `FIX_C3_SERIAL0`, `FIX_C3_RANDA5` (relleno `0xA5`), `FIX_C3_SERIALFF`
    (el serial 255, que vuelve a 0) y `FIX_C4_SERIAL2` (el C2 de 300 bytes como C4 de 421, con el
    serial 2);
  - el borde entre C3 y C4: 23 bloques salen como C3 de 255 bytes, y 24, como C4;
  - Q-C10: el C3 de `[05] F1 02 01` se rearma como `C1 05 F1 02 01` (en el original, `C1 04`);
  - Q-C04: con el serial de recepción en 5, los seriales 5, 9 y 10 dan `InOrder`, `Resynced` e
    `InOrder` (en el original, dos "Decrypt error" y el desfase queda: caso 5 de
    `red_reference.py`); y 255 seguido de 0 es `InOrder`;
  - un checksum malo es `ChecksumMismatch` y el serial avanza (caso 6); también
    `NotWholeBlocks`, `InvalidBlock` y `TooShort`;
  - un C3 o un C4 que se pasa a `EncryptPacket` es `WrongPacketType` y no gasta serial.
- [ ] **Paso 6 (PR B):** `PacketSerials`, `EncryptPacket` y `DecryptPacket`.
- [ ] **Paso 7 (PR B):** el golden con el cable: los 183 C3 del cliente se descifran con
  `Dec1.dat` (checksum bien, serial desde 0 y +1 en cada conexión) y, bloque por bloque, se vuelven
  a cifrar con `Enc1.dat` del cliente en los mismos bytes; los 25 C3/C4 del servidor (con el `0xAA`
  deshecho a mano hasta la CL2.4) se descifran con `Dec2.dat` con el checksum bien y el serial
  desde 0, sin un `Resynced`, en las 7 conexiones que los traen. Sin imprimir un payload.
- [ ] **Paso 8:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "SimpleModulus|PacketSerials|EncryptedPacket"
  ```

## Sub-ola CL2.4: `Obfuscation` (identidad y FoxAnti)

**Concepto:** una interfaz con dos implementaciones, elegida por configuración, y una capa que
traduce entre dos formatos sin que el resto se entere.

**Contrato** ([03 § La capa de FoxAnti](../../../Client/docs/research/03-red-y-cifrado.md#la-capa-de-foxanti)
y `analysis/foxanti/minimal/src/packet.c`):

| Pieza | Original | MuRet |
| --- | --- | --- |
| máscara (Q-C18) | `decode_inbound` decide el `0xAA` por el primer paquete de cada `recv` y lo aplica a toda la lectura; no lo llevan C1 `00`, C1 `F4` y C2 `F4` | por paquete, con su primer byte en el cable: `6B`, `68`, `69` y `6E` (C1 a C4 con el `0xAA`) lo llevan; `C1` y `C2`, no. El lector desenmascara cada paquete antes de cortarlo |
| la lista (`parse_f4_02`) | `C2 F4 02` con cantidad de 1 byte y entradas de 4 pasa a `C2 F4 06` con cantidad de 2 bytes, y a cada código le suma un delta según `(código + 1) / 11` (`0x190`, `0x172`, `0x154`, `0x136`, `0x118`, `0xFA`, `0xDC`, `0xBE`, `0xA0`); desde el grupo 9 la entrada queda en 0; tamaño + 1. No mira la cantidad contra el paquete (Q-C09), y lo que viene detrás de la lista en la misma lectura queda en 0 (Q-C18) | igual en el paquete; una cantidad que no entra es `ObfuscationError::InvalidPacket`, y los paquetes de al lado no se tocan |
| `reset_queue` | a un C1/C3 `F3 11` le mete un `00` en `pkt[5]` y suma 1 al tamaño; con menos de 5 bytes copia de más (Q-C09: un `C1 04 F3 11` tumba al cliente) | igual, y menos de 5 bytes es `InvalidPacket`. Con el tamaño real que deja la CL2.3 (Q-C10), un C3 `F3 11` conserva su último byte, que el original perdería (*análisis*: no hay ninguno en las capturas) |
| `send_forward` | sobre lo que va a `0x685ac0` (`[serial][op]...` y el byte de relleno): deshace el XOR32, remapea el opcode (`0x10→0x1D`, `0x19→0xA5`, `0x1E→0xA6`) y lo vuelve a aplicar | igual, en `BeforeEncrypt` |
| `encode_outbound` | sobre todo C1 que sale: a `C1 06 F4 03` le resta al código la escalera de 8 rangos, que no tiene el `0x154` (Q-C17), sin tocar el XOR32; a los demás les deshace el XOR32, remapea `D3→A3` y `D7→A4` y lo vuelve a aplicar. C2, C3 y C4 pasan igual | igual, en `BeforeSend` |
| el dispatcher (Q-C13) | sin FoxAnti nadie lo llama | el dispatcher es del cliente (CL4); `IdentityObfuscation` no toca nada |

El orden es el del original. Al recibir: la máscara y el corte, `AfterCut`, el descifrado si es
C3/C4, y `BeforeDispatch`. Al mandar: `Protocol` arma el paquete (con XOR32 si lo lleva); si va
cifrado, `EncryptPacket` llama a `BeforeEncrypt` antes de cifrar; y todo lo que sale pasa por
`BeforeSend`.

**Archivos:** crear `Client/src/Network/Obfuscation/Obfuscation.h`, `ObfuscationError.h`,
`IdentityObfuscation.h/.cpp` y `FoxAntiObfuscation.h/.cpp`;
`Client/tests/network/IdentityObfuscationTest.cpp`, `FoxAntiObfuscationTest.cpp` y
`GoldenFoxAntiObfuscationTest.cpp`. Modificar `PacketReader` (recibe la `Obfuscation` para la
máscara), `EncryptPacket` (la recibe para `BeforeEncrypt`), `Client/CMakeLists.txt`, 07 (Q-C09,
Q-C17 y Q-C18 con sus tests), PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Network
{
class Obfuscation
{
public:
    virtual ~Obfuscation() = default;
    // S->C: el XOR del paquete que empieza con este byte del cable; 0 si no lleva.
    [[nodiscard]] virtual std::byte InboundMask(std::byte firstWireByte) const = 0;
    // S->C, recién cortado y desenmascarado.
    [[nodiscard]] virtual Expected<Packet, ObfuscationError> AfterCut(Packet packet) const = 0;
    // S->C, ya descifrado, antes del dispatcher.
    [[nodiscard]] virtual Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const = 0;
    // C->S: lo que se va a cifrar, con el serial en el byte 0 y el byte de relleno al final.
    virtual void BeforeEncrypt(std::span<std::byte> sealed) const = 0;
    // C->S: un paquete que sale al cable.
    [[nodiscard]] virtual Packet BeforeSend(Packet packet) const = 0;
};
} // namespace Client::Network
```

- [ ] **Paso 1:** los tests:
  - `IdentityObfuscation` deja todo igual;
  - los del cable del 03: la lista `C2 00 0A F4 02 01 00 00 00 CC` pasa a
    `C2 00 0B F4 06 00 01 90 01 00 00`, el `C1 06 F4 03 90 01` sale como `C1 06 F4 03 00 00`, y
    `C1 08 F3 11 FE 04 16 68` pasa a `C1 09 F3 11 FE 00 04 16 68`;
  - la máscara: los tres exentos van en claro, un C3 y un C4 con el `0xAA` se cortan bien, y un
    C1 `F4` exento seguido en el mismo `Feed` de un C1 enmascarado da los dos bien (Q-C18);
  - los cinco remapeos, contra lo que da la FoxAnti mínima (compilada en el host, como en el 04)
    con los mismos bytes, y en `BeforeEncrypt` también el byte de relleno: uno del `0x1E` con
    `0x29` como primer byte, como los tres `A6` de la sesión real;
  - Q-C17: los códigos 0 a 20 vuelven iguales, el 21 sale como 51 y el 98 queda en 0;
  - Q-C09: `C1 04 F3 11` y una lista con una cantidad de más son `InvalidPacket`; los paquetes
    bien formados dan lo mismo que la FoxAnti mínima (los casos de `test_packet.c` de
    `mu-client`: `test_decode_inbound`, `test_reset_queue`, `test_recv_forward`,
    `test_send_forward`, `test_send_chain_remap` y `test_encode_outbound_ladder`).
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** la ida y vuelta del código 21, a mano con las dos
  tablas, antes de correr el test de Q-C17.
- [ ] **Paso 3:** `IdentityObfuscation`, `FoxAntiObfuscation`, y la `Obfuscation` en
  `PacketReader` y en `EncryptPacket`.
- [ ] **Paso 4:** el golden con el cable: con `FoxAntiObfuscation`, el S->C de las 72 conexiones se
  corta en 3.033 paquetes sin bytes de más (144 del ConnectServer en claro; 2.889 del GameServer,
  todos con el `0xAA`), y las 72 listas `F4 02` pasan a `F4 06` con el código 400. El golden de la
  CL2.3 deja de deshacer el `0xAA` a mano.
- [ ] **Paso 5:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "Obfuscation|Packet"
  ```

## Sub-ola CL2.5: los paquetes del login

**Concepto:** structs del protocolo leídos y escritos campo por campo, sin `reinterpret_cast` ni
`#pragma pack` (la pregunta 3 del plan de la ola 1 del servidor), y un solo lugar para las
constantes del build.

**Contrato** ([04 § El flujo de conexión](../../../Client/docs/research/04-protocolo.md#el-flujo-de-conexión)):

| Paquete | Sentido | Bytes | De dónde |
| --- | --- | --- | --- |
| `F4 02`, pedir la lista | C->S | `C1 04 F4 02`, sin XOR32 | `0x4c6070` |
| `F4 06`, la lista | S->C | `C2 <tamaño:2> F4 06 <cantidad:2> <entradas de 4 bytes>`; la cantidad en big-endian (`pkt[5] * 0x100 + pkt[6]`); cada entrada, el código (u16 little-endian), la carga y un byte que no se lee | lo que deja FoxAnti y lee `0x4c1860` (*estático*) |
| `F4 03`, elegir | C->S | `C1 06 F4 03 <código:2 LE>`, sin XOR32 | `main.exe` manda su código (400) y FoxAnti lo pasa a 0 |
| `F4 03`, la respuesta | S->C | `C1 16 F4 03 <IP:16, con \0> <puerto:2 LE>` | en el cable, `"172.16.198.1"` y `5D DA` (55901) |
| `F1 00`, saludo | S->C | `C1 0C F1 00 <resultado> <índice:2> <versión:5>`; el índice en big-endian (`pkt[5] * 0x100 + pkt[6]`) | `0x4c1e20` (*estático*); compara la versión byte a byte con la suya |
| `F1 01`, login | C->S | `C1 31 F1 01 <cuenta:10> <clave:10> <tick:4 LE> <versión:5> <serial:16>`, con XOR32, como C3 de 68 bytes | `0x641bf0`; la cuenta y la clave con XOR `FC CF AB` desde el principio de cada campo (`0x4c1690`) |
| `F1 01`, respuesta | S->C | `C1 05 F1 01 <resultado>`: 1 es aceptado | |
| `F1 03`, aviso | C->S | `C1 06 F1 03 06 00`, con XOR32, como C3 | el checksum malo de la CL2.3 |
| `0E`, keepalive | C->S | `C1 0C 0E 00 <tick:4 LE> <x:2 LE> <y:2 LE>`, con XOR32, como C3 de 24 bytes | `0x4bee60` y el `WM_TIMER` de `0x4af4e0`; a x e y les resta 20 si el personaje tiene el bit `0x01`, si no 25 con el `0x08`, y dan la vuelta en 16 bits |

Las constantes del build van en `Protocol/BuildConstants.h`: la versión `10209` (en `0x6c8284`
está guardada con cada byte más su índice más 1, `32 32 35 34 3E`) y el serial
`MULORDSVERSAO102`. La clave de la cuenta y la clave es la del XOR3 de las tablas: el original
tiene dos copias (`0x6c8c74`, que usa `0x5496c0`, y `0x6c82e4`, que usa `0x4c1690`) y MuRet una:
`Xor3` pasa de `Data::Tables` a `Client::Core`. El `A9` del principio (Q-C16) y el orden del flujo
son de la CL4.

**Archivos:** crear `Client/src/Protocol/BuildConstants.h`, `ProtocolError.h`, `Outgoing.h` y, en
`Client/src/Protocol/Connection/`, `ServerList.h/.cpp` (`F4 02`, `F4 06` y `F4 03`), `Hello.h/.cpp`
(`F1 00`), `Login.h/.cpp` (`F1 01`, su respuesta y `F1 03`) y `Keepalive.h/.cpp` (`0E`); sus tests
en `Client/tests/protocol/` y `GoldenLoginTest.cpp`. Mover `Xor3` a `Client/src/Core/`. Modificar
`Client/CMakeLists.txt`, `Client/README.md`, 04 (la cantidad de `F4 06` y el índice de `F1 00`),
PROGRESS y este plan.

**Interfaces:**

```cpp
namespace Client::Protocol
{
enum class Delivery { Clear, Encrypted }; // C1/C2 al cable, o C3/C4 con EncryptPacket
struct Outgoing
{
    Network::Packet packet; // con el XOR32 ya aplicado si lo lleva
    Delivery delivery;
};

Outgoing WriteServerListRequest();
struct ServerEntry { std::uint16_t code; std::uint8_t load; };
Expected<std::vector<ServerEntry>, ProtocolError> ReadServerList(const Network::Packet& packet); // F4 06
Outgoing WriteServerSelect(std::uint16_t code);
struct ServerAddress { std::string host; std::uint16_t port; };
Expected<ServerAddress, ProtocolError> ReadServerAddress(const Network::Packet& packet);

struct ServerHello { std::uint8_t result; std::uint16_t playerIndex; std::array<char, 5> version; };
Expected<ServerHello, ProtocolError> ReadServerHello(const Network::Packet& packet);

// No tiene operator<<: la cuenta y la clave no se imprimen nunca.
struct LoginRequest { std::string_view account; std::string_view password; std::uint32_t tick; };
Expected<Outgoing, ProtocolError> WriteLoginRequest(const LoginRequest& request); // FieldTooLong con más de 10
Expected<std::uint8_t, ProtocolError> ReadLoginResult(const Network::Packet& packet);
Outgoing WriteDecryptErrorReport();

// Los bits del personaje que el research no nombró todavía.
inline constexpr std::uint8_t KeepaliveAdjust20 = 0x01;
inline constexpr std::uint8_t KeepaliveAdjust25 = 0x08;
std::uint16_t KeepaliveCoordinate(std::uint16_t value, std::uint8_t characterFlags);
struct Keepalive { std::uint32_t tick; std::uint16_t x; std::uint16_t y; };
Outgoing WriteKeepalive(const Keepalive& keepalive);
} // namespace Client::Protocol
```

`ProtocolError` tiene `WrongPacket` (otro tipo u otro opcode), `TooShort` y `FieldTooLong`.

- [ ] **Paso 1:** los tests, uno por paquete, con los bytes del 04 y los fixtures de `mu-client`:
  - `FIX_LOGIN_PLAIN` y `FIX_LOGIN_FULL_PLAIN` (cuenta y clave de 10 bytes, sin `\0`), y sus C3
    con la clave identidad; una cuenta de 11 bytes es `FieldTooLong`, y una vacía da la clave sola;
  - los cinco keepalive (`RAW`, `ADJUST20`, `ADJUST25_WRAP`, `ADJUST20_PRIORITY` y `HIGH_Y`, este
    con el serial 255 y el relleno `0xA5`);
  - el aviso: `FIX_F1_PLAIN` y `FIX_C3_SERIAL0`;
  - `ReadServerList` con la lista del cable después de FoxAnti: un servidor, código 400 y carga 0;
    una cantidad que no entra es `TooShort`;
  - `WriteServerSelect(400)` es `C1 06 F4 03 90 01`, sin XOR32;
  - `ReadServerAddress` con la respuesta del cable: el host hasta el primer `\0` de los 16 bytes
    (los 16 si no hay, la regla de Q-C07) y el puerto 55901;
  - `ReadServerHello` con `C1 0C F1 00 01 00 05 31 30 32 30 39`: resultado 1, índice 5 y la
    versión `10209`;
  - un paquete de otro opcode es `WrongPacket`, y uno corto, `TooShort`.
  Los tests usan solo las credenciales sintéticas de `mu-client`.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** decodificar a mano `C1 0C F1 00 01 00 05 31 30 32 30 39`
  (detective de bytes) antes de escribir `ReadServerHello`.
- [ ] **Paso 3:** mover `Xor3` y escribir los paquetes.
- [ ] **Paso 4:** el golden con el cable: los 7 login descifran a 49 bytes que, sin el XOR32,
  empiezan con `C1 31 F1 01` y traen la versión `09908` en `+28` (el `muproxy.log` es del build
  histórico; el actual manda `10209`); de la cuenta, la clave y el serial no se mira nada. Los 156
  keepalive descifran a 12 bytes que empiezan con `C1 0C 0E 00`.
- [ ] **Paso 5:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "Protocol|Login|Keepalive|ServerList|Hello"
  ```

---

## Al cerrar la CL2

- [ ] Completar el cierre de la guía de la CL2.0.
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Escribir el plan de la CL3 (plataforma y bucle de juego) en su propio PR, con lo aprendido.
