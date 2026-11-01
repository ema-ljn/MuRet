# Plan de la ola CL1: Formatos sin ventana

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md).
> Escrito el 2026-11-01. La CL1.0 y la CL1.1 están detalladas; de la CL1.2 a la CL1.5 hay un
> esbozo con su contrato, que se detalla al arrancar cada una con lo aprendido en la anterior.

**Objetivo:** leer los archivos de `Data\` igual que el original, en funciones puras que van de
bytes a tipos de C++, sin ventana y sin globales. Cada formato se prueba con vectores literales
(siempre corren) y contra la carpeta golden (si está).

**Arquitectura:** todo va en `ClientLib`, en la capa `Client::Data` (`Client/src/Data/`, con
`Bmd/`, `Terrain/`, `Textures/` y `Text/`, como dice el roadmap). Un cargador recibe un
`std::span<const std::byte>` y devuelve la tabla o un error. Leer el archivo del disco es tarea
del que llama, así los tests no tocan el disco salvo los golden. El `Expected` propio va en
`Client::Core` (`Client/src/Core/`). Como toda N.0, la CL1.0 vive en `labs/`.

**Stack:** C++20 (`std::byte`, `std::span`, `std::array`, `std::variant`, `<bit>`), GoogleTest.

**Guía de estudio:** [`docs/learning/ola-cl1.0-bytes-y-archivos.md`](../../learning/ola-cl1.0-bytes-y-archivos.md)
para la CL1.0. De la CL1.1 en adelante, la sección de cada sub-ola de este plan.

**Research que fija esta ola:**
- [05-formatos-de-datos](../../../Client/docs/research/05-formatos-de-datos.md): tablas, terreno,
  texturas y `message.wtf`.
- Los quirks Q-C01, Q-C02, Q-C07, Q-C08, Q-C12, Q-C14, Q-C15 y Q-C23 de
  [07](../../../Client/docs/research/07-quirks-y-bugs.md).
- La evidencia de cada cargador, con su asm, en
  `client-research/analysis/mainexe/evidence/s31-assets/agentes/` del repo de research (por
  ejemplo `005495e0-text-bmd-loader.md`).
- `client-research/mu-client/src/assets/bmd_tables.c` es la reimplementación en C99 del research:
  sirve de oráculo para comparar, no se copia.

**Terminada cuando:**
- las cuatro tablas, el terreno de un mundo, las texturas OZT y OZJ y `message.wtf` (si
  `main.exe` lo lee, ver la CL1.5) se leen desde bytes, con su round-trip sintético en CI;
- con `MURET_CLIENT_GOLDEN_DIR`, cada formato se lee entero desde los archivos reales, y lo que el
  research midió (tamaños, checksums, quirks) da igual.

## Preguntas para decidir al empezar

1. **Cómo se devuelve un error.** El roadmap decía `std::expected`, que es de C++23, y MuRet
   compila en C++20. **Decidido (Emanuel, 2026-11-01): un `Expected<T, E>` propio**, como pide L-3
   del [camino](../specs/2026-10-28-camino-de-aprendizaje-design.md): se construye lo que el
   estándar todavía no trae. Lo escribís vos en la CL1.0 y lo usan todos los cargadores. Cuando el
   servidor lo necesite (ola 5), se decide si pasa a un `Common/` compartido (pregunta 2 del
   roadmap del cliente).
2. **Qué hace un cargador con un archivo corto.** El original hace un `fread` de tamaño fijo y no
   mira el retorno: un `Text.bmd` corto deja en la tabla lo que había en el heap.
   *Recomendación:* error de tamaño sin tocar nada, como hace `mu-client`, y los bytes que sobran
   se ignoran, como en el original. Se decide en la CL1.1.
3. **¿Los volcados de memoria del original son una segunda carpeta golden?** `mu-golden` (en el
   repo de research, fuera de Git) tiene las tablas tal como quedaron en la memoria del `main.exe`
   original (`memoria-cliente/2026-10-09-devias/s31_text.bin` y siguientes). Son el oráculo más
   fuerte: comparar contra el archivo solo prueba que el XOR es involutivo. Las tablas del
   laboratorio (`Gate.bmd` y las de `Data\Local\`) tienen el mismo SHA-256 que las de
   `mu-golden`, así que los volcados valen para ellas. *Recomendación:* una variable `MURET_CLIENT_MEMORY_DIR` con la misma
   regla que la golden (sin variable se saltea, incompleta falla). Se decide en la CL1.1.
4. **Biblioteca JPEG (CL1.4).** Es la pregunta 1 del roadmap y se decide al llegar.

---

## Sub-ola CL1.0: bytes y archivos (N.0, en `labs/`)

**Concepto:**
- la memoria como un arreglo de bytes, y cómo se representan los enteros (sin signo y en
  complemento a dos);
- endianness, alineación, y por qué no se castea un buffer a `std::uint32_t*`;
- cadenas de largo fijo;
- errores como valor: un tipo resultado.

Es la 1.0 del camino con material del cliente. La 1.0 del servidor no se hizo, así que va
entera, y suma lo que necesita la CL1: el `Expected`.

**Archivos:**
- Crear: `docs/learning/ola-cl1.0-bytes-y-archivos.md` (la guía; Claude)
- Crear: `labs/cl1.0-bytes/ByteView.h`, `ByteView.cpp`, `ByteViewTest.cpp` y `FixedTextTest.cpp` (el jefe)
- Crear: `Client/src/Core/Expected.h`, `Client/tests/core/ExpectedTest.cpp`
- Modificar: `labs/CMakeLists.txt`, `labs/README.md`, `Client/CMakeLists.txt`

- [ ] **Paso 1 (Claude, andamiaje):** GoogleTest en `labs/` (desde la CL0.2 está en `vcpkg.json`) y
  el target de la carpeta nueva. Va después del merge del PR #11, que toca el mismo
  `labs/CMakeLists.txt`.
- [ ] **Paso 2 (Emanuel, `TODO(human)`):** los tests de `Labs::ByteView` primero, con bytes
  literales:
  - `ReadU8`, `ReadU16Le`, `ReadU32Le` y `ReadU16Be`. Por ejemplo, `F1 E2 00 00` como `u32le` es
    `0xE2F1`, la clave del checksum de `Item.bmd`.
  - Una lectura que se pasa del final falla sin tocar memoria ajena.
- [ ] **Paso 3 (Emanuel):** `ByteView` (puntero y largo, con acceso que chequea los límites) y las
  lecturas con desplazamientos de bits, sin `reinterpret_cast`. Después, la misma lectura con
  `std::span`, `std::bit_cast` y `std::endian`, y comparar (guía, actividad 8).
- [ ] **Paso 4 (Emanuel, `TODO(human)`):** `Client::Core::Expected<T, E>`, primero sus tests:
  - guarda un valor o un error, nunca los dos;
  - pedir el valor de uno con error es un error de quien llama, y el test lo deja escrito.

  La interfaz la decidís vos (guía, actividad 10).
- [ ] **Paso 5 (Emanuel, jefe):** Q-C07. El registro 7 de `Text.bmd` ocupa sus 300 bytes sin un
  `\0`. Escribí el test que muestra que leerlo como una cadena de C sigue en el registro 8, y que
  con un largo fijo no pasa. Con un buffer que termina justo ahí, ASan lo marca.
- [ ] **Paso 6:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "ByteView|Expected|FixedText"
  ```

