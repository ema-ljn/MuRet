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
significa cada opcode, y `Protocol` no sabe de C3/C4 ni de FoxAnti: cuando un paquete lleva el
XOR32, lo aplica con la función de `Network`. Como toda N.0, la CL2.0 vive en `labs/`.

**Stack:** C++20 (`std::byte`, `std::span`, `std::array`, `std::optional`, aritmética sin signo
de 32 bits, `std::mt19937` con semilla fija), GoogleTest. No entra ninguna biblioteca
(`CODING_RULES.md` §10): el corte, el XOR32, el `SimpleModulus` y FoxAnti son del MU, y
empaquetar bits son unas pocas líneas. Los tests al azar usan la salida del `std::mt19937`
directamente: el estándar fija esa secuencia, pero no la de `std::uniform_int_distribution`, que
cambia entre MSVC y libstdc++.

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
  mínima (`analysis/foxanti/minimal/src/packet.c`). Son oráculos para comparar: su código no se
  copia, y sus vectores sí, con su origen. Dos fixtures de `mu-client` cifran mal un C2 y se
  arman de nuevo (CL2.2).

**Terminada cuando** (roadmap):
- pasan en CI los vectores del cable y los fixtures de `mu-client` para el corte, el XOR32, C3/C4,
  FoxAnti, el login y el keepalive;
- con el cable del laboratorio (pregunta 4), sus 72 conexiones se cortan sin bytes de más, los 183
  C3 del cliente se descifran y, bloque por bloque con `EncryptBlock`, se vuelven a cifrar con los
  mismos bytes, y los 25 C3/C4 del servidor se descifran con su checksum bien y sus seriales en
  orden. Bloque por bloque porque en 73 de los 176 C3 con relleno el original dejó basura de la
  pila después del primer byte, y `EncryptPacket` pone ceros (pregunta 5).

## Preguntas para decidir al empezar

1. **Quién escribe la CL2.** En la CL1 pediste seguir con el cliente y Claude la escribió como
   ejemplo trabajado. La recomendación era igual, con la CL2.0 en `labs/` para vos y un paso
   `TODO(human)` chico por sub-ola (marcados abajo). **Decidido (Emanuel, 2026-11-02): Claude
   escribe todo.** Primero elegiste "Claude, como en la CL1", y el mismo día pediste que Claude
   escriba también la CL2.0 en `labs/` y los pasos `TODO(human)`, hasta que digas que
   participás. Los pasos marcados "(Emanuel)" los hace Claude por ahora, y en
   [PROGRESS](../../learning/PROGRESS.md) quedan como resueltos por Claude; cuando entres, los
   que sigan abiertos vuelven a ser tuyos. Las preguntas 2 a 5 siguen con su recomendación
   mientras no digas otra cosa: todas se pueden cambiar después.
2. **Código compartido con los servidores** (pregunta 2 del roadmap). El corte C1/C2 lo van a
   necesitar el DataServer (el `FrameParser` de su ola 1) y el ConnectServer (ola 10). C3/C4, el
   XOR32 y FoxAnti, solo el cliente: el GameServer queda fuera del roadmap del servidor.
   *Recomendación:* en esta ola no nace `Common/`. `Network` depende solo del estándar y de
   `Client::Core` (`Expected`, `ByteReader`), así que pasar el corte a un `Common/` en la raíz es
   mover archivos. Se decide en la 1.2 del servidor, el segundo que lo usa: reusarlo, o escribir
   el `FrameParser` como el ejercicio que es en su plan.
3. **Hasta qué tamaño se lee un paquete** (Q-C40 y Q-C05). El original recibe en un buffer de
   `0x2000` bytes: un paquete más grande nunca entra entero y el buffer se llena. Desde ahí cada
   lectura loguea "Receive Packet Buffer Overflow" y vuelve con 1 sin llamar a `recv`
   (`0x4e88a0`); el WndProc ignora ese 1 (`0x4b3207`), así que la conexión no se cierra, pero no
   se lee más. Además descifra en ~2.046 bytes de la pila, y un C4 de más de ~2,8 KB la desborda
   (*estático*, sin probar). *Recomendación:*
   `MaxPacketSize = 0x2000` para los cuatro tipos, como error apenas llega la cabecera, y el
   descifrado escribe en un buffer del tamaño de lo que entra. En el cable del laboratorio el más
   grande mide 254 bytes, un C2 del GameServer.
4. **Dónde está el cable para los tests golden.** El `muproxy.log` del laboratorio (72 conexiones,
   en `mu-online-reverse-lab/analysis/mainexe/evidence/`) y las claves del servidor (`Dec1.dat` y
   `Enc2.dat`, en `gameserver/evidence/keyfiles/` del repo de research) no entran en el Git de
   MuRet: el cable lleva el login. *Recomendación:* una variable de entorno `MURET_CLIENT_WIRE_DIR`
   (solo de entorno, como `MURET_CLIENT_MEMORY_DIR`) con una carpeta que tenga esos tres archivos,
   con la regla de la golden (sin variable se saltea, incompleta falla). `Enc1.dat` y `Dec2.dat` del
   cliente ya están en la carpeta golden. Los tests con el cable no imprimen un payload, tampoco al
   fallar: dicen conexión, índice, cabecera, opcode, serial y offset, nunca el valor de un byte.
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

- Una lectura que termina en el primer byte de un paquete, o en los dos primeros de un C2/C4,
  detrás de un paquete entero (Q-C03, CL2.1).
- En una misma lectura, un paquete exento del `0xAA` al lado de uno enmascarado, y uno enmascarado
  partido entre dos lecturas (Q-C18, CL2.4).
- Un largo o una cantidad que no entra en el paquete: la lista `F4 02` con su cantidad de más, un
  `C1 04 F3 11` (Q-C09, CL2.4), un C3 que no trae bloques enteros (CL2.3).
- Un producto que da la vuelta en 32 bits, con un carril cifrado mayor que su módulo, y un
  `std::uint16_t * std::uint16_t`, que se promueve a `int` y puede desbordar: las claves del
  servidor llegan a 16 bits (CL2.3).
- Los bits 16 y 17 de un carril escritos al revés: la clave identidad nunca los prende y un
  round-trip no lo nota, así que lo fija un vector (CL2.3).
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
- bits MSB primero: campos de bits empaquetados en un arreglo de bytes, y por qué un carril del
  `SimpleModulus` no es un campo de 18 bits MSB primero: sus 16 bits de abajo van byte por byte,
  el de abajo primero, y después el bit 17 y el 16.

Del C++: operadores de bits sobre `std::byte` y `std::to_integer`; la aritmética sin signo que da
la vuelta (definida) contra la con signo (comportamiento indefinido), y la promoción de
`std::uint16_t` a `int` antes de multiplicar; `enum class` para los estados; `std::mt19937` con
semilla fija para un test de propiedad.

**Archivos:**
- Crear: `docs/learning/ola-cl2.0-flujos-y-bloques.md` (la guía; Claude)
- Crear: `labs/cl2.0-flujos/FrameCutter.h/.cpp`, `BitStream.h/.cpp` y `ModularInverse.h/.cpp`,
  sus tests y `jefe/OldCutTest.cpp` (Emanuel)
- Modificar: `labs/CMakeLists.txt`, `labs/README.md`

- [x] **Paso 1 (Claude):** la guía, justo antes de empezar
  ([ola-cl2.0-flujos-y-bloques.md](../../learning/ola-cl2.0-flujos-y-bloques.md), 2026-11-02).
- [x] **Paso 2 (Claude, andamiaje):** GoogleTest en `labs/`, el paso 1 de la CL1.0, si todavía no
  se hizo. Llegó con esta sub-ola, en `MuRetLabsCl20` y `LabCl20Tests`.
