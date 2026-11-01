# Plan de la ola CL1: Formatos sin ventana

> Plan de ejecución en el formato de superpowers (writing-plans): cada tarea tiene
> archivos, pasos chicos con checkbox y el comando que verifica el resultado.
> Roadmap: [`2026-10-30-cliente-design.md`](../specs/2026-10-30-cliente-design.md).
> Escrito el 2026-11-01. De la CL1.0 a la CL1.3 están detalladas; la CL1.4 y la CL1.5 tienen un
> esbozo con su contrato, que se detalla al arrancar cada una con lo aprendido en la anterior.

**Objetivo:** leer los archivos de `Data\` igual que el original, en funciones puras que van de
bytes a tipos de C++, sin ventana y sin globales. Cada formato se prueba con vectores literales
(siempre corren) y contra la carpeta golden (si está).

**Arquitectura:** todo va en `ClientLib`, en la capa `Client::Data` (`Client/src/Data/`, con
`Tables/`, `Text/`, `Terrain/` y `Textures/`, como dice el roadmap). Un cargador recibe un
`std::span<const std::byte>` y devuelve la tabla o un error. Leer el archivo del disco es tarea
del que llama, así los tests no tocan el disco salvo los golden. El `Expected` propio va en
`Client::Core` (`Client/src/Core/`). Como toda N.0, la CL1.0 vive en `labs/`.

**Stack:** C++20 (`std::byte`, `std::span`, `std::array`, `std::variant`, `<bit>`), GoogleTest.

**Guía de estudio:** [`docs/learning/ola-cl1.0-bytes-y-archivos.md`](../../learning/ola-cl1.0-bytes-y-archivos.md)
para la CL1.0. De la CL1.1 en adelante, la sección de cada sub-ola de este plan.

**Research que fija esta ola:**
- [05-formatos-de-datos](../../../Client/docs/research/05-formatos-de-datos.md): tablas, terreno,
  texturas y `message.wtf`.
- Los quirks Q-C01, Q-C02, Q-C07, Q-C08, Q-C12, Q-C14, Q-C15 y de Q-C23 a Q-C27 de
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
   estándar todavía no trae. Lo usan todos los cargadores. Iba a ser tuyo en la CL1.0, pero el
   2026-11-01 pediste seguir con el cliente y lo escribió Claude como ejemplo trabajado. Cuando el
   servidor lo necesite (ola 5), se decide si pasa a un `Common/` compartido (pregunta 2 del
   roadmap del cliente).
2. **Qué hace un cargador con un archivo corto.** El original hace un `fread` de tamaño fijo y no
   mira el retorno: un `Text.bmd` corto deja en la tabla lo que había en el heap.
   *Recomendación:* error de tamaño sin tocar nada, como hace `mu-client`, y los bytes que sobran
   se ignoran, como en el original. **Se tomó la recomendación en la CL1.1** (ejemplo trabajado,
   2026-11-01): `DataError::FileTooShort`, y la CL1.2 la siguió en las otras tres tablas. Quedó
   en el 07 como Q-C24. Si preferís otra cosa, se cambia en su PR.
3. **¿Los volcados de memoria del original son una segunda carpeta golden?** `mu-golden` (en el
   repo de research, fuera de Git) tiene las tablas tal como quedaron en la memoria del `main.exe`
   original (`memoria-cliente/2026-10-09-devias/s31_text.bin` y siguientes). Son el oráculo más
   fuerte: comparar contra el archivo solo prueba que el XOR es involutivo. Las tablas del
   laboratorio (`Gate.bmd` y las de `Data\Local\`) tienen el mismo SHA-256 que las de
   `mu-golden`, así que los volcados valen para ellas. *Recomendación:* una variable `MURET_CLIENT_MEMORY_DIR` con la misma
   regla que la golden (sin variable se saltea, incompleta falla). **Se tomó la recomendación en
   la CL1.1**: `MemoryDumpTest` en `Client/tests/support/GoldenDir.h`. Va solo en el entorno,
   porque `gtest_discover_tests` no pasa dos variables en `ENVIRONMENT`. **En la CL1.4** pasó a
   apuntar a `memoria-cliente/`, la carpeta de las tres capturas, para que entren Lorencia y sus
   objetos.
4. **Biblioteca JPEG (CL1.4).** Es la pregunta 1 del roadmap. **Decidido (Emanuel, 2026-11-01):
   un decodificador propio compatible con la libjpeg 6b**, porque libjpeg-turbo da los mismos bytes
   en los 2.380 JPEG del laboratorio pero no fuera de ellos. Tiene su plan:
   [`2026-11-01-cl1.4-decodificador-jpeg.md`](2026-11-01-cl1.4-decodificador-jpeg.md).

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
- [x] **Paso 4 (Claude, ejemplo trabajado):** `Client::Core::Expected<T, E>`, primero sus tests:
  - guarda un valor o un error, nunca los dos;
  - pedir el valor de uno con error es un error de quien llama, y el test lo deja escrito.

  Las decisiones de la actividad 10 de la guía (los nombres, T y E del mismo tipo) están
  explicadas en el comentario de `Expected.h`. Te queda contarlas con tus palabras.

Los pasos 1 a 3 y 5 (la `ByteView` y el jefe en `labs/`) y las actividades 1 a 6 de la guía
quedan para después: el 2026-11-01 elegiste seguir con el cliente.
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

Tres cosas del original que no se copian:
- Usa el texto como formato de `sprintf` (`FUN_00633300`). MuRet nunca pasa un texto de datos
  como formato.
- Lee el texto 7 como cadena de C: como su registro no tiene `\0`, sigue en el registro 8 (519
  bytes con el archivo real). MuRet corta en los 300 bytes del registro (Q-C07).
- Si falta el archivo, muestra un `MessageBox` y sigue con la tabla en cero (Q-C12). En MuRet
  vuelve un error. Los cargadores de la CL1 reciben bytes y no abren archivos, así que Q-C12 lo
  cumple, con su test, el arranque que lee las tablas del disco: si falta una, el cliente no
  arranca. Llega a más tardar con la primera escena (CL5.4).

**Archivos:**
- Crear: `Client/src/Data/ByteReader.h/.cpp`: la `ByteView` de la CL1.0 llevada a `ClientLib`
  sobre `std::span` (L-3: en el producto se usa el estándar).
- Crear: `Client/src/Data/DataError.h` (`enum class` con lo que puede fallar en esta sub-ola).
- Crear: `Client/src/Data/Tables/Xor3.h/.cpp` y `Client/src/Data/Tables/TableChecksum.h/.cpp`.
- Crear: `Client/src/Data/Text/TextTable.h/.cpp`.
- Crear: `Client/tests/data/ByteReaderTest.cpp`, `Xor3Test.cpp`, `TableChecksumTest.cpp`,
  `TextTableTest.cpp` y `GoldenTextTableTest.cpp`.
- Crear: `Client/tests/support/` un helper que lee los bytes de un archivo golden.
- Modificar: `Client/CMakeLists.txt`.

Los pasos 1 a 8, menos el 3b, los hizo Claude como ejemplo trabajado (2026-11-01), después de que
pidieras seguir con el cliente: los 1 a 4 fueron en un PR con `ByteReader` (#16), y `Text.bmd` en
el siguiente. Queda tuyo el test del checksum de dos dwords (paso 3b, actividad 12 de la guía).
`ByteReader` se escribió directo en `ClientLib`; la `ByteView` de `labs/` (CL1.0) queda para
comparar las dos.

- [x] **Paso 1:** los tests de XOR3, con vectores escritos a mano:
  - `00 00 00 00` da `FC CF AB FC`;
  - aplicarlo dos veces devuelve el original;
  - aplicado a un registro de 300 bytes, da lo mismo que aplicado al archivo entero, porque
    `300 % 3 == 0`.
- [x] **Paso 2:** `Xor3(std::span<std::byte>)`.
- [x] **Paso 3:** los tests del checksum, y uno golden por tabla:
  - los de `mu-client/tests/assets/test_bmd_tables.c` (bytes `00..1f`: `0x033426d0` con la
    clave de `Item` y `0x21abebaa` con la de `Skill`);
  - un buffer vacío da la semilla `key << 9`;
  - el de la `Skill.bmd` y el de la `itemtest.bmd` reales son los que guardan los archivos. La
    `item.bmd` del laboratorio no pasa el suyo (Q-C01) y queda para la CL1.2.
- [ ] **Paso 3b (Emanuel, `TODO(human)`):** el test del checksum de dos dwords, calculado a mano
  (guía, actividad 12).
- [x] **Paso 4:** `TableChecksum(bytes, key, paridad)`. La paridad es un `enum class`,
  no un `bool`. Se usa recién en la CL1.2, pero va acá porque es parte de cómo se leen las
  tablas.
- [x] **Paso 5:** los tests de `TextTable` con un archivo sintético de `0x96000` bytes:
  - el texto 0 y el 2047;
  - un registro de 300 bytes sin `\0` (Q-C07);
  - un archivo corto (pregunta 2);
  - un índice fuera de rango: ¿qué devuelve `Text(2048)`? En el original, un índice que
    depende del servidor puede pasarse de 2047 (`FUN_004da070`, caso 7).
- [x] **Paso 6:** `LoadTextTable(std::span<const std::byte>)` devuelve un
  `Expected<TextTable, DataError>`, y `TextTable::Text(index)` devuelve un `std::string_view` de
  hasta 300 bytes, cortado en el primer `\0` si lo hay. Con un índice de 2048 o más devuelve
  `std::nullopt`: el texto va en un `std::optional`. Sobre una tabla temporal no compila, porque el
  texto colgaría de ella. Los textos quedan como bytes: pasarlos a UTF-8 para dibujarlos es de la
  CL5.3.
- [x] **Paso 7:** el test golden. `Data\Local\Text.bmd` mide 614.400 bytes, carga, y su registro 7
  ocupa 300 bytes sin `\0`. La pregunta 3 salió que sí: además, cada texto (hasta su `\0`, que es
  lo que muestra `Text()`) es igual al que tenía en memoria el original en `s31_text.bin`.
  Comparado aparte, byte a byte, el `Text.bmd` descifrado es igual al volcado entero.
- [x] **Paso 8:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "ByteReader|Xor3|TableChecksum|TextTable"
  ```
  Si el PR queda grande, XOR3 y el checksum van en uno y `Text.bmd` en otro.