## Sub-ola CL1.1: XOR3, checksum y `Text.bmd`

**Concepto:** el primer formato real. Un cifrado XOR que se deshace aplicándolo de nuevo, un
checksum con aritmética de 32 bits que da la vuelta, y una tabla de registros de largo fijo.

**Contrato** ([05 § Tablas](../../../Client/docs/research/05-formatos-de-datos.md#tablas-bmd) y
`005495e0-text-bmd-loader.md`):

| Qué | Original | Regla |
| --- | --- | --- |
| XOR3 | `0x5496c0`, clave en `0x6c8c74` | byte `i` XOR `{FC, CF, AB}[i % 3]`, con `i` desde el principio del buffer que se pasa |
| checksum | dentro de los cargadores de `Item` y `Skill` | `acc = key << 9`; por cada dword *i* (little-endian): XOR o suma según su paridad; si `i % 4 == 0`, `acc ^= (acc + key) >> ((i & 7) + 1)`. En `Item` (`key = 0xE2F1`) van con XOR los dwords impares; en `Skill` (`0x5A18`), los pares |
| `Text.bmd` | `0x5495e0` / `0x549700` | `0x96000` bytes = 2048 registros de 300, sin cabecera, contador ni checksum, con XOR3 de punta a punta. El texto `i` está en `300 * i`; el registro 7 no tiene `\0` (Q-C07) |

Dos cosas del original que no se copian:
- Usa el texto como formato de `sprintf` (`FUN_00633300`). MuRet nunca pasa un texto de datos
  como formato.
- Si falta el archivo, muestra un `MessageBox` y sigue con la tabla en cero (Q-C12). En MuRet
  vuelve un error.

**Archivos:**
- Crear: `Client/src/Data/ByteReader.h/.cpp`: la `ByteView` de la CL1.0 llevada a `ClientLib`
  sobre `std::span` (L-3: en el producto se usa el estándar).
- Crear: `Client/src/Data/DataError.h` (`enum class` con lo que puede fallar en esta sub-ola).
- Crear: `Client/src/Data/Bmd/Xor3.h/.cpp` y `Client/src/Data/Bmd/TableChecksum.h/.cpp`.
- Crear: `Client/src/Data/Text/TextTable.h/.cpp`.
- Crear: `Client/tests/data/ByteReaderTest.cpp`, `Xor3Test.cpp`, `TableChecksumTest.cpp`,
  `TextTableTest.cpp` y `GoldenTextTableTest.cpp`.
- Crear: `Client/tests/support/` un helper que lee los bytes de un archivo golden.
- Modificar: `Client/CMakeLists.txt`.

- [ ] **Paso 1 (Emanuel, `TODO(human)`):** los tests de XOR3, con vectores escritos a mano:
  - `00 00 00 00` da `FC CF AB FC`;
  - aplicarlo dos veces devuelve el original;
  - aplicado a un registro de 300 bytes, da lo mismo que aplicado al archivo entero, porque
    `300 % 3 == 0`.
- [ ] **Paso 2 (Emanuel):** `Xor3(std::span<std::byte>)`.
- [ ] **Paso 3 (Emanuel, `TODO(human)`):** los tests del checksum:
  - el de dos dwords, calculado a mano (guía, actividad 12);
  - los de `mu-client/tests/assets/test_bmd_tables.c` (bytes `00..1f`: `0x033426d0` con la
    clave de `Item` y `0x21abebaa` con la de `Skill`);
  - un buffer vacío da la semilla `key << 9`.
- [ ] **Paso 4 (Emanuel):** `TableChecksum(bytes, key, paridad)`. La paridad es un `enum class`,
  no un `bool`. Se usa recién en la CL1.2, pero va acá porque es parte de cómo se leen las
  tablas.
- [ ] **Paso 5 (Emanuel, `TODO(human)`):** los tests de `TextTable` con un archivo sintético de
  `0x96000` bytes:
  - el texto 0 y el 2047;
  - un registro de 300 bytes sin `\0` (Q-C07);
  - un archivo corto (pregunta 2);
  - un índice fuera de rango: ¿qué devuelve `Text(2048)`? En el original, un índice que
    depende del servidor puede pasarse de 2047 (`FUN_004da070`, caso 7).
- [ ] **Paso 6 (Emanuel):** `LoadTextTable(std::span<const std::byte>)` devuelve un
  `Expected<TextTable, DataError>`, y `TextTable::Text(index)` devuelve un `std::string_view` de
  hasta 300 bytes, cortado en el primer `\0` si lo hay. Los textos quedan como bytes: pasarlos a
  UTF-8 para dibujarlos es de la CL5.3.
- [ ] **Paso 7 (Claude, andamiaje si lo pedís):** el test golden. `Data\Local\Text.bmd` mide
  614.400 bytes, carga, y su registro 7 ocupa 300 bytes sin `\0`. Si la pregunta 3 sale que sí,
  además la tabla es igual byte a byte a `s31_text.bin`.
- [ ] **Paso 8:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "ByteReader|Xor3|TableChecksum|TextTable"
  ```
  Si el PR queda grande, XOR3 y el checksum van en uno y `Text.bmd` en otro.

## Sub-ola CL1.2: `Gate`, `Item` y `Skill.bmd` (esbozo)

**Concepto:** registros de layout fijo, y qué hacer cuando el contrato incluye un bug.

| Tabla | Formato | Lo raro |
| --- | --- | --- |
| `Gate.bmd` | 256 × 9 bytes, XOR3 por registro | con un archivo corto, el original reusa el buffer del registro anterior (`0054a360-gate-bmd-loader.md` §2): se reproduce o se rechaza, a decidir |
| `Item.bmd` | 8192 × `0x54` bytes + checksum de 4 bytes, clave `0xE2F1` | Q-C01: el original no valida el checksum (un `EB 66` lo saltea) y el archivo real no pasa el suyo; se calcula, se informa y se carga igual |
| `Skill.bmd` | 256 × `0x3C` bytes + checksum, clave `0x5A18` | si el checksum no coincide, el original no carga ("File corrupted."); Q-C08: el contenedor guardia con `rand()` no se porta |

Las tablas se cargan desde `Data\Local\`. El `Data\` del laboratorio tiene además `Item\item.bmd` y
`skill\skill.bmd`, con otro SHA-256: los tests golden tienen que leer los de `Local\`. Los campos
de cada registro (nombres, niveles, requisitos) quedan para la CL9; acá van crudos, con el layout
de la memoria del cliente.

## Sub-ola CL1.3: terreno (esbozo)

**Concepto:** un cifrado con estado (K16) y una grilla de 256 × 256 celdas.

- K16: XOR de 16 bytes con `prev0 = 0x5E` y paso `0x3D`, un solo flujo.
- `EncTerrain<N>.map` (`0x30002` bytes), `.att` (K16 y XOR3), `.obj` (N × 30 bytes en 256 bloques).
- `TerrainHeight.OZB`: Q-C02, las alturas se leen desde BMP + 1080.
- Antes de escribir: cuáles carga el cliente, ¿los `EncTerrain` o los `Terrain/*` sueltos?
  (05, "a confirmar en CL1").

## Sub-ola CL1.4: texturas OZT y OZJ (esbozo)

**Concepto:** formatos con prefijo, TGA y JPEG, y lados en potencia de 2.

- OZT: 4 bytes + TGA de 32 bpp; lados de hasta 512.
- OZJ: 24 bytes + JPEG baseline. Q-C14: 7 OZJ son en realidad TGA.
- La biblioteca JPEG es la pregunta 4 de este plan.
- Una textura que falta corta el arranque (Q-C23).

## Sub-ola CL1.5: `message.wtf` (esbozo)

- Formato: cabecera `CC 01 "WTF File"`, entradas `id:2 len:2 texto` con XOR `0xCA`. Q-C15: largo
  validado.
- **Antes de escribirla:** [05](../../../Client/docs/research/05-formatos-de-datos.md#el-pipeline-de-las-dlls)
  infiere que `main.exe` no carga `mumsg.dll` en el arranque normal, que la usa el launcher.
  Si se confirma, la CL1.5 sale de la ola.

---

## Al cerrar la CL1

- [ ] Completar el cierre de la [guía de la CL1.0](../../learning/ola-cl1.0-bytes-y-archivos.md).
- [ ] Actualizar `docs/learning/PROGRESS.md`.
- [ ] Revisar el plan de la CL2 (protocolo sin red) con lo aprendido: el `ByteReader` y el
  `Expected` de esta ola son la base del `PacketReader`.