- [x] **Paso 3 (Emanuel, `TODO(human)`; lo resolvió Claude):** `Labs::FrameCutter`, alimentado de a
  un byte, con los estados explícitos en un `enum class` (tipo, tamaño, cuerpo). Primero los tests:
  los casos de `red_reference.py --selftest` que no son del cifrado (corte, resto parcial, cabecera
  incompleta, cabecera inválida y tamaño 0), y uno de propiedad: paquetes válidos al azar, uno
  detrás del otro, salen iguales y en orden. Como el corte recibe de a un byte, partir el flujo no
  cambia nada; esa propiedad es del `PacketReader` de la CL2.1, que recibe lecturas enteras.
- [x] **Paso 4 (Emanuel; lo resolvió Claude):** `Labs::BitWriter` y `Labs::BitReader`, MSB primero:
  `Write(value, bits)` y `Read(bits)`. Tests: el primer bit escrito es el `0x80` del byte 0;
  cuatro campos de 18 bits más uno de 16 ocupan justo 11 bytes y se leen iguales; y un carril
  como lo escribe el original (sus dos bytes de abajo, de a 8 bits, y después el bit 17 y el 16):
  `0x10000` da `00 00 40` y `0x20000`, `00 00 80`.
- [x] **Paso 5 (Emanuel; lo resolvió Claude):** `Labs::ModularInverse(a, m)`, con Euclides
  extendido, que devuelve `std::nullopt` si no hay inversa. Tests: uno a mano (3 módulo 7 es 5), uno
  sin inversa, y que para un módulo de 18 bits el producto de los dos módulo m da 1.
- [x] **Paso 6 (Emanuel, jefe; lo resolvió Claude):** Q-C03. El corte del original sobre un buffer
  que se reusa: con los dos primeros bytes de un C2 al final de una lectura, el tamaño se arma con
  un byte viejo; si da 0, lo que queda no se corre al principio y la lectura siguiente se pega
  detrás de bytes ya usados. El test reproduce el caso 1b de `red_reference.py`: `C1 05 10 01 02` y
  `C2 00 06 20 03 04`, partidos después de `C2 00`, dan en el original un segundo paquete
  `C1 05 06 20 03`. Después, que el `FrameCutter` espera los bytes que faltan.
- [x] **Paso 7 (Emanuel; lo resolvió Claude):** comparar, y anotarlo en `labs/README.md`: el
  `FrameCutter` con el `PacketReader` de la CL2.1 (cuál es más fácil de probar y por qué), y el
  `BitWriter` con `std::bitset` y `<bit>` (`std::bit_width`, `std::rotl`): qué resuelven y qué no.
  Quedó en el README de la carpeta, [`labs/cl2.0-flujos/README.md`](../../../labs/cl2.0-flujos/README.md),
  con las respuestas de las otras actividades; `labs/README.md` lo enlaza.
- [x] **Paso 8 (carril R; lo resolvió Claude):** en Ghidra, el corte de `0x4e893d`: las lecturas de
  `buf[off + 1]` y `buf[off + 2]` sin mirar cuántos bytes quedan, y el `cmp eax,0x2000` de
  `0x4e88a0`.