## Sub-ola CL1.2: `Gate`, `Item` y `Skill.bmd`

**Concepto:** registros de layout fijo, y qué hacer cuando el contrato incluye un bug.

**Contrato** ([05 § Tablas](../../../Client/docs/research/05-formatos-de-datos.md#tablas-bmd) y,
en el repo de research, `0054a360-gate-bmd-loader.md`, `0054b9e0-item-bmd-loader.md` y
`0054acf0-skill-bmd-loader.md`):

| Tabla | Formato | Lo raro | MuRet |
| --- | --- | --- | --- |
| `Gate.bmd` | 256 × 9 bytes, XOR3 por registro, sin checksum | con un archivo corto, el original llena los registros que faltan con lo que quedó en el buffer del anterior (§2 de su cargador) | es un bug interno (Q-C24): `DataError::FileTooShort` |
| `Item.bmd` | 8192 × `0x54` bytes + checksum de 4 bytes, clave `0xE2F1`, XOR en los dwords impares | Q-C01: el original no valida el checksum (un `EB 66` lo saltea) y el archivo real no pasa el suyo | es contrato: se calcula, se informa (`checksumMatches`) y se carga igual, también si al archivo le falta el checksum |
| `Skill.bmd` | 256 × `0x3C` bytes + checksum, clave `0x5A18`, XOR en los dwords pares | si el checksum no coincide, el original no carga ("File corrupted."); Q-C08: el contenedor guardia con `rand()` | `DataError::ChecksumMismatch`; el contenedor no se porta |

En los tres, XOR3 vuelve a empezar en cada registro: el original llama a `0x5496c0` con el largo
del registro. En `Gate` da lo mismo que de punta a punta, porque `9 % 3 == 0`; en `Item` y `Skill`
(`0x54` y `0x3C`) también, pero la tabla no lo supone. Los bytes que sobran se ignoran, como en el
original. Los campos de cada registro (nombres, niveles, requisitos) quedan para la CL9: acá van
crudos, con el layout de la memoria del cliente.

**Rutas:** `Gate.bmd` está en la raíz de `Data\` (la cadena `Data\Gate.bmd` en `0x6d2aec`), e
`Item.bmd` y `Skill.bmd` en `Data\Local\`. El `Data\` del laboratorio tiene además
`Item\item.bmd` y `skill\skill.bmd`, con otro SHA-256: los tests golden leen los de `Local\`.

**Los oráculos:**
- `Gate`: `Data\Gate.bmd` descifrado es igual a `s31_gate.bin` (`0x900` bytes, el volcado de
  `DAT_07688ab8`). La `Gatetest.bmd` del laboratorio es el mismo archivo.
- `Item`: la `Item.bmd` del laboratorio mide 688.212 bytes, `0x50` más que los registros y su
  checksum, y no pasa el suyo con ninguna de las dos paridades: el cliente parcheado no lo mira
  (Q-C01). Sus primeros `0xA8000` bytes descifrados son iguales a `s31_item.bin`. La
  `itemtest.bmd` (688.132 bytes, el tamaño justo) pasa su checksum: es la que prueba la clave y la
  paridad.
- `Skill`: la `Skill.bmd` real pasa su checksum, solo con la paridad par. No hay test contra la
  memoria: el volcado (`s31_skill_live3.bin`) es la tabla dentro del contenedor guardia, no la
  tabla en claro (Q-C08).

**Archivos:**
- Crear: `Client/src/Data/Tables/RecordTable.h`, la plantilla `RecordTable<RecordSize>` que
  comparten las tres tablas (regla 4 de `docs/CODING_RULES.md`: si dos o más lugares necesitan
  lo mismo, se saca a un helper).
- Crear: `Client/src/Data/Tables/GateTable.h/.cpp`, `ItemTable.h/.cpp` y `SkillTable.h/.cpp`.
- Modificar: `Client/src/Data/Tables/TableChecksum.h/.cpp` (`StoredChecksum`) y
  `Client/src/Data/DataError.h` (`ChecksumMismatch`).
- Crear: `Client/tests/data/RecordTableTest.cpp`, `GateTableTest.cpp`, `ItemTableTest.cpp`,
  `SkillTableTest.cpp` y sus `Golden*TableTest.cpp`, y `Client/tests/support/TableFiles.h`
  (arma archivos sintéticos: cifra por registro y agrega el checksum).
- Modificar: `Client/CMakeLists.txt` y `Client/tests/data/GoldenTableChecksumTest.cpp`.

Los pasos 1 a 6 los hizo Claude como ejemplo trabajado (2026-11-01), a pedido tuyo de seguir con
el cliente; el 7 queda tuyo. Una decisión quedó tomada sin preguntarte, con la regla de 07: el `Gate.bmd` corto es
un error, y no se reproducen los registros alternados (Q-C24). Si preferís otra cosa, se cambia
en su PR.

- [x] **Paso 1:** `RecordTable<RecordSize>`. Copia los registros enteros, descifra cada uno con
  XOR3 y devuelve el registro `i` como un `std::span<const std::byte, RecordSize>` dentro de un
  `std::optional`: `std::nullopt` si `i >= Count()`. Como `TextTable::Text()`, sobre una tabla
  temporal no compila. Tests: sobre ceros, cada registro de 4 bytes da `FC CF AB FC` (de punta a
  punta, el segundo daría `CF AB FC CF`), un registro incompleto no entra, y un índice que
  multiplicado por el largo da la vuelta no tiene registro.
- [x] **Paso 2:** `StoredChecksum(file, recordsSize)`: el dword little-endian que sigue a los
  registros, o `std::nullopt` si no llega a haber cuatro bytes.
- [x] **Paso 3:** `LoadGateTable`, con sus tests (en claro, archivo corto, bytes de más) y el
  golden contra el archivo y contra `s31_gate.bin`.
- [x] **Paso 4:** `LoadItemTable` devuelve un `LoadedItemTable { ItemTable table; bool
  checksumMatches; }`: con un checksum que no coincide, o sin checksum, carga igual. Sin todos los
  registros es `FileTooShort`. El golden carga la `Item.bmd` real (con `checksumMatches` en
  `false`) y la compara con `s31_item.bin`; la `itemtest.bmd` da `true`.
- [x] **Paso 5:** `LoadSkillTable`: con un checksum que no coincide, o un registro cambiado,
  devuelve `DataError::ChecksumMismatch`. El golden carga la `Skill.bmd` real.
- [x] **Paso 6:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "RecordTable|GateTable|ItemTable|SkillTable|TableChecksum"
  ```
- [ ] **Paso 7 (Emanuel, `TODO(human)`, opcional):** el comportamiento viejo de Q-C24, que la
  regla del 07 pide mostrar en un test. En `labs/`, el bucle del original sobre un buffer de 9
  bytes que se reusa, y un test que muestre los registros alternados con un `Gate.bmd` cortado
  entre registros. Es la misma idea que el jefe Q-C07 de la CL1.0.

## Sub-ola CL1.3: terreno

**Concepto:** un cifrado con estado (K16) y una grilla de 256 × 256 celdas guardada fila por fila.

**Qué archivos carga el cliente** (la pregunta que el 05 dejaba "a confirmar en CL1"): solo los
`EncTerrain`. El cargador de mundo (`0x61a560`) arma `Data\World<N>\EncTerrain<N>.map`, `.att` y
`.obj` (las cadenas de `0x6d12e8`, `0x6c7314` y `0x6d12b8`). N es el mundo + 1, salvo que varios
mundos comparten carpeta (11 a 17 la 12, 18 a 23 la 19, 24 a 29 y 36 la 25; el detalle en el 05).
Dos `.att` llevan otro número en el nombre (`EncTerrain312.att` en World31, `351` o `352` en
World35), pero su cabecera dice el de la carpeta: el `world` de los cargadores es siempre el N de
`World<N>`. Los `Terrain.map`, `Terrain.obj` y `Terrain.att` sueltos no los carga nadie (solo los
nombra el exportador de `0x61ada0`, una herramienta de desarrollo, y los del laboratorio son otro
formato, en claro), y los `Terrain<N>.att` de 65.539 bytes (cabecera `00 FF FF`) son el formato
del servidor. Los `EncTerraintest*` del laboratorio no los pide nadie.

**Se parte en dos PRs:**
- **PR A (#19):** K16, `EncTerrain.map` y `EncTerrain.att`.
- **PR B (este):** `EncTerrain.obj` (cabecera de 4 bytes y N registros de 30: tipo, posición,
  ángulo y escala) y `TerrainHeight.OZB` (Q-C02: las alturas desde BMP + 1080, por 1,5; por 3 en
  el mundo `0x37`).
- `TerrainLight.OZJ` es un JPEG: pasa a la CL1.4, con el decodificador JPEG.

**Contrato** ([05 § Terreno](../../../Client/docs/research/05-formatos-de-datos.md#terreno-de-un-mundo-dataworldn)
y, en el repo de research, `005f87b0-encterrain-map.md` y `005f8390-encterrain-att.md`):

| Archivo | Formato | Lo raro | MuRet |
| --- | --- | --- | --- |
| K16 | `plano[i] = (K[i % 16] ^ c[i]) - previo`, `previo = c[i] + 0x3D`, `previo0 = 0x5E`, un solo flujo desde el byte 0 | el previo sale del byte **cifrado** | `DecryptK16`, en el lugar |
| `.map` | `0x30002` bytes, solo K16: versión (no se lee), mundo, capa 1, capa 2, alfa | el original no mira el tamaño: con uno corto lee después del buffer (Q-C24) | `FileTooShort`; los bytes de más se ignoran |
| | el alfa es el byte por `0x3B808081` (1/255 en float) | no es dividir por 255: con 51 da otro float | se multiplica por la misma constante; 255 da 1.0f justo |
| | en memoria, la capa 2 va antes que la capa 1 (`0x07AACC8C` y `0x07ABCC8C`) | es fácil invertirlas | el test de memoria compara cada capa con su mitad del volcado |
| `.att` | `0x10004` (1 byte por celda) o `0x20004` (un WORD, queda el byte de abajo); K16 y después XOR3, cabecera incluida; cabecera `00 N FF FF`; atributos menores que `0x80` | con otro tamaño devuelve -1 sin avisar y el mundo se queda con los atributos del anterior (Q-C25); la celda testigo por mundo (Q-C26) | `UnexpectedSize`; la cabecera o un atributo malo es `InvalidData`; la celda testigo no se porta |
| `.obj` | K16, sin XOR3; cabecera: un byte que no se lee, el mundo y la cantidad (int16 con signo); un objeto cada 30 bytes: tipo (int16), posición, ángulo y escala (7 floats) | la cantidad no se compara con el tamaño (Q-C24); con cero o negativa no hay objetos; Devias trae dos NaN y World7 un tipo -515 (Q-C28) | `FileTooShort`; los floats con sus bits, con `std::bit_cast`; los bytes de más se ignoran |
| alturas | sin cifrar: 4 bytes, una cabecera de BMP que el original toma como de `0x438` y un byte por celda, por 1,5 (por 3 en World56) | no mira `bfOffBits` (`0x436` en los de 8 bits del laboratorio): lee dos bytes más adelante (Q-C02); no mira la cabecera, y World11 (24 bits) y World19 (todo ceros) cargan | se reproduce: desde el byte `0x43C`; `FileTooShort` por debajo de 66.620 bytes |
| `.map`, `.att` y `.obj` | el byte 1 es el número del mundo, y si no es N el que llama muestra "%s file corrupted." | | `WrongWorld` |

Si falta el `.map`, el original carga el mundo con valores por defecto (capa 1 en 0, capa 2 en
`0xFF`, alfa en 0) sin avisar (Q-C27); la tabla de `rand()` que llena junto con ellos es del
dibujo. Las dos cosas son de quien abre el archivo, en la CL7: acá los cargadores reciben bytes.

**Los oráculos:**
- Los 20 mundos del `Data\` del laboratorio cargan su `.map` y su `.att` con su número. El 1 y
  los del 31 en adelante tienen el `.att` de dos bytes por celda; el 31 y el 56 traen celdas con el
  byte de arriba distinto de cero. Los tres `.att` con otro número en el nombre cargan con el de
  su carpeta.
- `.map` de Lorencia: los conteos que midió el research con su propio descifrado (7.939 celdas de
  agua, 43.785 sin capa 2, 14.629 con alfa y 73 con alfa sin capa 2).
- `.map` de Devias contra la memoria del original: `s31_l2l1.bin` (capa 2 y capa 1) y
  `s31_alpha.bin` (los 65.536 floats), byte a byte. Desde la CL1.4, también el de Lorencia
  (`lor_l2l1.bin` y `lor_alpha.bin`).
- `.att` de Devias contra `s31_att.bin`: iguales salvo las 10 celdas que el juego ya había
  cambiado (el bit `0x02` de la celda del personaje y dos pasajes con el `0x04` limpiado), que el
  test lista una por una, como el research (`s31-assets/byte-a-byte.md`).
- `.att` de Lorencia, el de dos bytes por celda: de las 7.939 celdas de agua del `.map`, 7.818
  tienen el atributo 4, el contraste del research entre los dos archivos. Desde la CL1.4 también
  contra la memoria de Lorencia (`lor_att.bin`), salvo 7 celdas de estado de juego.
- `.obj`: los 20 mundos cargan, cada uno con justo los objetos que caben en su tamaño. Lorencia
  tiene lo que midió el research (2.870 objetos de 109 tipos, de 0 a 153, escalas de 0,30 a 3,28,
  y los bits que da descifrados de los dos primeros). El golden lista los objetos raros de todo el
  laboratorio (Q-C28): los dos NaN de Devias, el tipo -515 de World7 y los dos en cero que lo
  siguen, y las cuatro escalas negativas. Desde la CL1.4, los objetos de Lorencia son los 2.870
  de la memoria del original (`obj_nodes.bin`, en la escena 5), con los mismos bits y en el mismo
  orden.
- Alturas: los 19 mundos que las traen cargan. Lorencia tiene las del research (máximo 382,5 y las
  dos últimas celdas en 0), World56 llega a 765 (255 por 3) y World19 es plano. Devias, contra
  `s31_height.bin` (las alturas) y `s31_bmphdr.bin` (los `0x438` bytes de cabecera que copió el
  original); leer desde `bfOffBits` da 52.139 alturas distintas. Desde la CL1.4, también Lorencia
  (`lor_height.bin` y `lor_bmphdr.bin`).

**Archivos:**
- Crear: `Client/src/Data/Terrain/K16.h/.cpp`, `TerrainGrid.h` (`CellIndex`, `TerrainLayer<T>`),
  `TerrainMapping.h/.cpp` y `TerrainAttributes.h/.cpp`; en el PR B, `TerrainObjects.h/.cpp` y
  `TerrainHeights.h/.cpp`.
- Modificar: `Client/src/Data/DataError.h` (`WrongWorld`, `UnexpectedSize`, `InvalidData`).
- Crear: `Client/tests/data/K16Test.cpp`, `TerrainMappingTest.cpp`, `TerrainAttributesTest.cpp`,
  sus `Golden*Test.cpp`, y `Client/tests/support/TerrainFiles.h` (cifra como el exportador del
  original y arma archivos sintéticos con un patrón que no se repite cada fila).
- Modificar: `Client/CMakeLists.txt`, 05 y 07.
- No entra ninguna biblioteca: estos formatos son del MU (`CODING_RULES.md` §10). Para las alturas
  se miró la de BMP que ya está en el proyecto, `SDL_LoadBMP_IO` de SDL3, en su código y
  corriéndola sobre los archivos del laboratorio: salta a `bfOffBits` y da vuelta las filas, y con
  el de Devias da 64.500 alturas distintas de las de la memoria (solo con el salto serían 52.139).
  Tampoco carga el de World11 (un BMP cortado) ni el de World19 (no empieza con "BM"). Además son
  unas pocas líneas.

Los pasos 1 a 4 los hizo Claude como ejemplo trabajado (2026-11-01), a pedido tuyo de seguir con
el cliente. Quedaron tomadas sin preguntarte, con la regla de 07: el error por otro mundo en el
cargador y no en el que llama, el `UnexpectedSize` de Q-C25, y los atributos como un WORD por
celda, como en la memoria del original. Si preferís otra cosa, se cambia en su PR.

- [x] **Paso 1:** `DecryptK16`. Tests: dos bytes a mano (si el previo saliera del byte en claro,
  el segundo daría otro), la clave que vuelve a empezar en el byte 16 sin la semilla, el vector
  del research (`8B E5 24 92`), la vuelta con el cifrado del exportador (`0x5f8960`) y una mitad
  descifrada sola, que sale mal solo en su primer byte.
- [x] **Paso 2:** `LoadTerrainMapping(file, world)`. Tests: las tres capas en el orden del
  archivo, la celda (x, y) en `y * 256 + x`, los bits del alfa sacados a mano, la versión que no
  se lee, otro mundo, archivo corto y bytes de más. El golden carga los 20 mundos y compara Devias
  con la memoria.
- [x] **Paso 3:** `LoadTerrainAttributes(file, world)`. Tests: la cabecera cifrada del archivo
  real, uno y dos bytes por celda (con el byte de arriba en uso), otros tamaños, la cabecera, el
  atributo `0x80`, otro mundo, que un dato malo gane sobre otro mundo, y la celda testigo que no se
  mira (Q-C26). El golden carga los 20 mundos y los tres `.att` con otro nombre, y compara Devias
  con la memoria.
- [x] **Paso 4:** verificar:
  ```bash
  cmake --build --preset linux-x64-debug && ctest --preset linux-x64-debug -R "K16|Terrain"
  ```
- [x] **Paso 5 (PR B):** `LoadTerrainObjects(file, world)` y `LoadTerrainHeights(file, world)`.
  Tests del `.obj`: los bytes descifrados del research, cada campo en su lugar, el tipo con
  signo, los bits de cualquier float (NaN, NaN de señal, -0, subnormal, infinito), un archivo que
  es solo la cabecera, cantidad cero o negativa, archivo corto, bytes de más, el byte que no se
  lee y otro mundo. Tests de las alturas: por 1,5 en todos los mundos y por 3 solo en World56, los
  256 valores justos, la celda (x, y) sin dar vuelta las filas, `bfOffBits` que no se mira
  (Q-C02), una cabecera en cero, archivo corto y bytes de más. Lo hizo Claude como ejemplo
  trabajado, igual que los pasos 1 a 4 y por el mismo pedido tuyo de seguir con el cliente. Quedaron
  tomadas sin preguntarte: un `.obj` corto es `FileTooShort` antes que `WrongWorld`, como el
  `.map`; una cantidad negativa da la lista vacía; las alturas reciben el N de la carpeta, como los
  otros, y de la cabecera del BMP no se guarda nada (en el original solo la lee el guardador de
  `0x5f92b0`).

## Sub-ola CL1.4: texturas OZT y OZJ

**Concepto:** una imagen dentro de una textura de lados potencia de 2, el orden de las filas y de
los canales, y cuándo una biblioteca hace exactamente lo que hacía el original.

**Se parte en tres PRs:**
- **PR 0:** `MURET_CLIENT_MEMORY_DIR` apunta a `memoria-cliente/`, la carpeta de las tres
  capturas (`MemoryCapture` en `GoldenDir.h`: la carpeta y el mundo), y los cargadores de la CL1.3
  suman los oráculos de Lorencia. Prepara los de las texturas, que están en esa captura
  (`lor2_slot*.bin`).
- **PR A (este):** OZT, un TGA de 32 bits con 4 bytes delante.
- **PR B:** OZJ, un JPEG con 24 bytes delante, y `TerrainLight.OZJ`, la luz de base del terreno,
  que viene de la CL1.3. Va sobre el decodificador JPEG propio, que se arma antes en siete PRs
  ([su plan](2026-11-01-cl1.4-decodificador-jpeg.md); el PR B es su sub-ola J8).

**Contrato** ([05 § Texturas](../../../Client/docs/research/05-formatos-de-datos.md#texturas) y, en
el repo de research, `0063bd00-ozt-loader.md`, `0063b870-ozj-loader.md` y
`0063b490-terrainlight-ozj-loader.md`):

| Archivo | Formato | Lo raro | MuRet |
| --- | --- | --- | --- |
| tamaño | la imagen va sin escalar en la esquina de un buffer de lados potencia de 2, desde 1 y con tope en 512 | el resto del buffer queda sin inicializar | `TextureSide`; el resto, en cero (Q-C30) |
| OZT | 4 bytes que no se leen y un TGA: ancho y alto (int16 con signo) en `0x10` y `0x12`, bits por píxel en `0x14`, píxeles BGRA desde `0x16` | no mira el tipo, el id ni el descriptor: da vuelta siempre las filas; rechaza otros bits por píxel y lados de más de 512 sin avisar, y deja pasar uno de cero o negativo; no mira el tamaño (Q-C24) | `InvalidData` para el rechazo; `FileTooShort`; RGBA con las filas dadas vuelta |
| OZJ | 24 bytes que no se leen y un JPEG, que decodifica la libjpeg 6b con sus parámetros por defecto | rechaza un lado de más de 512; no da vuelta las filas; 7 OZJ son TGA (Q-C14) y libjpeg los rechaza | `Jpeg::Decode`, el decodificador propio (PR B) |
| luz | un OZJ de 256 × 256, cada byte por `0x3B808081` (1/255 en float) | la fila 0 es la última del JPEG | PR B |

**Los oráculos:**
- Los 664 OZT del laboratorio cargan, salvo `Monster\wolfa02.OZT`, el único de 24 bits, que el
  original rechaza (Q-C29). 133 no son de lados potencia de 2.
- Los 9 cursores de `Interface` y `TileGrass01.OZT` de World1 son los buffers que tenía en memoria
  el original (`lor2_slot002..00a.bin` y `lor2_slot032.bin`), byte a byte.
- PR B: los 9 tiles OZJ de World1 (`lor2_slot023..02b.bin`) y la luz de Devias y de Lorencia
  (`s31_light.bin`, `lor_light.bin`) contra la memoria del original.

**Archivos (PR A):**
- Crear: `Client/src/Data/Textures/Texture.h` (`Texture`, `PixelFormat`, `TextureSide`) y
  `OztTexture.h/.cpp`.
- Crear: `Client/tests/data/TextureTest.cpp`, `OztTextureTest.cpp`, `GoldenOztTextureTest.cpp`,
  `Client/tests/support/TextureFiles.h` (arma un OZT como el exportador) y `LittleEndian.h` (sale
  de `TerrainFiles.h`, que ahora la comparte).
- Modificar: `Client/CMakeLists.txt`, `Client/README.md`, 05, 07 (Q-C24, Q-C29 y Q-C30), PROGRESS y
  este plan.

**Bibliotecas (`CODING_RULES.md` §10):**
- **TGA:** se miró stb_image. No entra porque lo que se lee del TGA son unas pocas líneas: el buffer
  de potencias de 2 y el rechazo habría que escribirlos igual, y la dependencia cuesta más. Además
  lee el descriptor y el largo del id, que el original no mira, y carga `Monster\wolfa02.OZT`, de
  24 bits, que el original rechaza. En los 663 de 32 bits del laboratorio daría las mismas filas.
- **JPEG (pregunta 4 de este plan y 1 del roadmap):** se compiló la IJG libjpeg 6b original (el
  `libjpeg6b_6b1.orig.tar.gz` del archivo de Ubuntu, idéntico a la 6b salvo el build) con `INT32`
  de 32 bits, como el `main.exe`, y se decodificaron los 2.380 OZJ que son JPEG en el laboratorio,
  como lo hace `0x63b870`. La 6b da los mismos bytes que la memoria del original en los 9 tiles de
  Lorencia. libjpeg-turbo 3.2.0, la del baseline de vcpkg, da los mismos bytes que la 6b en los
  2.380, con SIMD y sin SIMD, y con o sin el suavizado de bloques de los progresivos (623 lo son).
  Pero las reviews del PR B midieron dónde turbo no es la 6b: lee JPEG aritméticos, sin pérdida y
  de 12 bits, que la 6b rechaza; agranda el 4:4:0 con otro filtro; suaviza con otro método un
  progresivo al que le faltan scans, y no tiene cómo imitar un muestreo que mezcla un componente al
  doble de alto con otro al doble de ancho. Nada de eso aparece en el laboratorio, pero el contrato
  es la 6b, así que **no entra ninguna biblioteca**: Emanuel decidió (2026-11-01) escribir un
  decodificador compatible con la 6b, con la 6b como especificación y su salida como oráculo
  ([su plan](2026-11-01-cl1.4-decodificador-jpeg.md)).

- [x] **Paso 0 (PR 0):** los volcados por captura, y Lorencia como oráculo de la CL1.3.
- [x] **Paso 1 (PR A):** `TextureSide` y `LoadOztTexture(file)`. Tests: filas dadas vuelta y
  BGRA a RGBA, una imagen de 3 × 5 en una textura de 4 × 8 con el resto en cero, cada píxel de una
  de 512 × 512 y de una de 5 × 300, solo 32 bits por píxel, lados hasta 512, cero y negativo (con el
  buffer entero), los campos que no se leen, archivo corto (también el que es solo la cabecera),
  los bits por píxel antes que los píxeles que faltan y bytes de más. Golden con los 664 y contra la
  memoria.
- [ ] **Paso 2 (J1 a J7):** el decodificador JPEG, un PR por sub-ola, como dice
  [su plan](2026-11-01-cl1.4-decodificador-jpeg.md).
- [ ] **Paso 3 (PR B, la J8 de ese plan):** `LoadOzjTexture(file)` y `LoadTerrainLight(file)` sobre
  `Jpeg::Decode`, con vectores chicos que corren en CI: JPEG sintéticos de los tres tipos del
  laboratorio (baseline 4:4:4 con un reinicio por fila de MCU, baseline 4:2:0 y progresivo 4:4:4),
  con el resultado que da la 6b.

Lo hizo Claude como ejemplo trabajado, por el mismo pedido tuyo de seguir con el cliente. Quedaron
tomadas sin preguntarte: el rechazo del OZT es `InvalidData` (el original devuelve false sin
avisar), un lado de cero o negativo carga sin píxeles como en el original, y el resto del buffer
queda en cero (Q-C30).

Una textura que falta corta el arranque (Q-C23), pero eso es de quien abre el archivo: los
cargadores reciben los bytes. Entra con el código que abre los archivos de las texturas.

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
