# Plan de la ola 1: Protocolo sin red

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Roadmap: [`2026-10-25-dataserver-port-design.md`](../specs/2026-10-25-dataserver-port-design.md).
> Escrito por adelantado el 2026-10-26; se revisa al arrancar la ola con lo aprendido en la 0.

**Objetivo:** convertir bytes en paquetes y paquetes en bytes, sin abrir un solo socket. Todo lo
de esta ola se prueba con arrays de bytes literales.

**Arquitectura:** una biblioteca estática `DataServerLib` con todo el código del servidor; el
ejecutable `DataServer` y los tests `DataServerTests` linkean contra ella. Esta ola llena
`Protocol/` (bytes, structs) y el `FrameParser` de `Network/`, que no depende de sockets.

**Stack:** C++20 (`std::byte`, `std::span`, `std::optional`, `enum class`), GoogleTest.

**Research que fija esta ola:** [04-red](../../../Server/DataServer/docs/research/04-red.md)
(framing, parser, dominio de tamaños, P-1…P-5) y
[05-protocolo](../../../Server/DataServer/docs/research/05-protocolo.md) (layouts de `0x00` y
`0x40`). Quirks: Q-02, Q-03, Q-04 de
[08](../../../Server/DataServer/docs/research/08-quirks-y-bugs.md).

**Terminada cuando:** pasan en CI (Windows y Linux) los vectores de prueba del parser (completo,
partido, varios por recepción, cabecera inválida, `size == 0`, tamaño fuera de dominio y el caso
P-1) y el round-trip de los structs de `0x00` y `0x40`.

## Preguntas para decidir al empezar

1. **Tamaño máximo de frame.** El original acepta en la práctica 3..0x800 (y desborda de 0x801 a
   0x2000). ¿MuRet acepta hasta 0x2000 con un buffer que crece, o fija 0x800? Pista: ningún head
   legítimo del GS 1.02 pasa de 0x800, pero el `0x06` responde con un C2 de 0x3BC y el `0x08` con
   uno de 0x4C8 (salen del servidor, no entran).
2. **Qué hace el parser ante un error.** ¿Devuelve un error y el que llama cierra la conexión, o
   descarta y sigue? El original descarta todo el acumulado con cabecera inválida y se queda
   trabado con `size == 0`.
3. **Structs empaquetados o lectura campo por campo.** El roadmap (D-6) pide `#pragma pack(1)` con
   `static_assert(sizeof)` para documentar el layout, pero leer un struct empaquetado con
   `reinterpret_cast` sobre el buffer es comportamiento indefinido en C++. ¿Cómo combinás las dos
   cosas?

---

## Sub-ola 1.1: helpers de bytes

**Concepto:** `std::byte` (un byte que no es un número), `std::span` (una vista que no es dueña
de la memoria), endianness (little-endian para los campos de MU, big-endian para el tamaño de C2)
y por qué no se castea un buffer a `uint16_t*`.

**Archivos:**
- Crear: `Server/DataServer/src/Protocol/ByteReader.h`, `ByteReader.cpp`, `ByteWriter.h`, `ByteWriter.cpp`
- Crear: `Server/DataServer/tests/protocol/ByteReaderTests.cpp`, `ByteWriterTests.cpp`
- Modificar: `Server/DataServer/CMakeLists.txt` (biblioteca `DataServerLib`; el ejecutable y los tests linkean contra ella)