- [x] **Paso 9:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "FrameCutter|BitWriter|BitReader|ModularInverse|OldCut"
  ```

## Sub-ola CL2.1: el corte C1/C2/C3/C4

**Concepto:** un lector de paquetes: entra un flujo de bytes, salen paquetes enteros, y nunca se
lee un byte que no llegó.

**Contrato** ([03 § Cadena de recepción](../../../Client/docs/research/03-red-y-cifrado.md#cadena-de-recepción)
y `004e893d-corte-paquetes.md`):

| Qué | Original | MuRet |
| --- | --- | --- |
| tamaño | `C1` y `C3` lo llevan en `pkt[1]`; `C2` y `C4`, en `pkt[1..2]` big-endian. Es el del paquete entero | igual |
| cabecera incompleta (Q-C03) | detrás de un paquete entero de la misma lectura, con el tipo solo de un C1/C3 o uno o dos bytes de un C2/C4, arma el tamaño con bytes viejos (lee `buf[off + 1]` y `buf[off + 2]` sin mirar cuántos quedan); si da 0, el flujo queda corrido. Al principio de una lectura espera bien: con menos de 3 bytes no corta | espera los bytes que faltan |
| tipo inválido | descarta todo lo acumulado, también los paquetes buenos que vengan detrás, y sigue | `PacketError::InvalidType`, y el error queda: la CL4 cierra la conexión |
| tamaño menor que la cabecera y el opcode | con 0 al principio de la lectura no avanza nunca, y el buffer se llena hasta que no se lee más (pregunta 3); con 1 o 2 en un C1/C3, o de 1 a 3 en un C2/C4, saca un paquete sin opcode | `PacketError::SizeTooSmall` (C1 y C3 miden al menos 3; C2 y C4, 4) |
| tamaño de más (Q-C40) | un paquete de más de `0x2000` bytes no entra en el buffer, que se llena hasta que no se lee más | `PacketError::TooLarge` apenas llega la cabecera (pregunta 3) |
| copia | cada paquete va a la cola en un buffer de `0x2004` bytes con un `0xFD` detrás | cada `Packet` es dueño de sus bytes, sin centinela |

Lo que hace MuRet en las filas de Q-C03, del tipo inválido, del tamaño chico y del de más corrige
bugs del original: ningún servidor bueno manda esos paquetes, y lo que hace el original con ellos
no es contrato. Q-C03 ya estaba en 07; el tipo inválido, el tamaño chico y el de más entran con
esta sub-ola (Q-C38, Q-C39 y Q-C40).

**Archivos:**
- **PR A:** `ByteReader` pasa de `Client::Data` a `Client::Core`
  (`Client/src/Core/ByteReader.h/.cpp`, y su test a `Client/tests/core/`), con `ReadU16Be` para el
  tamaño de un C2, el primer entero big-endian del cliente. Lo van a usar `Data`, `Network` y
  `Protocol`; en `Data` cambia el include.
- **PR B:** crear `Client/src/Network/Packet.h/.cpp`, `PacketError.h` y `PacketReader.h/.cpp`;
  `Client/tests/network/PacketTest.cpp`, `PacketReaderTest.cpp` y `GoldenPacketReaderTest.cpp`;
  `Client/tests/support/WireCapture.h/.cpp`, que lee el `muproxy.log` (una línea por segmento TCP:
  hora, puerto, sentido, `len=` y los bytes en hex; `ACC` abre una conexión), con su
  `WireCaptureTest.cpp` sobre un log sintético de pocas líneas. Modificar
  `Client/tests/support/GoldenDir.h/.cpp` (`MURET_CLIENT_WIRE_DIR`, solo por el entorno, como
  `MURET_CLIENT_MEMORY_DIR`: `gtest_discover_tests` pierde la segunda variable de `ENVIRONMENT`),
  `SameBytes.h` (`SameBytesWithoutValues`, que dice dónde difieren y no qué valen, con su
  `SameBytesTest.cpp`: el mensaje de una diferencia no trae ningún valor), `Client/CMakeLists.txt`,
  `Client/README.md`, 07 (Q-C03 también con el primer byte de un C1/C3, y el tipo inválido, el
  tamaño chico y el de más, nuevos, con sus tests), PROGRESS y este plan.

Hecho así: la carpeta del cable la pide un fixture, `WireDataTest` en `support/GoldenDir.h`, que
salta el test sin `MURET_CLIENT_WIRE_DIR` y falla si la carpeta no tiene el archivo. El tipo
inválido, el tamaño chico y el de más son Q-C38, Q-C39 y Q-C40 en 07, y el 03 los nombra en el
corte. El golden también corta de a un byte, y en lecturas que terminan 1 y 2 bytes adentro del
paquete siguiente (lo que en el original dispara Q-C03), lo que mandó el cliente y lo que contestó
el ConnectServer, que trae los C2: da lo mismo. Con las revisiones: la regla del tamaño quedó en
`CheckDeclaredSize`, que usan `FromBytes` y el lector; un lector movido queda como nuevo; `Feed`
borra lo que ya salió solo cuando es al menos tanto como lo pendiente, así no corre todo en cada
lectura si no se sacan los paquetes; y `support/Bytes.h` y `support/NetworkPrinters.h` (que
imprime un `PacketError` por su nombre) quedan para los tests de `Network`.

**Interfaces:**

```cpp
namespace Client::Network
{
enum class PacketType : std::uint8_t { C1 = 0xC1, C2 = 0xC2, C3 = 0xC3, C4 = 0xC4 };

// El tipo y el tamaño: 2 bytes en C1 y C3, 3 en C2 y C4.
std::size_t HeaderSize(PacketType type);

// El paquete más grande que se lee o se escribe (pregunta 3).
inline constexpr std::size_t MaxPacketSize = 0x2000;

// SizeTooSmall, TooLarge o std::nullopt: la regla del tamaño de FromBytes y del lector.
std::optional<PacketError> CheckDeclaredSize(PacketType type, std::size_t declared);

class Packet
{
public:
    // Valida el tipo, el tamaño mínimo y el máximo, y que el tamaño que declara sea el que mide.
    static Core::Expected<Packet, PacketError> FromBytes(std::vector<std::byte> bytes);
    PacketType Type() const;
    std::span<const std::byte> Bytes() const; // con la cabecera
    // Lo que sigue a la cabecera (desde el opcode en C1/C2, desde el cifrado en C3/C4): se puede
    // cambiar en el lugar (el XOR32, los remapeos de FoxAnti), pero no el tipo ni el tamaño.
    std::span<std::byte> MutableBody();
    // Para lo que cambia el tamaño (FoxAnti): saca los bytes, que vuelven con FromBytes.
    std::vector<std::byte> TakeBytes() &&;
    // pkt[2] en un C1 y pkt[3] en un C2; std::nullopt en C3 y C4, que lo llevan cifrado.
    std::optional<std::uint8_t> Opcode() const;
};

class PacketReader
{
public:
    void Feed(std::span<const std::byte> bytes);
    // El paquete que sigue, std::nullopt si falta que llegue, o el error, que se repite desde ahí.
    [[nodiscard]] Core::Expected<std::optional<Packet>, PacketError> Next();
};
} // namespace Client::Network
```

`PacketError` tiene `InvalidType` (el primer byte no es `C1` a `C4`), `SizeTooSmall` (menos que la
cabecera y el opcode: 3 bytes en C1 y C3, 4 en C2 y C4; un vector vacío también), `SizeMismatch`
(`FromBytes` con un tamaño declarado distinto del que mide) y `TooLarge` (más de `MaxPacketSize`; en
`PacketWriter`, más de lo que entra en el tipo).

En `Next()`, `return std::nullopt;` y `return packet;` no compilan con `Core::Expected`: cada uno
necesita dos conversiones definidas por el usuario (a `std::optional` y de ahí a `Expected`), y
C++ permite una sola. Se escribe `return std::optional<Packet>{};` y
`return std::optional<Packet>{std::move(packet)};`.

- [x] **Paso 1 (PR A):** mover `ByteReader` y sumar `ReadU16Be`. Test: `01 2C` da `0x012C`.
- [x] **Paso 2:** los tests de `Packet`: `FromBytes` acepta `C1 04 20 AA` y `C2 00 05 52 AA`;
  rechaza `C1 05 20 AA` (`SizeMismatch`), un tipo `C5` (`InvalidType`), nada y `C1 02`
  (`SizeTooSmall`, sin leer un opcode que no está) y un C2 de `0x2001` (`TooLarge`), en ese
  orden (`C2 20 01` es `TooLarge` y `C1 02 20 AA`, `SizeTooSmall`); `Opcode()`
  en C1 y C2, y `std::nullopt` en C3 y C4; `MutableBody()` de un C1 empieza en el opcode y el de
  un C2 también; `TakeBytes()` devuelve los mismos bytes y deja el `Packet` vacío.
- [x] **Paso 3:** `Packet`.
- [x] **Paso 4 (Emanuel, `TODO(human)`; lo resolvió Claude):** la tabla de vectores del
  lector, antes del código:
  - `C1 04 20 AA` da un paquete, y `C2 01 2C` con 297 bytes más, uno de 300;
  - un C3 y un C4 se cortan como un C1 y un C2, sin descifrarlos;
  - el caso 1 de `red_reference.py`: `C1 05 10 01 02` y los 3 primeros bytes de
    `C2 00 06 20 03 04` en un `Feed`, y el resto en otro, dan los dos paquetes;
  - Q-C03: el mismo par partido después de `C2` y después de `C2 00` da los mismos dos paquetes
    (el original, en el segundo caso y con un 0 viejo, da `C1 05 06 20 03`, el jefe de la CL2.0);
    y `C1 05 10 01 02` seguido de un `C1` o un `C3`, con `04 20 AA` en el `Feed` siguiente, da los
    dos paquetes. Un `C1` solo al principio espera, como en el original, que no corta con menos de
    3 bytes;
  - `C1 03` y después `00` dan un paquete;
  - varios paquetes en un `Feed` (en el cable hubo 337 lecturas con 2 a 7);
  - `00 05 01 02 03` seguido de un paquete bueno es `InvalidType`, y el bueno no sale;
  - `C1 00`, `C1 01`, `C1 02`, `C3 02`, `C2 00 00`, `C2 00 01`, `C2 00 03` y `C4 00 03` son
    `SizeTooSmall`;
  - `C2 20 00` espera sus `0x2000` bytes, y `C2 20 01` es `TooLarge` con solo la cabecera;
  - después de cada uno de los tres errores, `Next()` lo repite cada vez que se pide, aunque
    llegue un paquete bueno, y `Pending()` queda en 0;
  - un lector movido, con el constructor o con la asignación, queda sin bytes y sin error, y el
    que lo recibe sigue con su paquete a medio llegar o con su error;
  - sin sacar todos los paquetes después de cada `Feed`, salen igual y en orden;
  - el mismo flujo partido al azar (con semilla fija y la salida directa del motor) da los mismos
    paquetes.
- [x] **Paso 5:** `PacketReader`.
- [x] **Paso 6:** el golden con el cable: el sentido C->S de las 72 conexiones se corta en 689
  paquetes sin bytes de más (al ConnectServer, 144 C1; al GameServer, 362 C1 y 183 C3), y el S->C
  de las 64 conexiones al ConnectServer, que va en claro, en 144 (72 C1 y 72 C2). Esos dos
  sentidos, de a un byte y en lecturas que terminan 1 y 2 bytes adentro del paquete siguiente, dan
  los mismos paquetes. El S->C del GameServer lleva el XOR `0xAA` en cada byte: va en la CL2.4.
- [x] **Paso 7:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "ByteReader|Packet"
  ```

## Sub-ola CL2.2: `PacketWriter` y XOR32

**Concepto:** escribir un paquete campo por campo, y un cifrado encadenado como etapa aparte.

