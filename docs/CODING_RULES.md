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
- A number whose meaning is fixed outside the file (an opcode, an item code, a map, a sound, a `Text.bmd` line) is
  named once, in its space's home: §7, "Named values".
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
| 3 | `engine/Platform`, `Pathfinding`, `Network/XorRemap`, `Audio/Sdl` | the window's contracts without SDL, the original's A*, the XOR and remap layer, the SDL3_mixer backend | see game state |
| 4 | `engine/Render`, `Platform/Sdl` | sprite and model batches, text, the frame contracts; SDL3 (window, GL context, events) | know actors or scenes |
| 5 | `engine/Models`, `Render/Gl` | skeleton, pose, animation, skinning, cloth; the OpenGL backend | decide what the game draws |
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

### The engine's libraries (2026-11-25)

The engine builds as four shared libraries, cut by what the code sees
([ADR 0028](adr/0028-shared-library-modules.md)): `Engine` sees neither SDL nor OpenGL, `Platform`
(`engine/Platform/Sdl/`) sees SDL, `OpenGL` (`engine/Render/Gl/`) sees OpenGL and `Audio`
(`engine/Audio/Sdl/`) sees SDL3_mixer. An engine module is a folder of its library, and a new file
joins by its folder. The game and the app are `ClientLib`, static until AR4. The client's binaries
go to `build/<preset>/bin/`.

- **The macro.** A class or a free function that a library declares in a header, defines in a
  `.cpp` and another binary uses (the game, the app, another library, a test) carries its library's
  macro, from the generated `<Library>Api.h`: `class ENGINE_API ByteReader`,
  `[[nodiscard]] ENGINE_API float RoundedSin(float x);`. What lives whole in a header carries none,
  because it has no symbol to export: a template, an inline function, a struct without functions in
  a `.cpp`, an interface with only pure functions. The libraries build with hidden visibility and
  `-Wl,--no-undefined`, so a missing macro is a link error on Linux as on Windows.
- **A class with the macro defines all its special members.** MSVC defines them in every file that
  sees the class, the implicit ones too. A class that holds a container of move-only values (a
  `std::vector<std::unique_ptr<T>>`) declares its copy deleted, and a class with a
  `std::unique_ptr` to an incomplete type defines its special members in the `.cpp`.
- **No mutable state defined in a header:** no `inline` variable, no `static` member or local of a
  function defined there. Each library would hold its own copy. It lives in a `.cpp`, behind a
  function with the macro.
- **An outside library has one owner, or it is shared.** libjpeg-turbo, FreeType and LLVM-libc are
  private to `Engine` and SDL3_mixer to `Audio`. SDL3 has two users, `Platform` and `Audio`, and one
  state, so it is a shared library on both systems; on Linux the triplet
  `cmake/triplets/x64-linux-shared-sdl.cmake` builds that port alone shared. Who links `Platform`
  sees SDL's headers, and only who calls SDL links it. CI checks who defines each one.
- **One binary:** `-DMU_MONOLITHIC=ON` builds the four libraries static and the macros expand to
  nothing. CI builds that shape in its Release job.

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
  `Platform/Sdl/GlLoader.h`, nunca por `SDL_opengl.h` directo. No hay `stdafx.h` que incluir: los
  precompiled headers los arma CMake y los fuerza solo
  ([`cmake/PrecompiledHeaders.cmake`](../cmake/PrecompiledHeaders.cmake)), con la biblioteca
  estándar. Son para compilar más rápido, no reemplazan ningún include, y el job de Linux del CI
  compila sin ellos para que lo que falte falle.
- **No common header.** There is no `Common.h`, `Define.h` or umbrella header that gathers other
  headers so files can include one line (cMaNGOS's `Common.h` has 27 includes and 259 users, and 482
  of its 511 scripts include only `sc_common.h`). It hides what each file uses, which
  `check_layers.py` and the module refactor need to see, and touching it recompiles everything;
  speed is the precompiled header's job. What several files share goes into a small header with one
  topic: `<Module>Fwd.h`, `<Module>Defines.h`, the ids of a map or event (`<Map>.h`, like
  TrinityCore's `molten_core.h`), or one header of `engine/Core` or `engine/Math`. clangd adds the
  include when a symbol is completed (`--header-insertion=iwyu`, its default).
- Lo verifica `misc-include-cleaner` de clang-tidy 18 en `Client/`: el job de Linux del CI lo
  corre y corta con cualquier hallazgo, sea un include que falta o uno que sobra. El check mira
  solo el archivo principal de cada compilación, así que los headers van aparte, de a uno. A mano
  se corre así, con el `compile_commands.json` de un build sin precompiled headers
  (`-DMU_PRECOMPILED_HEADERS=OFF`): clang-tidy no lee el `.gch` de GCC y corta con un error.

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
  `Platform/Sdl/GlLoader.h`), y uno que siempre se incluye por lo que trae, sin nombrarlo, con
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
  en `IObfuscation.h`. Sus implementaciones no: `IdentityObfuscation`, `XorRemapObfuscation`.
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
- Un ejecutable por archivo de test, como en MuMain: `mu_add_test` en
  `Client/tests/CMakeLists.txt` lo arma, lo enlaza con GoogleTest, le pone los warnings y registra
  cada `TEST` en CTest. A test links what its includes reach: `Engine` and `EngineTestSupport` (the
  `.cpp` files of `tests/support/` that are not tests and see only the engine) always, and
  `ClientLib` and `ClientTestSupport` only when the file, or a header of `tests/` it includes,
  includes a header of the game or the app. A test of the engine cannot include one: it does not
  see their folders. Cada `TEST` tiene un tope de tiempo (`MU_TEST_CASE_TIMEOUT`), como el `TIMEOUT` de
  `mu_add_test` en MuMain. Cada módulo de `tests/` tiene su `CMakeLists.txt`.
