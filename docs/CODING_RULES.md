# Reglas de código

Estas reglas valen para todo cambio de código en MuRet, lo escriba una persona o un agente. Leelas
antes de commitear.

> Adaptadas de [`docs/CODING_RULES.md` de sven-n/MuMain](https://github.com/sven-n/MuMain/blob/main/docs/CODING_RULES.md).
> Se conservan sus doce reglas, traducidas. Se sacó lo que es propio de un cliente (C#, UI,
> render) y se agregó lo del DataServer: sus capas, el research como contrato y la regla 13 de
> tests.

---

## 1. Funciones cortas y enfocadas

- Una función hace **una cosa**. Si para describirla necesitás un "y", partila.
- Apuntá a funciones que entren en una pantalla (unas 40 líneas). Las que tienen mucha lógica
  (un parser, un handler con varios pasos), más cortas todavía.
- Si el pedazo que extraés no pertenece al archivo actual, **movelo a otro archivo** en la carpeta
  que corresponda.

## 2. Salir temprano

- Los errores y los casos borde se manejan **arriba** de la función.
- Nada de anidar de más: mejor `if (malo) return …;` que envolver todo el cuerpo en
  `if (bueno) { … }`.
- Cada salida temprana tiene una razón clara. Un error no se traga en silencio: se devuelve o se
  loguea.

## 3. Nada hardcodeado

- Números, cadenas, rutas, tamaños, timeouts: si no se explican solos, **llevan nombre**.
- Constantes, `enum class` o valores de configuración. Un número mágico enterrado en la lógica es
  un bug esperando su turno.
- **Lo que depende del lugar donde corre va en la config**: el puerto, la cadena de conexión, las
  rutas a los archivos de datos. Ningún secreto va en el código ni en el repo.
- **Las constantes del wire** (heads, tamaños, offsets) son constantes con nombre en `Protocol/`,
  con un comentario que cita la sección del research que las fija
  (por ejemplo `// 05-protocolo § tabla de heads`).
- `0` y `1` en contextos booleanos o triviales están bien. `0x3BC`, `55960` y `5000` no.

## 4. Código reutilizable

- Antes de escribir lógica nueva, fijate si ya existe algo parecido. No dupliques.
- Si dos o más lugares necesitan lo mismo, sacalo a una función o un helper.
- Parámetros generales cuando no cuesta nada, pero sin abstraer para futuros hipotéticos.

## 5. Capas separadas

El DataServer tiene estas capas, cada una con su carpeta en `Server/DataServer/src/` y su
namespace:

| Capa | Namespace | Hace | No hace |
| --- | --- | --- | --- |
| `Core/` | `DataServer::Core` | config, log, tipos de error | nada de red, SQL ni paquetes |
| `Network/` | `DataServer::Network` | sockets, IOCP/epoll, sesiones, framing | no sabe de SQL ni de qué significa cada head |
| `Protocol/` | `DataServer::Protocol` | bytes ↔ structs de cada head | no toca sockets ni base: es puro |
| `Dispatch/` | `DataServer::Dispatch` | cola, hilo de lógica, tabla head → handler | no tiene lógica de juego |
| `Handlers/` | `DataServer::Handlers` | la lógica de cada head | no toca sockets ni escribe SQL: usa interfaces |
| `Database/` | `DataServer::Database` | interfaces de repositorio y sus implementaciones (memoria, ODBC) | no conoce paquetes |

- **Solo `main.cpp` conoce a todas** y las conecta.
- El código que depende del sistema (`#ifdef _WIN32`) vive en un solo lugar de `Network/`; el resto
  no ve `#ifdef`.
- Agrupá datos y comportamiento relacionados. No desparrames la misma lógica por archivos que no
  tienen nada que ver.
- Nombrá con precisión: si un nombre necesita un comentario para entenderse, elegí otro.

### The client's layers (2026-11-21)

The client is split into 58 modules in three roots, `engine/`, `game/` and `app/`
([spec](superpowers/specs/2026-11-20-client-architecture-design.md),
[ADR 0023](adr/0023-client-module-architecture.md)). A module uses only modules of a lower layer;
inside a layer, only the ones its `may_use` names in [`Client/modules.tsv`](../Client/modules.tsv).
No cycles between modules.

| Layer | Modules | Does | Does not |
| ---: | --- | --- | --- |
| 0 | `engine/Core` | `Expected`, `ByteReader`, the log, `IClock`, text encodings | depend on anything |
| 1 | `engine/Math`, `Crt`, `Crypto` | the original's x87 math, what mimics the MSVC 6 runtime (`rand`, sort, `sprintf`), MU's byte ciphers | touch sockets, files or game state |
| 2 | `engine/Diagnostics`, `Input`, `Audio`, `Network`, `Data` | the crash handler, input per frame, playing sounds, sockets and framing, reading the `Data\` formats | know which sounds MU has or what a packet means; see SDL |
| 3 | `engine/Platform`, `Pathfinding`, `Obfuscation` | SDL3 (window, GL context, events), the original's A*, the FoxAnti layer | see game state |
| 4 | `engine/Render` | sprite and model batches, text, the frame contracts, GL | know actors or scenes |
| 5 | `engine/Models` | skeleton, pose, animation, skinning, cloth | decide what the game draws |
| 6 | `game/Protocol`, `Tables`, `Maps` | packet layouts and writers, MU's tables, map ids and their predicates | include each other or hold behavior |
| 7 | `game/Kernel` | the `GameContext`, the shared state and the ports (send a packet, request a sound, an on-screen message) | hold feature logic |
| 8 | `game/Assets`, `Terrain`, `Sound`, `Camera`, `Ui` | the game services and the widget kit | know actors or game modules |
| 9 | the game modules (`Actors` … `FrontEnd`) | one vertical slice per feature: state, handlers, actions, windows | include another module's internals or know the session |
| 10 | `game/Session` | the connection, keepalive, the opcode table and routing | hold game logic |
| 11 | `game/Scenes` | the frame scripts (login, selection, loading, game) and the `SceneManager` | do the work: they call the modules in the original's order |
| 12 | `app` | `main.cpp`, config, command line, the loop, the composition root | |

- **Checked in CI.** `Client/tools/architecture/check_layers.py` reads the includes, takes each
  file's module from [`module-map.tsv`](../Client/docs/research/coverage/module-map.tsv) and fails
  on a new include that breaks the table, or on a file without a module. The old ones are in
  `layers-baseline.tsv` and leave it as the refactor moves each module.
- **Anatomy of a game module:** `<Module>.h` is what other modules may use, `<Module>Fwd.h` its
  forward declarations and `<Module>Defines.h` its enums and ids, with no logic. `<Module>State.h`
  holds the part of the original's globals the module owns. `In/` has one handler per incoming
  packet: a free function that receives the packet already read and the state it touches, in its
  signature. `Out/` has one action per player intent, which sends its packet.
- **Reference sizes,** for review and not for cutting blindly: a handler up to ~80 lines, a file up
  to ~400, a function up to ~60. A transcription of one of the original's `switch`es is marked and
  may stay long, with one function per long `case`.
- **Interfaces** only at the engine's edges and where a test needs one. An interface with a single
  implementation is noise. A port lives in the module that uses it (`Kernel`) and is implemented
  above it (`Session`, `Sound`, `Ui`).
- **Fan-outs are ordered lists** that `app/` assembles in the original's order. No registration at
  runtime and no static constructors: the order of calls and of `rand()` is part of the contract.

## 6. Una clase por archivo

- Cada clase o struct importante tiene su **propio archivo**, con el nombre de la clase.
- Header e implementación separados (`.h` / `.cpp`). Structs chicos muy pegados a una sola clase
  pueden quedarse en el header de esa clase.
- In a client game module, each packet handler and each action is a free function in its own file,
  in `In/` or `Out/` (§5).

## 7. Estilo consistente

- El formato lo decide [`.clang-format`](../.clang-format) (el de MuMain); corré `clang-format`
  antes de commitear y no lo discutas en la revisión. CI lo verifica en `Client/`, `labs/` y
  `Server/` (`clang-format --dry-run --Werror`, versión 18).
- Seguí el estilo del código que está alrededor. No inventes una convención nueva en un archivo.
- Includes mínimos: solo lo que el archivo usa, y todo lo que usa. Un header compila solo, con sus
  propios includes, y un archivo no cuenta con lo que le llega por el include de otro (Core
  Guidelines SF.10 y SF.11): si usa `std::vector`, incluye `<vector>`, y un `.cpp` incluye lo que
  usa aunque su header ya lo traiga. Si un header solo nombra un tipo por puntero o referencia,
  alcanza con declararlo (`class ISocket;`). De SDL3 va el header de cada parte
  (`<SDL3/SDL_video.h>`, no `<SDL3/SDL.h>`), y los tipos y constantes de GL llegan por
  `Platform/GlLoader.h`, nunca por `SDL_opengl.h` directo. No hay `stdafx.h` que incluir: los
  precompiled headers los arma CMake y los fuerza solo
  ([`cmake/PrecompiledHeaders.cmake`](../cmake/PrecompiledHeaders.cmake)), con la biblioteca
  estándar. Son para compilar más rápido, no reemplazan ningún include, y el job de Linux del CI
  compila sin ellos para que lo que falte falle.
- Lo verifica `misc-include-cleaner` de clang-tidy 18 en `Client/`: el job de Linux del CI lo
  corre y corta con cualquier hallazgo, sea un include que falta o uno que sobra. El check mira
  solo el archivo principal de cada compilación, así que los headers van aparte, de a uno. A mano
  se corre así, con el `compile_commands.json` de un build sin precompiled headers
  (`-DMURET_PRECOMPILED_HEADERS=OFF`): clang-tidy no lee el `.gch` de GCC y corta con un error.

  ```bash
  run-clang-tidy-18 -p build/linux-x64-debug -quiet -checks='-*,misc-include-cleaner' \
    'Client/(src|tests)/'
  git ls-files -z 'Client/src/*.h' 'Client/tests/*.h' \
    | xargs -0 -n 8 clang-tidy-18 -p build/linux-x64-debug -quiet -checks='-*,misc-include-cleaner'
  ```

- Si un include hace falta pero el archivo no nombra nada de él, el check lo da por sobrante. Ahí
  va `// IWYU pragma: keep`, con un comentario que diga por qué: `<SDL3/SDL_main.h>` en
  `main.cpp`, que cambia `main` en Windows, o `<compare>` para un `operator<=>` por defecto. Un
  header que reexporta otro a propósito lo marca con `// IWYU pragma: export` (`SDL_opengl.h` en
  `Platform/GlLoader.h`), y uno que siempre se incluye por lo que trae, sin nombrarlo, con
  `// IWYU pragma: always_keep` (los `*Printers.h` de los tests, cuyo `PrintTo` busca GoogleTest).
  Los headers que nadie incluye a mano, como los `bits/` de libstdc++, están en el
  `IgnoreHeaders` de `.clang-tidy`. Lo que solo declaran ellos, como los literales de `<chrono>`
  (`1ms`), el check no lo pide: ese include se pone a mano, con `keep` si el archivo no nombra nada
  más de él. Tampoco ve lo que está adentro de un `#ifdef _WIN32` o `_MSC_VER`, que en Linux no se
  compila: esos includes se revisan a mano.
- **Todo lo del código y de Git va en inglés** (Emanuel, 2026-11-21): identificadores, nombres de
  tests (`Feed`, `TEST(PacketReaderTest, CutsAC1)`), comentarios, mensajes de log y de error, ramas,
  commits y títulos y descripciones de PR (ramas, commits y títulos, con la notación Angular de
  `AGENTS.md`). Nada de comentarios en castellano. Los docs también van en
  inglés: los nuevos desde el 2026-11-21, y los de antes se traducen con el
  [plan de traducción](superpowers/plans/2026-11-21-docs-to-english.md).
- `.editorconfig` fija espacios, LF y UTF-8. `.clang-tidy` es el de MuMain, con las carpetas de
  MuRet y `misc-include-cleaner`. CI corre solo el de los includes. El resto todavía tiene
  hallazgos en `Client/` y se corre a mano:
  `run-clang-tidy -p build/linux-x64-debug -quiet 'Client/(src|tests)/'`.

### Nombres (decidido el 2026-11-02, alineado con el código nuevo de MuMain)

Son las costumbres del código nuevo de MuMain (el de los namespaces `<Capa>::<Concepto>`, no el
heredado con `C*`, notación húngara y `g_`). Sus reglas escritas no las nombran: salen de ese
código (`m_socket` en `Network/Reconnect/ReachabilityProbe.h`, `ICamera`, `s_installed`). Valen
para todo el código nuevo y para `Client/` y `labs/cl2.0-flujos/` enteros. `Server/` y
`labs/0.0-enlace/` se alinean cuando se toca cada archivo. Eso es de MuRet, no de MuMain: allá el
código viejo sigue con las costumbres de su módulo hasta una modernización aparte, "not
piecemeal", porque es mucho; acá son dos archivos, y alinearlos al tocarlos no mezcla estilos.

- Tipos, funciones, métodos y namespaces en `PascalCase`: `PacketReader`, `PacketReader::Feed`,
  `Client::Network`.
- Variables, parámetros y locales en `camelCase`: `firstWireByte`, `declaredSize`.
- Miembros de datos privados y protegidos con `m_` y `camelCase`: `m_buffer`, `m_consumed`. Así se
  distingue el estado del objeto de un local al leer un método, y el parámetro de un constructor
  puede tener el nombre natural sin chocar con `-Wshadow` (`cmake/CompilerWarnings.cmake`):
  `ByteReader(std::span<const std::byte> buffer) : m_buffer(buffer)`. Vale también para los
  fixtures de GoogleTest (`m_connections`).
- Los campos públicos de un `struct` sin invariantes van en `camelCase`, sin prefijo:
  `LoadedItemTable::table`, como `Rgb::red` en MuMain.
- Las interfaces (clases con solo funciones virtuales puras) llevan `I` adelante: `IObfuscation`,
  en `IObfuscation.h`. Sus implementaciones no: `IdentityObfuscation`, `FoxAntiObfuscation`.
- Las variables estáticas que no son `const`, de un archivo (en un namespace anónimo) o de una
  función (`static bool s_initialized` adentro de una función, como en MuMain), llevan `s_`. Hoy no
  hay ninguna.
- Constantes y enumeradores en `PascalCase`, con `constexpr` y no `#define`: `MaxPacketSize`,
  `PacketType::C3`.

### Cómo se arma un archivo

- Un header empieza con `#pragma once`, después van los includes y recién después el comentario
  del archivo, pegado a lo primero que declara (el namespace o la clase), como
  `Network/Reconnect/ReachabilityProbe.h` en MuMain:

  ```cpp
  #pragma once

  #include "Network/Packet.h"

  #include <cstddef>

  // Splits a TCP stream into C1/C2/C3/C4 packets.
  namespace Client::Network
  {
  class PacketReader
  ```

- Un `.cpp` empieza con su propio header; si tiene un comentario de archivo, va arriba de lo que
  describe, después de los includes.
- Sin línea en blanco después de la llave de `namespace X {` ni antes de `} // namespace X`.

### Tests y CMake (como MuMain)

- Un archivo de test se llama `test_<snake_case>.cpp`, por lo que prueba, y va en la carpeta de su
  módulo: `Client/tests/network/test_packet_reader.cpp`, `test_golden_item_table.cpp`. Seguimos con
  GoogleTest (D-C9), y la suite lleva el nombre de la clase: `TEST(PacketReaderTest, CutsAC1)`.
  Así `ctest -R PacketReader` sigue filtrando como antes.
- Un ejecutable por archivo de test, como `mu_add_test` en MuMain: `muret_add_test` en
  `Client/tests/CMakeLists.txt` lo arma, lo enlaza con `ClientLib`, `ClientTestSupport` (los `.cpp`
  de `tests/support/` que no son tests) y GoogleTest, le pone los warnings y registra cada `TEST` en
  CTest. Cada `TEST` tiene un tope de tiempo (`MURET_TEST_CASE_TIMEOUT`), como el `TIMEOUT` de
  `mu_add_test`. Cada módulo de `tests/` tiene su `CMakeLists.txt`.
- Los fuentes se juntan con un glob y `CONFIGURE_DEPENDS`: `ClientLib` toma todo `src/` con
  `file(GLOB_RECURSE)`, menos `main.cpp` (y, desde la CL3.1, `src/Platform/`, que es de
  `ClientPlatform`, sacado con `list(FILTER ... EXCLUDE REGEX)` como hace MuMain), y cada módulo de
  tests sus `test_*.cpp` con `file(GLOB)`. Un archivo nuevo entra al reconfigurar, sin tocar el
  CMake. Es una diferencia elegida: MuMain lista cada `mu_add_test` a mano. El glob de un módulo
  no entra en subcarpetas, y una carpeta de tests nueva necesita su `CMakeLists.txt` y su
  `add_subdirectory`; si un `test_*.cpp` queda sin ejecutable, el configure se corta y lo nombra.
- Excepción, los labs (`labs/<ola>/`): sus archivos también se llaman `test_<snake_case>.cpp`,
  pero cada lab es **un** ejecutable (`LabCl20Tests`) con su lista a mano, porque el lab es la
  unidad que se aprende y se corre entera.

### Namespaces

1. Siempre con el componente adelante, para no chocar cuando entren otros servidores:
   `Client::<Capa>[::<Concepto>]`, `DataServer::<Capa>[::<Concepto>]`. MuMain no lo usa porque
   tiene un solo componente. La jerarquía va **por dominio**: lo de gremios va en
   `DataServer::Handlers::Guild`, lo arme quien lo arme.
2. Los helpers privados de un archivo van en un namespace anónimo.
3. Nada de `using namespace` en un header.

## 8. Sin fugas ni estado colgado

- Todo recurso tiene un dueño claro y un camino claro de liberación.
- **RAII siempre**: sockets, handles de ODBC, hilos y memoria se liberan en un destructor.
  `std::unique_ptr` (o `std::shared_ptr` cuando la propiedad es compartida de verdad) en vez de
  `new`/`delete` crudos.
- No dejes recursos abiertos en los caminos de salida temprana.
- **La vida de un buffer en vuelo:** un buffer o un `OVERLAPPED` entregado al sistema le pertenece
  hasta la completion; no se libera ni se reusa antes (es C-3 del research).
- **Estado entre hilos:** cada dato tiene un hilo dueño, o una sincronización explícita y
  documentada. El estado del servidor lo toca solo el hilo de lógica.

## 9. Legible antes que ingenioso

- Escribí código obvio de leer, aunque la versión "ingeniosa" sea más corta.
- Las expresiones complejas se parten en variables intermedias con nombre.
- **Un comentario va solo si es estrictamente necesario** (Emanuel, 2026-11-21); si no, no va. Si
  un bloque necesita un comentario para explicar **qué** hace, refactorizalo para que no lo
  necesite. Lo que queda va en inglés y corto, en una línea si se puede. Son tres casos:
  - la cita al original (`// 0x4d3200`, a lo sumo con una etiqueta corta), porque el rastreo cuenta
    las direcciones citadas en `Client/src` (`build_roadmap_matrix.py`, la referencia de
    cobertura);
  - la sección del research que fija una constante (§3);
  - un porqué que el código no puede decir: un quirk del original que se copia a propósito, un
    orden que no se puede cambiar.
- Nada de comentarios que repiten el código, bloques de prosa ni código comentado.

## 10. Pensar antes de agregar

- Cada dependencia, abstracción o patrón nuevo tiene un costo de mantenimiento: justificalo. Las
  dependencias entran por `vcpkg.json` y, si son de peso, con un ADR.
- **No reconstruir la rueda.** Antes de escribir algo que no sea propio del MU, buscá una biblioteca
  en vcpkg y leé su documentación (Context7). Lo propio del MU son sus formatos, sus checksums y sus
  cifrados; si uno de ellos combina primitivas estándar (SHA-1, AES), lo propio es la combinación y
  las primitivas se buscan.
  - La biblioteca entra solo si resuelve **ese** problema y lo hace como se espera. Una que hace
    algo parecido no alcanza (con el código propio es al revés: regla 4).
  - Lo que se espera lo fija el research: la salida del original, salvo lo que MuRet corrige a
    propósito y deja escrito. Lo prueban los tests golden, corridos con la carpeta de datos porque
    CI no la tiene, y vectores chicos que sí corren en CI. Si una diferencia chica se acepta, lo
    decide el roadmap (para JPEG, su pregunta 1).
  - Se escribe a mano igual si el roadmap o el camino ya lo decidieron (D-3, D-5, D-C2; el
    `Expected` es la excepción de L-3 porque C++20 no lo trae), o si son pocas líneas y la
    dependencia cuesta más que ellas. Reabrir una de esas decisiones es un PR a esa decisión. Un
    ejercicio del camino va en `labs/` y no alcanza para mantener una copia casera.
  - Si la salida depende de la versión de la biblioteca, subirla es un PR propio que corre los
    golden.
  - El PR que suma una dependencia, o que escribe a mano algo que no es del MU, dice qué
    bibliotecas se miraron y por qué entraron o no.
- **Una sub-ola = un concepto = un PR chico.** Los diffs chicos se revisan mejor.
- Si dudás de una decisión estructural, preguntá antes de construir encima.

## 11. Documentar el uso, no las tripas

- Lo nuevo o cambiado se documenta al nivel de **uso**: cómo se compila, se corre y se configura
  (`Server/DataServer/README.md`).
  - Fuera de alcance: firmas, diagramas de clases, flujo interno. El código es la fuente de verdad
    de eso.
- La excepción son **las reglas del protocolo y los cálculos no obvios**: qué responde cada head,
  escalas de unidades, el cifrado del nombre. El comportamiento del original está en el
  [research](../Server/DataServer/docs/research/README.md); citá su sección desde el código en vez
  de copiarla.
- **Cuando MuRet hace algo distinto del original** (un quirk arreglado, un cambio de wire), se
  escribe un ADR en `docs/adr/` con el contraste: qué hacía el original, qué hace MuRet y por qué.
- Si no le sirve a quien opera el servidor o a quien busca una regla del protocolo, no lo escribas.

## 12. Rendimiento: con criterio en los caminos calientes

- No micro-optimices los caminos fríos (arranque, carga de config y de tablas): ahí gana la
  claridad.
- **Sí tené cuidado en los caminos calientes.** En un servidor son lo que corre **por cada paquete
  o por cada mensaje**: el parser, el despacho, el envío, el log.
- En un camino caliente, evitá:
  - asignaciones de memoria por paquete cuando hay una alternativa simple (un buffer que se reusa);
  - I/O dentro del bucle del hilo de lógica que no sea la consulta que el handler necesita (por
    ejemplo, loguear cada mensaje a disco en nivel `info`);
  - recorridos O(N²) sobre sesiones cuando hay uno O(N) directo.
- **Medí antes de optimizar** (las sub-olas 2.3 y 4.3 son justamente eso). "Caliente" se juzga por
  frecuencia de llamada, no por lo importante que parece el código.

## 13. Cada comportamiento, un test (propia de MuRet)

- Lo que el research fija (un layout, una respuesta, un quirk) tiene un test que lo demuestra.
- Un quirk que se arregla lleva un test que documenta el comportamiento viejo y el nuevo
  ([08-quirks-y-bugs](../Server/DataServer/docs/research/08-quirks-y-bugs.md)).
- Primero el test en rojo, después el código. No se saltea, desactiva ni pone en cuarentena un
  test para que CI quede verde.

## 14. Addresses, offsets and citations (MuRet's own, 2026-11-21)

- **Citations to the original stay in comments,** on one line: the address and at most a short
  label (`// 0x4d3200`). Tracking counts them (`build_roadmap_matrix.py`, the coverage reference),
  so a move or a comment cleanup never drops one.
- **A layout that matters carries its offsets in the type.** A packet is described with
  `Field<T, Offset, Endian>` and a `static_assert` on its size (from AR2.1; until then its offsets
  are named constants in `Protocol/`, §3). A `Data\` format gets `static_assert(offsetof(...))` and
  `static_assert(sizeof(...))`.
- **In-memory state does not copy the original's layout,** so a `static_assert` proves nothing
  there. A global named by its address (`dword723bd8`) gets a name that says what it is; the
  address stays in its comment and, from AR3.1, in `globals.tsv`.
- **Contiguous data is not split.** A struct that copies contiguous memory of the original because
  an overflow, a copy or a reset crosses it stays whole, marked with
  `// Contiguous in the original: …` and its row in the 07 of the research.