**Contrato** ([03 § Envío](../../../Client/docs/research/03-red-y-cifrado.md#envío-el-motor-c1c2-y-el-xor32)
y `mu-client/docs/component-net-envio.md`):

| Qué | Original | MuRet |
| --- | --- | --- |
| empezar | C1: `buf[0] = C1`, `buf[2] = op`, tamaño 3. C2: `buf[0] = C2`, `buf[1..2]` provisorio, `buf[3] = op`, tamaño 4. En los dos el opcode va en claro | `PacketWriter::C1(op)` y `C2(op)` arman el paquete sin XOR32 |
| XOR32 | cada byte que se agrega después del opcode (desde el 3 en un C1, desde el 4 en un C2): `valor ^= key[i & 31] ^ buf[i - 1]`, con `buf[i - 1]` ya cifrado; el primero se encadena con el opcode | `Xor32Encode`, aparte |
| terminar | escribe el tamaño, en un C2 big-endian | `Finish()` |
| tope (Q-C06) | `0x800` bytes: antes de cada campo mira si entra, y si no lo saltea en silencio y sigue con el próximo; en un C1 el tamaño es un byte, y con más de 255 sale truncado si va en claro (`test_engine_cap`, que modela el tope byte por byte: con `0x800` da `C1 00`) | `PacketError::TooLarge` con más de 255 en un C1 y más de `0x800` en un C2: nunca sale un paquete truncado |
| quién lo usa | casi todo lo que manda el cliente; los `F4` al ConnectServer no: `0x4c6070` arma el pedido sin el motor, y el `F4 03` llega con su `03` en claro | cada paquete de `Protocol` aplica el XOR32 si lo lleva (CL2.5) |

El C2 es *estático*: el cliente del laboratorio no manda ninguno. Su único emisor en `c32bbd5f` es
la ventana de cartas (`0x498820`): en `0x498b2c..0x498b89` escribe `C2`, la palabra provisoria
`01 00` y el opcode `C5` sin XOR, y el XOR32 de lo que sigue (`0x498c54..0x498c83`) empieza en
el índice 4 y se encadena con el `C5`. `mu-client` lo modela distinto (`buf[3] = op ^ key[3]`,
en `src/net/send.c`, sin evidencia), y con ese modelo están armados `FIX_C2_PLAIN` y
`FIX_C4_SERIAL2`: no se copian, se arman de nuevo con el modelo del binario. El 03 decía lo mismo
que `mu-client` y se corrigió con este plan.

Un C1 de más de 255 bytes que va cifrado no sale truncado en el original: el envoltorio C3/C4
usa el tamaño de 16 bits del motor y pisa `buf[1]` con el serial, así que saldría como un C4
válido (`0x4e21b6..0x4e229f`). Ningún emisor arma uno (*análisis*), y `Finish()` lo rechaza
igual: si apareciera, se escribe como C2.

**Archivos:** crear `Client/src/Network/PacketWriter.h/.cpp` y
`Client/src/Network/Crypto/Xor32.h/.cpp`; `Client/tests/network/PacketWriterTest.cpp` y
`Xor32Test.cpp`, y los fixtures largos de `mu-client` en `Client/tests/vectors/net/` como binarios,
con un README que dice de dónde sale cada uno (`tests/net/fixtures/gen_net_send_fixtures.py` del
research) y su línea en `.gitattributes`. Los dos del C2 los arma un script chico en
`Client/tools/net/`, con el contenido del generador del research y el opcode en claro, y el README
lo dice. Modificar `Client/CMakeLists.txt`, 07 (Q-C06 con su test), PROGRESS y este plan.

Hecho así: el script quedó en `Client/tests/vectors/net/tools/make_vectors.py`, junto a sus
vectores, como el de los JPEG en `tests/vectors/jpeg/tools/`, y los tests los leen con
`support/NetVectors.h`, con `MURET_CLIENT_NET_VECTORS_DIR`. En esta sub-ola arma solo
`c2-long.bin`; el C4 (`FIX_C4_SERIAL2`) lo suma la CL2.3, que es la que lo usa. Por las
revisiones: el escritor guarda el tipo y el opcode aparte de los campos, así uno del que se
movieron los bytes sigue armando su tipo; cada `Put` tiene una versión para un temporal que
devuelve otro escritor, así una referencia a un temporal no compila; y Q-C06 dice que el tope del
original es por campo.

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
    [[nodiscard]] Core::Expected<Packet, PacketError> Finish() const;
};
} // namespace Client::Network

