# Library review: hand-written code a library could replace

**Date:** 2026-11-25. **Base reviewed:** `main` at `5bec734` (#459). **Status:** report and backlog. No item has
started: each one starts when Emanuel says so, under [CODING_RULES §10](../CODING_RULES.md#10-pensar-antes-de-agregar).

Emanuel asked on 2026-11-25 for an exhaustive pass over the project, looking for hand-written code that a popular or
constantly maintained library already does, or does in a very similar way, so MuRet does not reinvent the wheel. He
asked for the findings as backlog, not as changes.

## Scope and method

- **Reviewed:** `Client/src/` (1,452 files, about 270,000 lines), `Client/tests/` (support code, fakes, printers,
  goldens), `Client/tools/`, the CI workflows, `cmake/`, `vcpkg.json` and, by their imports, the research scripts in
  `Client/docs/research/scripts/`. **Not reviewed:** the DataServer (paused since 2026-11-08), `labs/` (exercises of
  the learning path, which rule 10 says never justify a home-made copy) and `third_party/llvm-libc` (vendored by
  [ADR 0027](../adr/0027-correctly-rounded-math.md)).
- **Two kinds of code.** Each candidate was classified by what the code it would replace answers to:
  - **Fidelity:** the code reproduces a function of `main.exe`, and research, ASM citations and golden tests pin its
    output. A library enters only if it gives the same bytes (rule 10), which rules out almost all of them.
  - **Infrastructure:** MuRet's own code (tests, tools, config, logs). A library can replace it if it does the same
    job and every test stays the same.
- **Lookups, 2026-11-25:** ports and versions in the vcpkg baseline the repo pins (`9a5dfef`), stars and last push from
  GitHub search, licenses from the port or the repository, documentation through Context7 (CLI11, magic_enum,
  clang-tidy), Python packages on PyPI. A library counts as popular or maintained when it has thousands of stars or is
  the standard of its field, and commits in the last months.
- Many files already say which libraries were considered when they were written, as rule 10 asks. Those decisions were
  checked against today's ports and are listed under [Checked and kept](#checked-and-kept), not reopened.

## The result in short

Most of the client is a port of `main.exe` whose bytes are the contract, and no library can take that part. The
infrastructure already uses a library wherever one fits: SDL3, SDL3_mixer, FreeType, libjpeg-turbo, glm, CLI11,
GoogleTest and LLVM-libc. No library should come in now. The review leaves six backlog items, all in tests, tools or
infrastructure, and none touches the fidelity of the port:

| ID | What | Library | Recommendation | Where in [plan #400](../superpowers/plans/2026-11-20-client-architecture-refactor.md) |
| --- | --- | --- | --- | --- |
| [LIB-1](#lib-1-gmock-for-the-spy-test-doubles) | hand-written spy test doubles | gMock (already in the `gtest` port) | later | AR6, next to the shared test fakes |
| [LIB-2](#lib-2-magic_enum-for-the-googletest-printers) | GoogleTest printers for enums | magic_enum | later | AR6, with LIB-1 |
| [LIB-3](#lib-3-clang-tidy-instead-of-most-of-check_rounded_mathpy) | `check_rounded_math.py` | clang-tidy `bugprone-unsafe-functions` | later, with clang-tidy 20 or newer in CI | AR7 |
| [LIB-4](#lib-4-tree-sitter-for-the-architecture-checks) | regex C++ parsing in the architecture checks | tree-sitter | later, at the first regex miss | AR7 |
| [LIB-5](#lib-5-stdexpected-under-coreexpected) | `Core::Expected` | `std::expected` (C++23) | later, low priority, its own decision | after AR7 |
| [LIB-6](#lib-6-fontconfig-for-the-font-lookup-on-linux) | font lookup on Linux | fontconfig | optional | `engine/Platform`, after AR1.2 |

One more is already in the plan: AR2 closes with a review of whether packet layouts need a generator, and if they do,
Kaitai Struct is the first to compare ([LIB-7](#lib-7-a-packet-layout-generator-if-ar2-asks-for-one)).

## Backlog

### LIB-1. gMock for the spy test doubles

- **MuRet code:** 34 hand-written fakes and spies in `Client/tests`. The spies repeat: a recording
  `Scenes::IGameUiRequests` is written six times (24 to 74 lines each, in `test_game_social_steps`,
  `test_game_dialog_steps`, `test_personal_shop_window`, `test_game_party_window`, `test_quest_window` and
  `test_game_letters`), and `RecordingPlayback` of `Audio::IPlayback` four times. No test uses gMock today.
- **Library:** gMock, part of GoogleTest (google/googletest: 39.8k stars, pushed 2026-11-17, BSD-3-Clause). The
  baseline's `gtest` port (1.18.0) already builds it with `BUILD_GMOCK=ON`; the tests link only `GTest::gtest`. No new
  dependency.
- **Fidelity:** none, test code.
- **Gain:** a spy becomes one `MOCK_METHOD` per call, and the test states what it expects with `EXPECT_CALL` instead of
  reading a recording class copied into each file.
- **Risk:** two styles of test double side by side, and gMock's matchers and strict or naggy mocks to learn; the test
  executables compile a little slower. The behavioral fakes (`FakeSocket`, `FakeClock`, `FakeServers`,
  `FakeTextureStore`, `LoopbackServer`) simulate a system and stay hand-written: gMock does not do that better.
- **Recommendation:** later, in AR6, next to the shared test fakes the plan already lists. Try it first on
  `IGameUiRequests` and its six copies, compare one test before and after, and adopt it for spies only if the tests
  read better.

### LIB-2. magic_enum for the GoogleTest printers

- **MuRet code:** `Client/tests/support/*Printers.h`, six headers, 515 lines. Of their 23 `PrintTo`, 16 are a `switch`
  that writes each enumerator's name, plus a fallback for a value outside the enum. A new enumerator needs its line by
  hand; without it the test message shows the fallback.
- **Library:** magic_enum (Neargye/magic_enum: 6.3k stars, pushed 2026-11-24, MIT), header-only, port `magic-enum`
  0.9.8 in the baseline. `magic_enum::enum_name(value)` gives the name, or an empty string for a value it does not know.
- **Fidelity:** none, test code. `Client/src` keeps its own texts: the ones it shows or logs (`"On"`, `"Adaptive"`,
  `"Off"`) are words of the config, not identifiers.
- **Gain:** one generic `PrintTo` per namespace (GoogleTest finds it by ADL) instead of 16 switches, and a new
  enumerator prints without touching the printers.
- **Risk:** magic_enum reads the names from the compiler's function signatures, only within `MAGIC_ENUM_RANGE_MIN` and
  `MAGIC_ENUM_RANGE_MAX` (−128 to 127 by default): an enum with values outside needs
  `magic_enum::customize::enum_range`, or it prints the fallback. It is one more test dependency, with some compile
  time.
- **Recommendation:** later, in AR6, together with LIB-1.

### LIB-3. clang-tidy instead of most of `check_rounded_math.py`

- **MuRet code:** `Client/tools/architecture/check_rounded_math.py` (112 lines and 153 of tests) finds the calls that
  [ADR 0027](../adr/0027-correctly-rounded-math.md) forbids (`std::sin`, `glm::rotate` and the rest) with regular
  expressions over the text, after blanking comments and literals.
- **Library:** clang-tidy's `bugprone-unsafe-functions` with its `CustomFunctions` option (llvm/llvm-project: 41.6k
  stars, Apache-2.0 with the LLVM exception): a list of regular expressions over qualified function names, each with its
  replacement and reason. It sees calls through `using` declarations, macros and templates, which a text search does
  not. The option exists since clang-tidy 20 (its release notes); CI runs clang-tidy 18, and only
  `misc-include-cleaner`.
- **Fidelity:** none, a CI check.
- **Gain:** the check reads the code the compiler sees. The one exception of `rounded-math-exceptions.tsv`
  (`std::pow` in `Platform/SdlAudio.cpp`) becomes a `NOLINT` with its reason.
- **Risk:** it needs clang-tidy 20 or newer in the Linux job (from apt.llvm.org, not Ubuntu 24.04's) and the client
  analyzed with a compilation database, which takes much longer than the script. `glm::quat(vec3)` is a constructor, not
  a function: a name check does not see it, so that case stays in Python or becomes a `clang-query` matcher.
- **Recommendation:** later, in AR7, when clang-tidy is upgraded for the `readability-function-size` check the plan
  already puts there.

### LIB-4. tree-sitter for the architecture checks

- **MuRet code:** the Python checks read C++ with regular expressions. `check_named_values.py` (394 lines) recognizes
  constants, enums and their initializers, and says in its docstring that "declaration matching around template
  parameters is lexical"; `check_rounded_math.py` blanks comments and literals; `check_comments_only.py` (158 lines)
  strips comments and compares tokens.
- **Library:** tree-sitter with its C++ grammar (tree-sitter/tree-sitter: 27.6k stars, pushed 2026-11-25, MIT;
  py-tree-sitter 0.27.0 on PyPI, 2026-11-23; tree-sitter-cpp 0.23.4 on PyPI, 2024-11-11, its repository pushed
  2026-09-17). It gives a syntax tree with byte positions, and comments and literals as nodes. The C++ lexer of Pygments
  (BSD-2-Clause) would only cover stripping comments.
- **Fidelity:** none.
- **Gain:** constants, enums and comments found by a grammar, not by patterns, with fewer special cases to maintain.
- **Risk:** the checks run today on Python's standard library alone (`python3 -I`); this adds pinned pip packages to CI
  and to every machine that runs them. tree-sitter-cpp is an error-tolerant grammar with gaps in recent C++ and in
  macros, so a misparse is silent unless a test catches it, and its Python package has had no release in a year.
- **Recommendation:** later, only when a regex miss costs time (a false positive or a missed value), starting with
  `check_named_values.py`. `check_layers.py` reads only `#include` lines and keeps its regex.

### LIB-5. `std::expected` under `Core::Expected`

- **MuRet code:** `Client/src/Core/Expected.h`, 343 lines: MuRet's own `Expected`, because C++20 has none (L-3 of the
  [learning path](../superpowers/specs/2026-10-28-camino-de-aprendizaje-design.md), D-C1 and D-C7 of the
  [client roadmap](../superpowers/specs/2026-10-30-cliente-design.md)). 1,176 files include it, with about 26,600
  calls to `HasValue()`, `Value()` and `Error()`.
- **Library:** `std::expected`, standard since C++23. GCC 13 of `ubuntu-24.04` and MSVC 2022 ship it. Not tl::expected
  (TartanLlama/expected: 1.9k stars, last push 2025-09-01, CC0-1.0): a third-party bridge to what the standard has.
- **Fidelity:** none.
- **Gain:** about 250 of the 343 lines. `Core::Expected` stays as a thin class over `std::expected` with the same names
  (`HasValue`, `Value`, `Error`, rule 7), so no call changes; renaming 26,600 calls in 1,176 files is not worth it.
- **Risk:** it needs C++23, a decision of its own (D-C1, the compilers in CI, clang-tidy, the precompiled headers).
  `Value() &&` returns the value moved, not a `T&&` like `std::expected`, so that `const T& t = Load().Value();` does
  not dangle: the thin class has to keep that.
- **Recommendation:** later and low priority: only if C++23 comes in for another reason, with its ADR.

### LIB-6. fontconfig for the font lookup on Linux

- **MuRet code:** `Platform/SystemFonts.cpp` (247 lines with its header) finds the file of a face in the Windows
  registry, as GDI does. Linux has no list: without `[Video] FontFile`, the client stops with an error that names the
  key (`App/FontFiles.h`).
- **Library:** fontconfig, the font lookup of Linux desktops (freedesktop.org; port `fontconfig` 2.17.1 in the
  baseline, MIT-style license).
- **Fidelity:** the face is a text of the game (texts 0 and 18 of `Text.bmd`), and the file found decides the letter
  widths the 2D layout depends on.
- **Gain:** on Linux the client finds the face by name, as on Windows, without editing the config.
- **Risk:** fontconfig always answers with its best match, so a missing face gives a substitute with other widths:
  MuRet would accept only an exact family match and keep the error otherwise. One more dependency, for the Linux build
  only.
- **Recommendation:** optional. It does not replace code, it fills the Linux gap; worth it only if Linux players
  without `FontFile` matter.

### LIB-7. A packet layout generator, if AR2 asks for one

AR2.1 writes `Field<T, Offset>` and `PacketLayout` by hand and decides "no code generator for now"; the plan already
reviews that when AR2 closes. If the review wants a generator, the first to compare is Kaitai Struct
(kaitai-io/kaitai_struct: 4.7k stars, pushed 2026-11-23; port `kaitai-struct-cpp-stl-runtime` 0.11, MIT): one `.ksy`
description gives C++ for the client and Python for the research scripts. Its C++ runtime reports errors with
exceptions, not `Expected`, so the client's errors (`TooShort` for a truncated F3/10,
[ADR 0010](../adr/0010-inventario-truncado.md)) would have to be checked against it. bitsery, zpp_bits and Boost.PFR
serialize C++ types; none describes a layout someone else fixed.

## Checked and kept

| Area | MuRet code | Libraries looked at | Why it stays |
| --- | --- | --- | --- |
| Crash handler | `Core/Crash/` (2,643 lines) | Crashpad (616 stars, Apache-2.0), sentry-native (583, MIT), Breakpad (2.9k), cpptrace (1.5k, MIT) | Fidelity: it writes `MuError.dmp` byte by byte as `0x67f840` does (Q-C241 to Q-C245), and none of them writes that format. A symbolized trace in MuRet's own log (cpptrace) would be a new feature, not a replacement. |
| CRC-32 | `Core/Crash/Crc32` (75 lines) | zlib, crc32c | The client does not link zlib and the CRC is a few lines (its header says so); crc32c is another polynomial. The copy in `tests/world/test_leaves.cpp` is AR6 deduplication, not a library. |
| Log | `Core/Log`, `FileSink`, `StderrSink`, `TeeSink` (383 lines) | spdlog (29.8k, MIT, 1.17.0), quill (3.1k, MIT, 13.1.0) | Decided in CL3 (question 4, in `Log.h`): four levels, two sinks and a mutex, plus the behavior of [ADR 0006](../adr/0006-el-log.md) (append, empty past 1 MiB, one warning on a failed write). Revisit if the client needs rotation, an asynchronous log or threads of its own. |
| Sockets | `Network/SocketApi`, `SystemSocket`, `SystemResolver` (992 lines) | Asio (6.2k, BSL-1.0, 1.38.2), SDL3_net (371, Zlib, 3.2.0), libuv (27.3k, MIT) | Decided in CL4 (question 3, in `SocketApi.h`, checked with Context7 on 2026-11-02): one non-blocking connection polled each frame (D-C5) with the original's 0x2000 send buffer. SDL3_net would bring SDL3 into the network layer, which must not see it (even more in `engine/` after AR1); Asio and libuv are handler models for many connections. |
| GL loader | `Platform/GlLoader`, `GlFunctionList.h` (325 lines) | glad (4.6k; the vcpkg port is still glad 0.1.36), libepoxy (760), glbinding (882), GLEW (2.9k) | Decided in D-C2: an X-macro of 46 functions over `SDL_GL_GetProcAddress`. The glad port brings the compatibility profile and needs Python; the others are larger than the table. |
| UTF-8 and Windows-1252 | `Core/Utf8`, `Windows1252`, `Windows1252BestFit` (327 lines) | utfcpp (2k, BSL-1.0), simdutf (2k), ICU | The best-fit table is what XP's `WideCharToMultiByte` gave, measured ([ADR 0016](../adr/0016-texto-occidental-en-windows-1252.md)). The UTF-8 reader is 62 lines with its own contract (the cursor stays on an invalid sequence). |
| The CRT's `sprintf` | `UI/SprintfFormat`, `UI/OriginalFormat` (667 lines) | {fmt} (26k, MIT), stb_sprintf | Fidelity: they walk the state machine of VC6's `_output` (Q-C590) with its flags, widths and mismatched arguments; rule 10 keeps `std::format` for MuRet's own texts. |
| Sorting, random numbers, x87 | `Core/Msvc6Sort`, `Crt/CrtRandom`, `Math/X87`, `World/Ftol` | the standard library | Fidelity: the order of equal elements, the `rand()` sequence and the float conversions of the original (rule 10). |
| Retained math | `Render/Models/OriginalRetainedMath` (988 lines) | Boost.Multiprecision, MPFR | Decided in [ADR 0014](../adr/0014-perfil-matematico-retenido.md), with no Boost.Multiprecision. MPFR is LGPL and only makes test vectors. |
| OZT textures | `Data/Textures/OztTexture` (186 lines) | stb_image (35k) | Decided in its header: stb reads the TGA descriptor and loads the 24-bit `Monster\wolfa02.OZT` the original rejects. |
| Config file | `App/Config.cpp` (628 lines: about 150 read the INI, the rest checks each value) | CLI11's config reader (already a dependency), inih (3.1k, BSD-3-Clause), SimpleIni (1.3k, MIT), toml++ (2.2k, MIT) | The reader gives each error with its line and its reason (an unknown section, a repeated key, a lone `\r`), and `ConfigTest` pins them. CLI11 maps sections to subcommands and gives no line; inih gives only the line of the first error; toml++ would change the format players edit (`Path = C:\MU` is not valid TOML). Not worth it for 150 lines; reconsider if the config grows lists or nesting. |
| Text cache | `Render/Text/TextCache` (490 lines) | generic LRU caches | It frees the text drawn longest ago but never a slot already in the frame's batch; a generic cache has no such pin. |
| Pathfinding | `GameLogic/PathFinder` (389 lines) | A* libraries | Fidelity: the A* of `0x4ead70`, whose order among equal costs decides the path. |
| Reading and writing packets | `Core/ByteReader`, `Network/PacketReader`, `PacketWriter` | bitsery, zpp_bits | MU's framing (C1 to C4, big-endian sizes, XOR32 as a separate stage). AR2.1 designs the layouts ([LIB-7](#lib-7-a-packet-layout-generator-if-ar2-asks-for-one)). |
| Behavior golden | `tests/characterization`, `FrameDigest` (1,090 lines) | ApprovalTests.cpp (342 stars, Apache-2.0) | Not popular enough by this review's measure, and the custom part is the digest of a frame, which it does not write; approving is one environment variable. |
| Test hashes | FNV-1a in `tests/support/Fnv1a.h` | xxHash (11.5k) | The goldens keep FNV-1a values, and FNV-1a is a few lines. |
| Module layers | `check_layers.py` (256 lines) | include-what-you-use (4.8k) | IWYU checks that a file includes what it uses (`misc-include-cleaner` does that already), not which module may include which. The layers become CMake targets in AR1.2, and AR7 makes the check strict. |
| CI scope | the "What the PR changes" step (15 lines of bash) | dorny/paths-filter | One `case` does not need a third-party action. |
| Research scripts | `Client/docs/research/scripts/` | | They already use pefile, unicorn and numpy for reading the PE, emulating and arrays; their imports show nothing else a library should take. |

## Already decided, not reopened

- The standard library over EASTL and other container libraries ([ADR 0026](../adr/0026-keep-the-standard-library.md)).
- Correctly rounded math from LLVM-libc behind `RoundedMath.h` ([ADR 0027](../adr/0027-correctly-rounded-math.md));
  MuRet's own retained math, without Boost.Multiprecision ([ADR 0014](../adr/0014-perfil-matematico-retenido.md)).
- SDL3 and OpenGL 3.3 core with MuRet's own GL loader (D-C2, D-C3); no full engine migration.
- JPEG with libjpeg-turbo ([ADR 0004](../adr/0004-jpeg-con-libjpeg-turbo.md)), audio with SDL3_mixer
  ([ADR 0021](../adr/0021-audio-sdl3.md)), text with FreeType (D-C12), the command line with CLI11.

## How to pick up an item

Each item is a PR of its own, started when Emanuel says so. As rule 10 asks, the PR says which libraries it looked at
and why one came in; the dependency goes into `vcpkg.json` at the baseline's version, with an ADR if it weighs; every
test and golden stays the same. The plan's AR6 and AR7 sections point back here.