- Los fuentes se juntan con un glob y `CONFIGURE_DEPENDS`: each engine library takes its folders of
  `src/engine/` with `file(GLOB_RECURSE)`, `ClientLib` takes the rest of `src/` except `main.cpp`
  (`list(FILTER ... EXCLUDE REGEX)`, como hace MuMain), y cada módulo de tests sus `test_*.cpp` con
  `file(GLOB)`. Un archivo nuevo entra al reconfigurar, sin tocar el
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
4. In the client's `engine/` and `game/` roots the namespace is the folder: a file under `engine/Render/Frame/` declares
   `Client::Render::Frame`, and one under `engine/Platform/Sdl/`, `Client::Platform::Sdl`. Two exceptions: a folder that
   exists only for its build flags keeps its parent's namespace (`engine/Math/RoundedMath/` is `Client::Math`), and a
   `.cpp` that defines the members of another module's class stays in that class's namespace.

### Types and aliases (2026-11-23)

A long type written again and again, an id passed as a bare integer and a loose `true` at a call make the reader stop.
These rules name things without changing what the original computes: an alias is the same type, and a new type goes
only where values must not be mixed. The principles hold across MuRet; the `Client::Core` names and the `In/` and
`Out/` folders are the client's.

1. **`using`, never `typedef`.** An alias names a role, not a shape: `PacketSender`, not `OutgoingFunction`.
2. **One home.** An alias that several files use lives in the header that owns the concept, next to its enum, constant
   or table, never in a common header. A `.cpp` does not redefine an alias that a header could give it.
3. **Results.** `Client::Core::Result<T>` and `Client::Core::Status` are reserved for errors that are a `std::string`
   message (`Core::Expected<T, std::string>` and `Core::Expected<void, std::string>`) and never mean anything else. A
   domain whose callers act on the error keeps its own type: an error enum (`ProtocolError`), a structured result
   (`SocketApi::Result`) or a system error. An error enum used about 20 times or more (a guide, not a threshold) gets
   its own alias next to it (`ProtocolResult<T>` beside `ProtocolError`).
4. **An alias protects nothing.** An alias of a primitive (`using SkillId = std::uint8_t`) separates nothing and hides
   the width the reader needs for the deliberate wraparounds (`static_cast<std::uint8_t>(a + b)`). Two values that must
   not be mixed, such as an actor key and a pool slot, need a type of their own. An alias of a tagged type
   (`using SoundId = Core::Id<struct SoundIdTag, std::int32_t>`) keeps that type and is fine.
5. **Ids.**
   - A closed set fixed by the game (the character class, the scene, the packet kind) is an `enum class` whose
     underlying type is its stored width. A value from outside becomes one through a single function that says what
     an unknown value does, as `PacketTypeFromByte` does.
   - An open space (sound, model, texture) is `Core::Id<Tag, Rep>`. It separates domains and nothing more: every value
     the original accepts is still accepted, and whether some value means "none" is the domain's decision (texture
     slot 0 is a slot, world object identity 0 is none).
   - An open id that is switched on or passed by value as a template argument (map, opcode) is a scoped enum without
     enumerators, `enum class X : Rep {}`, with `inline constexpr` constants. It holds any value of `Rep`, its
     constants are not all its values, and a `switch` over it gets no warning for a missing case. A `Core::Id` type can
     be a template type argument, but its private member keeps a `Core::Id` value from being a template argument.
   - A typed id is built with braces (`ModelId{0x20a}`), which reject a narrowing conversion, and unwrapped with
     `Value()` or one named function, at the place where the cast was.
   - The engine never names a game id. The game converts to the engine's representation on its side.