namespace Client::Network::Crypto
{
// La tabla de 32 bytes del motor (03 § Envío).
inline constexpr std::array<std::uint8_t, 32> Xor32Key{/* e7 6d 3a 89 ... e8 56 */};
// El núcleo, sobre bytes sueltos: desde bytes[first], cada uno con key[(i + keyShift) & 31] y el
// anterior ya cifrado. Un C1 o un C2 entero es (pkt, HeaderSize + 1, 0). FoxAnti lo usa sobre lo
// que va a cifrarse en C3/C4 ([serial][op]..., CL2.4) con (sealed, 2, 1), como su `Crypt` con k0 = 3.
void Xor32EncodeBytes(std::span<std::byte> bytes, std::size_t first, std::size_t keyShift);
void Xor32DecodeBytes(std::span<std::byte> bytes, std::size_t first, std::size_t keyShift);
// Un C1 o un C2 entero, con MutableBody(); un C3 o un C4 vuelve igual.
[[nodiscard]] Packet Xor32Encode(Packet packet);
[[nodiscard]] Packet Xor32Decode(Packet packet);
} // namespace Client::Network::Crypto
```

- [x] **Paso 1 (Emanuel, `TODO(human)`; lo resolvió Claude):** antes de leer los vectores del
  paso 2, cifrar a mano con la tabla los bytes 3 y 4 de `C1 06 A9 00 00 01`, y los bytes 4 y 5 de
  `C2 00 06 42 0B 30`. El paso 2 trae los resultados para corregirlos.
- [x] **Paso 2:** los tests:
  - los dos del cable del 04: `C1 04 F3 00` sale como `C1 04 F3 7A`, y `C1 06 A9 00 00 01`, como
    `C1 06 A9 20 9C 2F`;
  - un C2 con el modelo del binario: `C2 00 06 42 0B 30` sale como `C2 00 06 42 F5 77`, con el
    opcode en claro;
  - de `test_net_send.c`, `FIX_F1_PLAIN` (el aviso `C1 06 F1 03 06 00`, CL2.3), y el C2 largo
    armado de nuevo (opcode `0x42` y 296 bytes, el i-ésimo `i * 37 + 11`, que dan `C2 01 2C`); los
    tamaños 255, 256, 300 y `0x800` en big-endian;
  - la clave vuelve a empezar en el byte 32: el 35 usa `key[3]`;
  - `Xor32Decode(Xor32Encode(p))` es `p` en C1 y en C2, y el opcode no cambia en ninguno de los
    dos;
  - `Xor32EncodeBytes` con `(sealed, 2, 1)` sobre `[serial]` y el C1 desde su opcode da lo mismo
    que `Xor32Encode` del C1;
  - Q-C06: un C1 de 255 bytes entra y uno de 256 no; un C2 de `0x800` entra y uno de `0x801` no.
- [x] **Paso 3:** `PacketWriter`, `Xor32EncodeBytes`, `Xor32DecodeBytes`, `Xor32Encode` y
  `Xor32Decode`.
- [x] **Paso 4:** verificar:
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
| bits de un carril | de los 16 de abajo van sus dos bytes en orden little-endian, cada uno con su bit más alto primero; después el bit 17 y el 16, en ese orden (`0x685bb0` llama a `AddBits` con 2 bits desde la posición 22 del dword, contando MSB primero): `0x20000` da `00 00 80` | igual: el algoritmo exacto es `encrypt_block` y `decrypt_block` de `c3c4_reference.py` |
| paquete | el envoltorio, inlineado en cada emisor (por ejemplo `0x4e2198..0x4e2500`, el del aviso `F1 03`): copia el C1/C2 ya con XOR32, pone el byte de relleno, pisa el tamaño con el serial (`[1]` en un C1, `[2]` en un C2) y cifra desde ahí con `0x685ac0`: `size - 1` bytes útiles en un C1, `size - 2` en un C2. Si lo cifrado mide menos de `0x100` sale como C3; si no, como C4 (`cmp eax,0x100`). La ventana de cartas (`0x498820`), el único emisor de un C2, arma siempre un C4, sin el `cmp`. *Estático* | `EncryptPacket`: un C1 por el largo cifrado y un C2 siempre como C4. La regla es solo del envío: el servidor del laboratorio manda C4 de 36, 69 y 80 bytes, y `DecryptPacket` los acepta |
| relleno | un byte de `rand()` y la pila (pregunta 5) | un parámetro y ceros |
| serial de envío | un byte global (`0x05767913`): 0 al conectar al GameServer (`0x4c1340`), +1 por C3/C4, de 255 vuelve a 0 | `PacketSerials`, uno por conexión |
| descifrado | `0x685b30`: de a 11 bytes; con un checksum malo, -1 | `DecryptPacket`; `CryptoError::ChecksumMismatch` |
| largo útil | suma el de cada bloque sin mirarlo | un bloque que dice 0 o más de 8, o uno que no es el último con menos de 8, es `CryptoError::InvalidBlock` |
| bloques enteros | lee de a 11 sin mirar si alcanzan; el checksum casi seguro falla (*análisis*) | `CryptoError::NotWholeBlocks` |
| serial de recepción (Q-C04) | `0x05767914`: si no coincide, guarda el recibido sin sumar 1, loguea y despacha igual; el desfase queda para siempre | se resincroniza con el recibido + 1, despacha igual y lo informa (`SerialCheck::Resynced`) para que la CL4 lo loguee una vez |
| checksum malo | no despacha, suma 1 al serial de recepción y manda `C1 06 F1 03 06 00` (con XOR32: `C1 06 F1 7B C1 73`) como C3 (`004e1d50-protocol-compiler.md`, confirmado en el asm, `0x4e1e35..0x4e22d4`; qué hace el servidor con él no se sabe). Es lo único que no despacha: con un bloque que dice más de 8, uno del medio con menos o el último con 0, suma igual, despacha, compara el serial como siempre y no avisa; con un solo bloque que trae solo el serial, también, y el opcode es el byte de relleno | igual con `ChecksumMismatch` y `NotWholeBlocks` (que en el original casi siempre termina en un checksum malo). Con los otros errores del descifrado, decisión de MuRet: no despacha, el serial avanza igual y no hay aviso. El aviso lo arma la CL2.5 y lo manda la CL4. Es nuevo para el 03: entra con esta sub-ola |
| rearmado (Q-C10) | `[C1][n]` y lo descifrado sin el serial: declara n y ocupa n + 1 (C4: n + 2) | declara lo que ocupa; menos de 2 bytes descifrados (serial y opcode) es `CryptoError::TooShort` |

Las claves reales cumplen lo que supone `c3c4_reference.py`: módulos de 17 y 18 bits, claves de
hasta 16 bits (el carril 2 de `Dec1.dat` y el de `Enc2.dat`; las del cliente llegan a 15), y
`clave de descifrado * (módulo - 1)` entra en 32 bits. `Enc1.dat` es la inversa de `Dec1.dat`
(mismo módulo, mismo XOR, y el producto de las claves es 1 módulo m), y `Dec2.dat` la de
`Enc2.dat`: medido con los archivos del laboratorio. Con claves de 16 bits, un
`std::uint16_t * std::uint16_t` pasa de `INT_MAX`: todos los productos van en `std::uint32_t`.

**Archivos:**
- **PR A:** crear `Client/src/Network/Crypto/CryptoError.h`, `SimpleModulus.h/.cpp` (la clave y
  los bloques) y `SimpleModulusKeyFile.h/.cpp`; `Client/tests/network/SimpleModulusTest.cpp`,
  `SimpleModulusKeyFileTest.cpp` y `GoldenSimpleModulusKeyFileTest.cpp`.
- **PR B:** crear `Client/src/Network/PacketSerials.h/.cpp` y `EncryptedPacket.h/.cpp`;
  `Client/tests/network/PacketSerialsTest.cpp`, `EncryptedPacketTest.cpp` y
  `GoldenEncryptedPacketTest.cpp`; el C4 armado de nuevo en `Client/tests/vectors/net/`, con su
  línea en `tools/make_vectors.py` y en el README. Modificar 03 (el aviso `F1 03`, la regla de C3
  o C4 por el largo cifrado y el relleno) y 07 (Q-C04, Q-C05 y Q-C10 con sus tests).
- En los dos: `Client/CMakeLists.txt`, PROGRESS y este plan.

Hecho así (PR A): la clave son dos tipos, `EncryptionKey` y `DecryptionKey`, para que una clave
cargada para descifrar no pueda cifrar (los dos archivos tienen el mismo formato, así que el tipo
lo decide el cargador que se usa). Cada uno se arma con `FromLanes`, que rechaza en un carril un
módulo mayor que `0x40000` (2^18) o un XOR con `xor | 0xFFFF` que no es menor que el módulo, lo que
también deja afuera un módulo en 0: el original no lo mira, y dividiría por cero o perdería el
carril (Q-C41, nuevo en 07, con `InvalidKey`). Que el producto entre en 32 bits depende del par, y
no se mira.
`CryptoError` trae en el PR A solo `InvalidKeyFile` e `InvalidKey`; el resto llega con el PR B. Un
archivo de clave de más de 54 bytes carga, y lo que sigue no se lee, como en el original. Los dos
pares sintéticos del paso 2 cumplen las cotas de las claves reales (clave de cifrar por `0xFFFF` y
de descifrar por `m - 1` en 32 bits), así que el producto que da la vuelta se prueba aparte: al
descifrar, con un carril 3 de 18 bits en 1 y la clave de 17 bits; al cifrar, con una clave de 18.
El golden pide las dos carpetas con un fixture nuevo, `GoldenAndWireDataTest`, y la del cable suma
`Dec1.dat` y `Enc2.dat` (el README dice de dónde salen y cómo se arma la carpeta). Las dos
revisiones sumaron: que el checksum, de un byte, no ve uno de cada 256 cambios en un carril más o
menos (`SomeChangedLaneBytesPassTheChecksum`); los bordes de `FromLanes`; un XOR de 17 bits que se
usa entero; y el largo útil entero, también 0 y `0xFF`. Los tests comparten `support/FixedRandom.h`,
`support/SimpleModulusKeys.h` y `support/KeyFiles.h`, que usa también el PR B.

Hecho así (PR B): un C2 sale siempre como C4, como en la ventana de cartas (`0x498820`), que pone
el `0xC4` sin el `cmp`; la regla del largo cifrado queda para los C1, y uno de 186 bytes o más
vuelve como C2, como en el original. Un bloque que dice 0 útiles también es `InvalidBlock`, y
`TooShort` queda para un solo bloque con el serial. Si hay más de un problema gana el checksum
malo, porque el original corta en el primero sin mirar los largos y es el error que lleva el
aviso. El vector del C4 lo arma `wrap` de `make_vectors.py`, que da los cuatro fixtures cifrados de
`mu-client` y, con `Pair17` y tres bloques, lo mismo que `encrypt_block` de `c3c4_reference.py`. El
golden deshace el `0xAA` en cada byte de lo que manda el GameServer: en el laboratorio lo llevan
todos sus paquetes, y el corte da 18 C3 y 7 C4. Las dos revisiones sumaron: un `Packet` al que ya
se le sacaron los bytes es `WrongPacketType` y no toca los seriales; `InvalidBlock` y `TooShort`
gastan el serial que se esperaba aunque el primer bloque pase su checksum y traiga otro, y un
bloque que dice 255, el último o uno del medio, no se copia (con ASan, copiarlo falla); los
paquetes más chicos, un C1 de 3 bytes y un C2 de 4, van y vuelven. La cabecera la escribe
`AppendHeader`, en `Packet.h`, para `PacketWriter` y para cifrar y rearmar. El golden mira además
el paquete rearmado contra los bloques descifrados uno por uno, y cifra de nuevo con
`EncryptPacket` los 183 C3 del cliente, con el relleno que puso el original: la cabecera y todos
los bloques menos el último dan lo mismo, y el último también cuando el original dejó ceros detrás
del relleno, en 110 de los 183.

**Interfaces:**

```cpp
namespace Client::Network::Crypto
{
using Lanes = std::array<std::uint32_t, 4>;
enum class KeyRole : std::uint8_t { Encrypt, Decrypt };

template <KeyRole Role> class SimpleModulusKey
{
public:
    // InvalidKey si un módulo pasa de MaxModulus (0x40000) o `xor | 0xFFFF` no es menor: Q-C41.
    static Core::Expected<SimpleModulusKey, CryptoError> FromLanes(const Lanes& modulus, const Lanes& key,
                                                                   const Lanes& xorKey);
    const Lanes& Modulus() const;
    const Lanes& Key() const; // de cifrado o de descifrado, según Role
    const Lanes& XorKey() const;
};
// Enc1.dat del cliente y Enc2.dat del servidor.
using EncryptionKey = SimpleModulusKey<KeyRole::Encrypt>;
// Dec2.dat del cliente y Dec1.dat del servidor.
using DecryptionKey = SimpleModulusKey<KeyRole::Decrypt>;
Core::Expected<EncryptionKey, CryptoError> LoadEncryptionKey(std::span<const std::byte> file);
Core::Expected<DecryptionKey, CryptoError> LoadDecryptionKey(std::span<const std::byte> file);

std::array<std::byte, 11> EncryptBlock(const EncryptionKey& key, std::span<const std::byte, 8> plain,
                                       std::uint8_t usefulSize);
struct DecryptedBlock
{
    std::array<std::byte, 8> bytes; // los 8, relleno incluido
    std::uint8_t usefulSize;
};
// std::nullopt si el checksum no coincide.
std::optional<DecryptedBlock> DecryptBlock(const DecryptionKey& key, std::span<const std::byte, 11> cipher);
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
Core::Expected<Packet, Crypto::CryptoError> EncryptPacket(const Packet& plain, const Crypto::EncryptionKey& key,
                                                          PacketSerials& serials, std::byte padding = {});
struct DecryptedPacket
{
    Packet packet; // el C1/C2 rearmado, con su tamaño real
    SerialCheck serial;
};
Core::Expected<DecryptedPacket, Crypto::CryptoError> DecryptPacket(const Packet& encrypted,
                                                                   const Crypto::DecryptionKey& key,
                                                                   PacketSerials& serials);
} // namespace Client::Network
```

`CryptoError` tiene `InvalidKeyFile`, `InvalidKey` (Q-C41), `WrongPacketType` (un C3/C4 a
`EncryptPacket`, o un C1/C2 a `DecryptPacket`: no toca los seriales), `ChecksumMismatch`,
`InvalidBlock`, `NotWholeBlocks`, `TooShort` y `TooLarge` (lo cifrado pasaría de `MaxPacketSize`:
con un C2 de más de 5.954 bytes, que solo puede venir de `FromBytes`, porque `PacketWriter` corta
en `0x800`).

- [x] **Paso 1 (Emanuel, `TODO(human)`; lo resolvió Claude):** antes de leer los tests del
  paso 2, el bloque `01 00 00 00 00 00 00 00` con la clave identidad, a mano: los cuatro carriles
  antes y después de la segunda pasada, y dónde caen sus bits en los 11 bytes. La respuesta está
  en el comentario de `SimpleModulusTest.EncryptsASingleBitLikeTheReference`.
- [x] **Paso 2 (PR A):** los tests de los bloques:
  - con dos pares de claves sintéticas escritos como literales en el test (calculados con
    `pow(d, -1, m)` de Python, un módulo de 17 bits y uno de 18), cifrar y descifrar de 1 a 64
    bytes al azar (semilla fija) devuelve lo mismo;
  - con la clave identidad de `mu-client` (módulo 65537, clave 1 y XOR 0 en los cuatro carriles),
    los bloques de sus fixtures;
  - un bloque con un solo bit en 1, contra lo que da `c3c4_reference.py`;
  - el orden de los bits 16 y 17, que la clave identidad nunca prende y un round-trip no ve: con el
    módulo `0x3FFFB` en los cuatro carriles, las claves de cifrado `0x6666 0x5555 0x4444 0x3C3C`,
    las de descifrado `10 12 15 17` y el XOR `0x1234 0x5678 0x9ABC 0xDEF0`, el bloque
    `10 00 00 00 00 00 00 00` con 8 útiles da `E2 32 50 6E A6 98 95 49 10 DD E8`, con el carril 0
    con solo el bit 16 y el 1 con solo el 17 (de `encrypt_block`), y descifra de vuelta;
  - un byte cambiado en el checksum siempre lo hace fallar, y uno en un carril, casi siempre: con
    este checksum de un byte, más o menos uno de cada 256 pasa;
  - un carril cifrado de 18 bits en 1, mayor que su módulo, descifra como `decrypt_block`, con el
    producto que da la vuelta.
- [x] **Paso 3 (PR A):** `EncryptBlock` y `DecryptBlock`.
- [x] **Paso 4 (PR A):** los archivos de clave. Tests con uno sintético de 54 bytes armado en el
  test (magic, tamaño y tres bloques con el XOR de la tabla): carga; otro magic, otro tamaño y uno
  corto son `InvalidKeyFile`. El golden carga `Enc1.dat` y `Dec2.dat` de la carpeta golden, y con
  el cable, `Dec1.dat` y `Enc2.dat`, y mide que cada par es inverso. Después `LoadEncryptionKey` y
  `LoadDecryptionKey`.
- [x] **Paso 5 (PR B):** los tests de los paquetes y los seriales:
  - con la clave identidad, `FIX_C3_SERIAL0`, `FIX_C3_RANDA5` (relleno `0xA5`) y
    `FIX_C3_SERIALFF` (el serial 255, que vuelve a 0), y el C2 de 300 bytes de la CL2.2 como C4 de
    421 con el serial 2 (el `FIX_C4_SERIAL2` armado de nuevo);
  - el borde entre C3 y C4: 23 bloques salen como C3 de 255 bytes, y 24, como C4; un C1 de 185
    bytes sale como C3 y uno de 186, como C4;
  - Q-C05: un C4 de 8.187 bytes (744 bloques) se descifra entero, en 5.952 bytes, más que los
    ~2.046 de la pila del original; un C2 de 5.954 bytes se cifra como ese C4, y uno de 5.955 es
    `TooLarge`;
  - Q-C10: el C3 de `[05] F1 02 01` se rearma como `C1 05 F1 02 01` (en el original, `C1 04`);
  - Q-C04: con el serial de recepción en 5, los seriales 5, 9 y 10 dan `InOrder`, `Resynced` e
    `InOrder` (en el original, dos "Decrypt error" y el desfase queda: caso 5 de
    `red_reference.py`); y 255 seguido de 0 es `InOrder`;
  - `Reset()` vuelve a 0 los dos seriales, como `0x4c1340` al conectar al GameServer;
  - un checksum malo es `ChecksumMismatch` y el serial avanza (caso 6); también
    `NotWholeBlocks`, `InvalidBlock` y `TooShort`, y los cuatro avanzan el serial;
  - un C3 o un C4 que se pasa a `EncryptPacket` es `WrongPacketType` y no gasta serial, y un C1
    que se pasa a `DecryptPacket` tampoco.
- [x] **Paso 6 (PR B):** `PacketSerials`, `EncryptPacket` y `DecryptPacket`.
- [x] **Paso 7 (PR B):** el golden con el cable: los 183 C3 del cliente se descifran con
  `Dec1.dat` (checksum bien, serial desde 0 y +1 en cada conexión) y, bloque por bloque con
  `EncryptBlock` y los 8 bytes descifrados de cada uno (relleno incluido), se vuelven a cifrar con
  `Enc1.dat` del cliente en los mismos bytes: `EncryptPacket` no sirve acá, porque el relleno del
  original trae basura de la pila. Los 25 C3/C4 del servidor (con el `0xAA` deshecho a mano hasta
  la CL2.4) se descifran con `Dec2.dat` con el checksum bien y el serial desde 0, sin un
  `Resynced`, en las 7 conexiones que los traen. El test corta el cable con el `PacketReader`: el
  `--golden` de `c3c4_reference.py` mira solo el primer paquete de cada segmento y ve 10 de los
  25. Sin imprimir un payload.
- [x] **Paso 8:** verificar:
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
| máscara (Q-C18) | `decode_inbound` decide el `0xAA` por el primer paquete de cada `recv` y lo aplica a toda la lectura; no lo llevan C1 `00`, C1 `F4` y C2 `F4`. Corre sobre el buffer entero, con el resto de la lectura anterior, que ya estaba desenmascarado y se enmascara de nuevo (*inferido*, `004e88a0-onread-recv.md`; en el cable no pasa) | por paquete, con su primer byte en el cable: `6B`, `68`, `69` y `6E` (C1 a C4 con el `0xAA`) lo llevan; `C1` y `C2`, no. El lector desenmascara cada byte cuando llega, con la máscara que eligió el primer byte del paquete, y la recuerda si el paquete sigue en la lectura siguiente |
| la lista (`parse_f4_02`) | `C2 F4 02` con cantidad de 1 byte y entradas de 4 pasa a `C2 F4 06` con cantidad de 2 bytes, y a cada código le suma un delta según `(código + 1) / 11` (`0x190`, `0x172`, `0x154`, `0x136`, `0x118`, `0xFA`, `0xDC`, `0xBE`, `0xA0`); desde el grupo 9 el código queda en 0 y la carga se copia igual; el cuarto byte de cada entrada sale en 0; tamaño + 1. No mira la cantidad contra el paquete (Q-C09), y lo que viene detrás de la lista en la misma lectura queda en 0 (Q-C18) | igual en el paquete; una cantidad que no entra es `ObfuscationError::InvalidPacket`, y los paquetes de al lado no se tocan |
| `reset_queue` | a un C1/C3 `F3 11` le mete un `00` en `pkt[5]` y suma 1 al tamaño, truncado a un byte (uno de 255 queda con tamaño 0); con menos de 5 bytes copia de más (Q-C09: un `C1 04 F3 11` tumba al cliente) | igual, y menos de 5 bytes o uno de 255 es `InvalidPacket`. Con el tamaño real que deja la CL2.3 (Q-C10), un C3 `F3 11` conserva su último byte, que el original perdería (*análisis*: no hay ninguno en las capturas) |
| `send_forward` | sobre lo que va a `0x685ac0` (`[serial][op]...` y el byte de relleno): deshace el XOR32, remapea el opcode (`0x10→0x1D`, `0x19→0xA5`, `0x1E→0xA6`) y lo vuelve a aplicar | igual, en `BeforeEncrypt` |
| `encode_outbound` | sobre todo C1 que sale: a `C1 06 F4 03` le resta al código la escalera de 8 rangos, que no tiene el `0x154` (Q-C17), sin tocar el XOR32; a los demás les deshace el XOR32, remapea `D3→A3` y `D7→A4` y lo vuelve a aplicar. C2, C3 y C4 pasan igual | igual, en `BeforeSend` |
| el dispatcher (Q-C13) | sin FoxAnti nadie lo llama | el dispatcher es del cliente (CL4); `IdentityObfuscation` no toca nada |

El orden es el del original. Al recibir: la máscara y el corte, `AfterCut`, el descifrado si es
C3/C4, y `BeforeDispatch`. Al mandar: `Protocol` arma el paquete (con XOR32 si lo lleva); si va
cifrado, `EncryptPacket` llama a `BeforeEncrypt` antes de cifrar; y todo lo que sale pasa por
`BeforeSend`. La máscara la aplica el `PacketReader`, porque la necesita para leer el tamaño;
`AfterCut`, `BeforeDispatch` y `BeforeSend` los llama quien recibe y manda, que es la conexión de
la CL4. En esta ola los llaman los tests y el golden, en ese orden.

**Archivos:** crear, en `Client/src/Network/` y en el namespace `Client::Network`, como el resto de
la capa, `Obfuscation.h`, `ObfuscationError.h`, `IdentityObfuscation.h/.cpp` y
`FoxAntiObfuscation.h/.cpp`; `Client/tests/network/IdentityObfuscationTest.cpp`,
`FoxAntiObfuscationTest.cpp` y `GoldenFoxAntiObfuscationTest.cpp`. Modificar `PacketReader` y
`EncryptPacket` (las sobrecargas de abajo), `Client/CMakeLists.txt`, 07 (Q-C09, Q-C17 y Q-C18 con
sus tests, y en Q-C18 el resto que se enmascara de nuevo), PROGRESS y este plan.

Hecho así: el `PacketReader` desenmascara cada byte recién cuando lo mira en `Next`, no en `Feed`,
porque hasta leer el tamaño de un paquete no sabe dónde empieza el siguiente, que puede llevar
otra máscara; recuerda hasta dónde desenmascaró y con qué, y nunca toca un byte del paquete que
sigue. Un C3 o un C4 que llega en claro (`C3`, `C4`) se lee en claro: el original lo enmascararía
y lo perdería, y el laboratorio no los manda así. En `EncryptPacket`, lo que se cifra lleva
siempre el byte de relleno detrás, aunque el último bloque quede lleno y el relleno no se cifre,
porque `BeforeEncrypt` lo transforma como el original, que lo escribe igual detrás del paquete; lo
que recibe lo fija `EncryptedPacketTest` con una ofuscación que lo anota.
`BeforeEncrypt` y `BeforeSend` deshacen el XOR32, remapean y lo vuelven a aplicar siempre, como el
original, sin atajo cuando no hay remapeo: que entonces no cambie nada lo prueban los tests. La
lista pide sus entradas enteras (una a la que le falta el cuarto byte, que el original no lee,
también es `InvalidPacket`), lo que sigue a las entradas sale en 0 como el `calloc` del original,
y la traducida tiene que entrar en `MaxPacketSize`. El `F3 11` se mira solo en un C1: en MuRet un
C3 descifrado ya es un C1, y un `C1 03 F3`, sin subcódigo, pasa igual. `Obfuscation` se copia y se
mueve solo desde una clase hija, para no quedarse con la base, y `IdentityObfuscation::Shared()`
es una `static` local, que no depende del orden en que arrancan los archivos. Lo que se espera de
cada traducción sale de la FoxAnti mínima compilada en el host con
`Client/tests/vectors/net/tools/foxanti_cases.c` (su README dice cómo), y los tests usan los
nombres de sus casos. El golden cuenta 3.033 paquetes S->C (144 del ConnectServer en claro y 2.889
del GameServer con el `0xAA`), traduce las 72 listas al código 400, lleva todo lo del GameServer
hasta el dispatcher con 7 `F3 11` de 8 bytes, y compara los 8 `F4 03` del cliente con lo que da
`BeforeSend` con el 400; de a un byte da lo mismo. Ninguno de los 2.396 segmentos S->C del cable
termina a mitad de un paquete, así que el resto que el original enmascara de nuevo (Q-C18) no se
ve ahí.

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
    [[nodiscard]] virtual Core::Expected<Packet, ObfuscationError> AfterCut(Packet packet) const = 0;
    // S->C, ya descifrado, antes del dispatcher.
    [[nodiscard]] virtual Core::Expected<Packet, ObfuscationError> BeforeDispatch(Packet packet) const = 0;
    // C->S: lo que se va a cifrar, con el serial en el byte 0 y el byte de relleno al final.
    virtual void BeforeEncrypt(std::span<std::byte> sealed) const = 0;
    // C->S: un paquete que sale al cable.
    [[nodiscard]] virtual Packet BeforeSend(Packet packet) const = 0;
};

// Lo que cambia en PacketReader y EncryptPacket. Lo de la CL2.1 y la CL2.3 queda igual y usa una
// IdentityObfuscation estática, así que sus tests no cambian. La Obfuscation vive más que el
// lector: las dos son de la conexión (CL4).
class PacketReader
{
public:
    PacketReader();
    explicit PacketReader(const Obfuscation& obfuscation);
    // Feed y Next, como en la CL2.1.
};
Core::Expected<Packet, Crypto::CryptoError> EncryptPacket(const Packet& plain,
                                                          const Crypto::EncryptionKey& key,
                                                          PacketSerials& serials,
                                                          const Obfuscation& obfuscation,
                                                          std::byte padding = {});
} // namespace Client::Network
```