- [ ] **Paso 1 (Claude, andamiaje):** crear el target `DataServerLib` (estático), mover `main.cpp` a linkear contra él y registrar los nuevos archivos de test.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** escribir primero los tests de `ByteReader`: leer `u8`, `u16le`, `u32le`, `u16be`, un string de largo fijo sin `\0` garantizado, y leer más allá del final (tiene que fallar sin tocar memoria ajena).
- [ ] **Paso 3 (Emanuel):** implementar `ByteReader` sobre `std::span<const std::byte>` con un cursor; cada lectura devuelve `std::optional` o marca un error, nunca lee fuera del span.
- [ ] **Paso 4 (Emanuel):** lo mismo para `ByteWriter` sobre un `std::vector<std::byte>`.
- [ ] **Paso 5:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R Byte
  ```

## Sub-ola 1.2: `FrameParser` C1/C2

**Concepto:** TCP es un flujo de bytes, no de mensajes; cómo se corta en frames; máquina de
estados; invariantes (el parser solo mira bytes recibidos, siempre termina, nunca escribe fuera
del buffer). Repasar la máquina de estados del original en
[04 § El parser](../../../Server/DataServer/docs/research/04-red.md#el-parser-0x403010).

**Archivos:**
- Crear: `Server/DataServer/src/Network/FrameParser.h`, `FrameParser.cpp`
- Crear: `Server/DataServer/tests/protocol/FrameParserTests.cpp`

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** escribir la tabla de vectores de prueba **antes** del código. Mínimo:
  - C1 completo `C1 04 20 AA` → un frame con head `0x20`.
  - C2 completo con tamaño big-endian (por ejemplo `C2 00 05 52 AA`).
  - La cabecera partida entre dos `Feed`: primero `C1`, después `04 20 AA`.
  - Dos frames en un solo `Feed`.
  - Primer byte inválido (`0x00`) → error.
  - `size == 0` → error (el original se traba acá).
  - `size` menor que la cabecera (C1 con 1 o 2) → error.
  - C2 mayor al máximo que decidiste → error (Q-03).
  - **P-1:** `C1 04 20 AA` seguido de `C1 03` en el mismo `Feed` → un frame y queda esperando; no inventa un segundo frame (Q-04).
- [ ] **Paso 2 (Emanuel):** diseñar la interfaz pública. Sugerencia: `void Feed(std::span<const std::byte>)` y `std::optional<Frame> Next()` o un resultado que distinga "frame", "falta data" y "error".
- [ ] **Paso 3 (Emanuel):** implementar hasta que pase la tabla.
- [ ] **Paso 4:** test de propiedad sencillo: partir un mismo flujo válido en trozos aleatorios (semilla fija) y comprobar que siempre salen los mismos frames.
- [ ] **Paso 5:** verificar con `ctest --preset linux-x64-debug -R FrameParser`.

## Sub-ola 1.3: structs de paquetes

**Concepto:** layout en memoria, padding y alineación, `static_assert(sizeof)` como documentación
que compila, y serializar campo por campo con `ByteReader`/`ByteWriter`.

**Archivos:**
- Crear: `Server/DataServer/src/Protocol/Heads.h` (los 22 heads como `enum class`)
- Crear: `Server/DataServer/src/Protocol/GameServerPackets.h`, `GameServerPackets.cpp` (`0x00` request y respuesta; marcadores `C1 08 41` y paquete `C1 40 40` de `0x40`)
- Crear: `Server/DataServer/tests/protocol/GameServerPacketsTests.cpp`

- [ ] **Paso 1 (Emanuel):** `Heads.h` con los 22 heads de [05](../../../Server/DataServer/docs/research/05-protocolo.md#tabla-de-heads).
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** los tests de round-trip: bytes de referencia → struct → bytes, idénticos. Para `0x00`: clase@+3, puerto u16@+4, nombre@+6 (0x30), serverId u16@+0x38; respuesta `C1 08 00` con result@+3 e ItemCount u32@+4. Verificá los offsets de `0x40` en el research antes de escribirlos.
- [ ] **Paso 3 (Emanuel):** implementar `Read`/`Write` por campo. Un request más corto de lo que pide su layout se **rechaza** (el original no recibe el largo y lee basura; ver [05 § convenciones](../../../Server/DataServer/docs/research/05-protocolo.md#convenciones-de-las-tablas)).
- [ ] **Paso 4:** `static_assert(sizeof(...))` en cada struct empaquetado que documente un layout del wire.
- [ ] **Paso 5:** verificar con `ctest --preset linux-x64-debug -R Packets` y en CI.

---

## Al cerrar la ola 1

- [ ] Nota de aprendizaje en `docs/learning/ola-1-protocolo.md` (span, endianness, máquinas de estado).
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Revisar el plan de la ola 2 con lo aprendido.