6. **Fidelity sets the representation.** Where the original fixes the width, the sign, the byte order, a promotion, a
   wraparound or the x87 rounding, the value stays the primitive the port reproduces, and the name carries the unit
   (`delayMs`, `angleDegrees`): the port spells the x87 intermediates by hand (`F32`, `D`, `Truncate` and `Bits`
   from `Math/X87.h`), and a unit type would choose them by itself. Elsewhere a vocabulary type is fine, such as `Core::Duration` (`std::chrono::nanoseconds`) for
   the loop's time. Packet and file layouts keep their fixed-width integers. Each conversion happens once, at its
   boundary (reading or writing a packet, a file, an engine-facing API; in the client, a module's `In/` and `Out/`),
   with the operation the original does there: a sign extension, a truncation, the low byte. Never by reinterpreting
   bytes.
7. **No alias** for a type used once, for one that only hides a `const`, a reference or the ownership
   (`std::unique_ptr`, `&`, `*`, `std::reference_wrapper`), or for a standard type that is already vocabulary
   (`std::optional<T>`, `std::function<void()>`). A named deleter or a callback role hides nothing: it says what the
   type is for.
8. **Flags, by meaning.** Exclusive modes are an `enum class` (`StopAt::Start`). Independent settings are an options
   struct filled with designated initializers (`{.effects = false, .music = false}`), whose defaults are the defaults
   the parameters had. A clear predicate or a setter keeps its `bool`. A literal `true` or `false` far from its
   declaration is the sign that a call needs one of the first two.
9. **`[[nodiscard]]`.** `Core::Expected` is a `[[nodiscard]]` class, so a function that returns it by value does not
   repeat the attribute. A function that returns a pointer or a reference, or that needs its own message, keeps it.

`Core::Result` and `Core::Status` live in `Core/Expected.h` and `Core::Id` in `Core/Id.h` (AR1.1's common kit). Existing
code adopts these rules module by module in the refactor's design PRs, never in a move or comments PR.

### Named values (2026-11-23)

A value whose numbering is fixed outside the file that uses it (by the wire, a `Data\` file, a load order or an
enumeration of the original) belongs to a space. Today the same number is named again in each file that needs it,
often under another name (`0xf3` is defined six times under three names; map 23 is `LastChaosCastleMap` 25 times).
These rules give each space one place to look without changing what the original computes. The homes below are the
client's.

1. **One home per space, never one home for all.** Opcodes and subcodes in one list per server in `game/Protocol`, as
   TrinityCore and cMaNGOS keep theirs; packet layouts in `game/Protocol/<Module>/`; table rows and their keys (item
   codes and item models, skills, the text lines several modules print) in `game/Tables`; maps in `game/Maps`; sounds
   in `game/Sound`; the other models and the textures in `game/Assets`; the numbers the x87 code shares in
   `engine/Math`. No `Constants.h`, `Offsets.h` or `Ids.h` across spaces.
2. **Identity once, role anywhere.** The home names what the value is (`Models::HornOfDinorant`). A file names what it
   means there (`HeightOffsetPet`) with its own type and the home's name as its initializer, never the literal again.
   One file may keep an identity private; the second file that needs it moves it home. A closed set, an opcode and a
   group bound are born in their home.
3. **Two spaces, two names.** A number that means two things has a name in each space (`0x1c15` is an item code and an
   item model), and going from one to the other is a named function (`ModelOf`).
4. **The site keeps its shape.** The comparison, its operator, its cast and its citation stay; the local name keeps the
   type the literal had there. A predicate replaces a comparison only where the original's code at that address
   matches it. A shared name is not a shared identity (`FirstEventMap` is 18 in `GameCharacters.cpp` and 24 in
   `InventoryMoves.cpp`), and two meanings of one value are not merged.
5. **A layout carries its offsets** (§14). An offset constant outside its layout is a missing accessor.
6. **What stays local:** one window's geometry and colors, one effect's parameters (also as float bit patterns), one
   model's bones and actions, a sentinel beside its record. An address of the original is a comment, and from AR3.1 a
   row of `globals.tsv`.
7. **The engine names no game value**: it receives it as a parameter or in a table the game fills.

`Client/tools/architecture/named-values.tsv` lists the spaces and their homes, and `check_named_values.py` checks
rules 1, 2 and 7 in CI with a baseline that only shrinks; both arrive in AR1.1, and a space whose home does not exist
yet is reported, not failed, until the PR that creates its home. Existing code adopts these rules in the refactor's
design PRs, never in a move or comments PR. The plan is
[`2026-11-23-named-values.md`](superpowers/plans/2026-11-23-named-values.md).

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

### The standard library in the client (2026-11-21)

The client keeps the C++ standard library for its containers and vocabulary types
([ADR 0026](adr/0026-keep-the-standard-library.md)). Measured on `main` `bfaad8b`, its heaviest
scene costs 2.5 to 3.2 ms of CPU of a 40 ms frame at `-O2`, and EASTL ties with `std` there.

- **No container library replaces `std`.** A new one (EASTL, Abseil, folly, Boost.Container, a
  hash map) comes in only with a measurement that shows the problem in the client, under the rules
  above. Even then, in this order: fix the usage pattern (`reserve`, a reused buffer, a
  `std::span`); a `std::array` with a count, or a small `StaticVector` in `Core`;
  `ankerl::unordered_dense` for one hot hash table; Boost.Container if several special containers
  are needed; `std::flat_map` and `std::inplace_vector` once the CI compilers ship them.
- **Sorting.** What the original sorts with its `std::sort` goes through `Crt::Msvc6Sort`,
  because the order of equal elements reaches the screen. `std::sort` and `std::ranges::sort` only
  when no two elements compare equal or equal ones cannot be told apart (integers, unique slots).
  `std::stable_sort` when the original keeps equal elements in arrival order.
- **Random numbers.** What the original draws with `rand()` goes through `Crt::CrtRandom`
  (`Crt/CrtRandom.h`), in the original's order. `std::mt19937` and `std::minstd_rand` are fixed by the standard and may serve
  MuRet's own draws, using their raw output. Never `std::rand`, `std::shuffle`, `std::sample` or the
  `<random>` distributions (`std::uniform_int_distribution` and the rest): the standard does not fix
  their results, and GCC and MSVC give different ones.
- **Hash tables.** Nothing the player sees, and no order of calls, depends on the iteration order
  of a `std::unordered_map` or `std::unordered_set`, or on the value of `std::hash`: both change
  between standard libraries and versions. A table that is walked is a `std::map`, a sorted
  `std::vector` or a container that keeps insertion order.
- **Text.** A text the original builds with `sprintf` or `wsprintfA` from a `Text.bmd` format goes
  through `UI::SprintfFormat` or `UI::OriginalFormat`: only they reproduce the CRT's flags, widths
  and what happens when the format and its arguments do not match. `std::format` is for logs,
  errors and MuRet's own fixed formats whose output equals the CRT's (`{}` of an integer or a
  text). A floating-point value the player sees is never printed with `std::format`: `{}` gives the
  shortest representation, and `%f` does not.
- **Math.** IEEE 754 fixes `+`, `-`, `*`, `/` and the square root. Each C library rounds the sine,
  the cosine, the logarithm and their kind in its own way, and glm calls them inside `glm::rotate`,
  `glm::perspective`, `glm::slerp`, `glm::angleAxis` and `glm::quat(vec3)`: the same frame gave
  other pixels on Windows and on Linux. The client takes them correctly rounded
  ([ADR 0027](adr/0027-correctly-rounded-math.md)): `RoundedSin`, `RoundedCos`, `RoundedTan`,
  `RoundedAcos` and `RoundedLog10` from `Math/RoundedMath.h`, `RoundedRotate` and
  `RoundedPerspective` from `RoundedTransforms.h`. Never `std::sin`, `std::cos`, `std::atan2`,
  `std::exp`, `std::log`, `std::pow` or their kind, with or without `std::`, in `Client/src` or
  `Client/tests`. A function the module lacks is added to it, with its MPFR hash. `std::sqrt`,
  `std::fabs`, `std::floor`, `std::fmod` and `std::ldexp` give the same bits everywhere and stay.
  `Client/tools/architecture/check_rounded_math.py` checks the names in CI; the one call it lets
  through is in `rounded-math-exceptions.tsv`, with its reason.

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

### The client's frame (2026-11-21)

In the client, the hot path is what runs every frame: the 25 steps per second and the drawing of
each frame ([ADR 0026](adr/0026-keep-the-standard-library.md)).

- **No allocation per mesh, per actor or per draw.** Code that runs every frame does not create a
  container for each mesh, actor or draw, and does not build a new `std::function`. It reuses a
  buffer that outlives the frame (cleared, not freed), or a `std::array` with a count where the
  original has a fixed array; what happens past that array's end follows the research. A callback
  that does not change between frames is built once, or replaced by an interface its caller keeps.
- **Today's exceptions are scheduled.** With 40 monsters on screen, the skinning
  (`Models/Skinning.cpp`) and the character drawing (`Scenes/CharacterDrawing.cpp`) create
  about 2,090 temporary `std::vector` per frame, and `GamePanels` rebuilds about 85
  `std::function`. The refactor removes them (AR4.1 and AR4.5 of the
  [plan](superpowers/plans/2026-11-20-client-architecture-refactor.md)) with the behavior golden
  identical. New code does not add more.
- **Measure an optimized build.** At `-O2`, `malloc` and `free` cost 0.15 ms of that frame. At
  `-O0` the frame is about 15 times slower because nothing is inlined, so a profile of a `-O0`
  build points at the standard library instead of the code: profile `-O2`, or `-Og` to debug.

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