`ObfuscationError` tiene `InvalidPacket` (un largo o una cantidad que no entra).

- [x] **Paso 1 (Emanuel, `TODO(human)`; lo resolvió Claude):** antes de leer los tests del paso 2,
  la ida y vuelta del código 21, a mano con las dos tablas. La respuesta está en el comentario de
  `FoxAntiObfuscationTest.ServerCodesComeBackTheSameOnlyFrom0To20`.
- [x] **Paso 2:** los tests:
  - `IdentityObfuscation` deja todo igual;
  - los del cable del 03: la lista `C2 00 0A F4 02 01 00 00 00 CC` pasa a
    `C2 00 0B F4 06 00 01 90 01 00 00`, el `C1 06 F4 03 90 01` sale como `C1 06 F4 03 00 00`, y
    `C1 08 F3 11 FE 04 16 68` pasa a `C1 09 F3 11 FE 00 04 16 68`;
  - la máscara: los tres exentos van en claro, un C3 y un C4 con el `0xAA` se cortan bien, y un
    C1 `F4` exento seguido en el mismo `Feed` de un C1 enmascarado da los dos bien (Q-C18);
  - un paquete enmascarado partido entre dos `Feed`: un C2 y un C4 después de su primer y de su
    segundo byte, y un C1 a mitad del cuerpo, dan lo mismo que enteros;
  - los cinco remapeos, contra lo que da la FoxAnti mínima (compilada en el host, como en el 04)
    con los mismos bytes, y en `BeforeEncrypt` también el byte de relleno: uno del `0x1E` con
    `0x29` como primer byte, como los tres `A6` de la sesión real;
  - lo que FoxAnti no toca: en `BeforeSend`, un C3 y un C4 (`FIX_C3_SERIAL0` y el C4 de la
    CL2.3) y `C1 04 F4 02`; en `BeforeEncrypt`, el aviso `F1 03` y el login, byte por byte;
  - Q-C17: los códigos 0 a 20 vuelven iguales, el 21 sale como 51, y desde el 98 la lista trae el
    código 0, con la carga copiada, y vuelve 0;
  - Q-C09: `C1 04 F3 11`, un `C1 FF F3 11` de 255 bytes y una lista con una cantidad de más son
    `InvalidPacket`; los paquetes bien formados dan lo mismo que la FoxAnti mínima (los casos de
    `test_packet.c` de `mu-client`: `test_decode_inbound`, `test_reset_queue`, `test_recv_forward`,
    `test_send_forward`, `test_send_chain_remap` y `test_encode_outbound_ladder`, y los de
    `test_net_foxanti.c`).
- [x] **Paso 3:** `IdentityObfuscation`, `FoxAntiObfuscation`, y las sobrecargas de
  `PacketReader` y `EncryptPacket`.
- [x] **Paso 4:** el golden con el cable: con `FoxAntiObfuscation`, el S->C de las 72 conexiones se
  corta en 3.033 paquetes sin bytes de más (144 del ConnectServer en claro; 2.889 del GameServer,
  todos con el `0xAA`), y las 72 listas `F4 02` pasan a `F4 06` con el código 400. El golden de la
  CL2.3 deja de deshacer el `0xAA` a mano.
- [x] **Paso 5:** verificar:
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
// F4 06
Core::Expected<std::vector<ServerEntry>, ProtocolError> ReadServerList(const Network::Packet& packet);
Outgoing WriteServerSelect(std::uint16_t code);
struct ServerAddress { std::string host; std::uint16_t port; };
Core::Expected<ServerAddress, ProtocolError> ReadServerAddress(const Network::Packet& packet);

struct ServerHello { std::uint8_t result; std::uint16_t playerIndex; std::array<char, 5> version; };
Core::Expected<ServerHello, ProtocolError> ReadServerHello(const Network::Packet& packet);

// No tiene operator<<: la cuenta y la clave no se imprimen nunca.
struct LoginRequest { std::string_view account; std::string_view password; std::uint32_t tick; };
// FieldTooLong con una cuenta o una clave de más de 10 bytes.
Core::Expected<Outgoing, ProtocolError> WriteLoginRequest(const LoginRequest& request);
Core::Expected<std::uint8_t, ProtocolError> ReadLoginResult(const Network::Packet& packet);
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

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** antes de leer los tests del paso 2, decodificar a mano
  `C1 0C F1 00 01 00 05 31 30 32 30 39` (detective de bytes): qué es cada campo y en qué orden van
  los bytes del índice.
- [ ] **Paso 2:** los tests, uno por paquete, con los bytes del 04 y los fixtures de `mu-client`:
  - `FIX_LOGIN_PLAIN` (cuenta y clave de 8 bytes, completadas con dos `\0`) y
    `FIX_LOGIN_FULL_PLAIN` (de 10, sin `\0`), y sus C3 con la clave identidad; una cuenta de 11
    bytes es `FieldTooLong`, y una vacía deja en el campo, antes del XOR32, los 10 bytes de la
    clave XOR3 (`FC CF AB FC ...`), porque `0x641bf0` aplica el XOR a los 10 bytes del campo;
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
- [ ] **Paso 3:** mover `Xor3` y escribir los paquetes.
- [ ] **Paso 4:** el golden con el cable. De los 9 C3 del cliente que descifrados llevan el opcode
  `F1`, el test elige los que, sin el XOR32, tienen el subcódigo `01` (el subcódigo viaja con XOR32,
  así que hay que deshacerlo para elegir): son 7, los login, de 49 bytes, que empiezan con
  `C1 31 F1 01` y traen la versión `09908` en `+28` (el `muproxy.log` es del build histórico; el
  actual manda `10209`). Los otros 2 son `F1 02` de 5 bytes. De la cuenta, la clave y el serial no
  se mira nada. Los 156 keepalive descifran a 12 bytes que empiezan con `C1 0C 0E 00`.
- [ ] **Paso 5:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "Protocol|Login|Keepalive|ServerList|Hello|Xor3"
  ```

---

## Al cerrar la CL2

- [ ] Completar el cierre de la guía de la CL2.0.
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Escribir el plan de la CL3 (plataforma y bucle de juego) en su propio PR, con lo aprendido.
