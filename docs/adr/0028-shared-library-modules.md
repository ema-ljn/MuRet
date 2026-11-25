# 0028. The engine ships as four shared libraries

**Status** · Accepted. Emanuel decided the shape on 2026-11-25: the first draft proposed one library per engine
module, 14 files, and he rejected it (14 DLLs make no sense); of the second draft's options he chose the three
libraries together with `Audio`. Task 8 of the [AR1 plan](../superpowers/plans/2026-11-22-ar1-engine-apart.md) builds
it; ["What building it changed"](#what-building-it-changed-task-8-2026-11-25) lists where the build differs from this
text.

## Context

- **What the build is today** (`main` `380760c8`). Three static libraries already cut the client by what the code
  sees: `ClientLib` holds everything that sees neither SDL nor OpenGL (the engine, the game and the app),
  `ClientPlatform` what sees SDL3 (`src/Platform/`) and `ClientRender` what sees OpenGL (`src/Render/Gl/`). `Main` links
  the three. Each test file is an executable (756 files) that links all of `ClientLib`: with debug information the ones
  of `tests/scenes` weigh about 100 MB each, which is why CI strips it (`.github/workflows/ci.yml`).
- **What the refactor already decided.** [ADR 0023](0023-client-module-architecture.md) gives every module a target,
  because a boundary between libraries is the only one the linker checks, and the
  [spec](../superpowers/specs/2026-11-20-client-architecture-design.md) wrote those targets as static libraries. Since
  AR0, `check_layers.py` checks every include against the modules' layers in CI, and since AR1.1 no engine file includes
  the game: its baseline has no engine row.
- **What Emanuel asked.** 2026-11-22: not one library. On Windows something like `Engine.dll`, `OpenGL.dll`,
  `Music.dll` and a few more, and the same on Linux with `.so`. No `MuRet` in any name: the executable is `main.exe`,
  like the original's. 2026-11-23: the XOR and remap layer is not shipped as a DLL that says what it is. 2026-11-25:
  not one DLL per module.
- **How big the engine is.** 330 files and 29,949 lines in 14 modules, 11% of the client's 270,794 lines. By what the
  code sees: 277 files and 25,399 lines see neither SDL nor OpenGL; 29 files and 2,304 lines see SDL (`Platform/`), two
  of them the SDL3_mixer backend (`SdlAudio`, 433 lines); 21 files and 1,919 lines see OpenGL (`Render/Gl/`); and the
  XOR and remap layer is 3 files and 327 lines.
- **Who uses the backends.** From outside them, only `main.cpp` and the tests of `tests/platform`, `tests/render_gl`
  and `tests/support` include `Platform/` or `Render/Gl/`. `Render/Gl/` includes `Platform/` (it calls OpenGL through
  the function table `Platform` loads); `Platform/` does not include `Render/Gl/`. `SdlAudio` includes nothing else of
  `Platform/`: it opens SDL's audio subsystem itself, while `SdlContext` starts and quits SDL and `SdlLog` routes its
  log. Six files of the `engine/Platform` module do not see SDL and live outside `Platform/` today (`WindowState`,
  `Resolution` and `VSyncMode` in `App/`, `IClipboard` in `UI/`; 192 lines): the game, the app and their tests use them.
- **Where the outside libraries enter.** SDL is included by 18 files of `Platform/` and by `main.cpp` (`SDL_main.h`),
  SDL3_mixer by `SdlAudio.cpp` alone, libjpeg-turbo by four files of `Data/Textures/` and FreeType by one file of
  `Render/Text/`. On Windows vcpkg gives them as DLLs (`x64-windows`), on Linux as static libraries (`x64-linux`). A
  first search of the 196 engine headers found no mutable state defined in a header.
- **What large projects do** (their documentation, read through Context7 on 2026-11-22 and again on 2026-11-25).
  Source and id Tech 3 (known, not read there) ship `engine.dll`, one library per graphics backend and the game as
  `client.dll`. Unreal compiles each module into a DLL loaded by one executable and can build a monolithic executable
  instead; its renderer is its own module, called through an interface. Its advice on many DLLs: better link times and
  faster iteration, against dealing with exports and interface classes more often. O3DE builds each Gem as a `.dll`
  and, with `LY_MONOLITHIC_GAME`, as a `.lib` linked into the launcher, which is smaller on disk and can start faster.
  The original itself ships its music player apart, as `wzAudio.dll`.
- **What a shared library changes.** On Windows a DLL exports only what is declared for export and must link
  completely by itself; on Linux a `.so` exports everything and may leave symbols undefined unless it is told
  otherwise. State defined in a header gets one copy per library. A function is inlined across libraries only if its
  body is in a header.

## Options considered

| | A. Three: `Engine`, `Platform`, `OpenGL` | B. Four: A and `Audio` | C. Two: `Engine` with SDL inside, `OpenGL` | D. One per engine module | E. Static, as today or one per module (the spec) |
| --- | --- | --- | --- | --- | --- |
| Our files beside `main.exe` | 3 | 4 | 2 | 14 | none |
| What the linker checks | the engine needs no backend and no game; each backend needs only what is below it | the same, and the audio backend needs only the engine | the engine needs neither OpenGL nor the game | every engine module's dependencies | nothing until the executable links |
| Who can include SDL | only `Platform`, as today | `Platform` and `Audio` | any engine file | only `Platform` | only `ClientPlatform` |
| What an engine test links | `Engine`: no SDL, no game | `Engine` | `Engine`, which brings SDL | its module's library | all of `ClientLib`, or its module's library |
| Changing the renderer | replace `OpenGL` | replace `OpenGL` | replace `OpenGL` | replace `OpenGL` | relink |
| What it costs | exports at three boundaries | on Linux SDL has to become one shared library, or each of the two carries its own copy | the build stops guarding who sees SDL | 14 files, five from modules under 1,000 lines, for a boundary `check_layers.py` already checks | not what he asked |
| What Emanuel asked | his `Engine.dll` and `OpenGL.dll`, and one for SDL | A and his `Music.dll`: **chosen**, 2026-11-25 | the fewest | rejected, 2026-11-25 | no |

A is the cut the build already has, turned into files. B adds the one backend that does not depend on the window, the
sound, and pays for it on Linux, where SDL stops being a static library. D put the linker between every pair of
modules, but the include check already holds those boundaries, so it paid 14 files for little. C saves a file and
loses the only guard that keeps SDL in one place.

## Decision

- **The shape: B, four libraries cut by what the code sees.** `.dll` on Windows and `.so` on Linux.
  - `Engine`: everything in the engine that sees neither SDL nor OpenGL. The modules `Core`, `Math`, `Crt`, `Crypto`,
    `Diagnostics`, `Input`, `Audio`, `Network`, `Data`, `Pathfinding`, `Render` and `Models`, and `Platform`'s six
    files without SDL. 277 files, 25,399 lines.
  - `Platform`: what sees SDL for the window and the system. The window, the events, the GL context and its function
    loader, the clipboard, the clock, the display modes and the system fonts. 27 files, 1,871 lines. It links `Engine`.
  - `OpenGL`: what sees OpenGL, `Render/Gl`. 21 files, 1,919 lines. It links `Engine` and `Platform`.
  - `Audio`: what sees SDL3_mixer, the backend `SdlAudio`. 2 files, 433 lines. It links `Engine`, not `Platform`.
- **The sound's contracts stay in `Engine`.** The `Audio` module (the playback contracts and the music player, 5
  files, 237 lines) does not see SDL: the game uses it and its tests run without a device. `Audio.dll` is the part that
  plays, as `wzAudio.dll` is in the original.
- **A module is a folder, not a library.** The 14 modules keep their rows, layers and `may_use` in
  `Client/modules.tsv`, and `check_layers.py` keeps checking their includes. The libraries add the linker where a
  mistake costs most: the engine must link without the game and without a backend.
- **Each backend is a folder and a module of its own**, under the module whose contracts it implements, so the map,
  the folders and the libraries say the same: `engine/Render/Gl` (layer 5) is `OpenGL`, `engine/Platform/Sdl` (layer 4,
  27 files of `Platform/` today) is `Platform` and `engine/Audio/Sdl` (layer 3, `SdlAudio`) is `Audio`.
  `engine/Platform` keeps its six files without SDL, in `Engine` as they are in `ClientLib` today. Their map rows
  change before the move (Task 7), so `move.py` puts every file in its library's folder.
- **Only the executable and the backends' tests link `Platform`, `OpenGL` and `Audio`**, so changing the renderer or
  the sound means changing one library (the spec, "The engine apart": the door stays open to swapping only the graphics
  backend).
- **`XorRemap` is not a library of its own.** It stays a module (`engine/Network/XorRemap`, `MU_XOR_REMAP`) and a
  static target that only the executable and its tests link: no shipped file names it and no library exports it.
- **The game: few libraries, starting with one.** `Game`, in AR4. Until then `ClientLib` stays static with the game
  and the app, and links `Engine`.
- **One switch for one binary.** `MU_MONOLITHIC=ON` builds the four static and links one executable. The
  "Windows (MSVC, Release, matemática)" job builds it, so both shapes build on every PR.
- **The names.** No prefix: `Engine.dll`, `Platform.dll`, `OpenGL.dll`, `Audio.dll`, later `Game.dll`, and `main.exe`
  (`main` on Linux, where CMake names the libraries `libEngine.so`). Targets use the same words. CMake options and
  functions use `MU_` and `mu_`.
- **What a library exports is written down.** CMake's `generate_export_header` gives each library a macro
  (`ENGINE_API`, `PLATFORM_API`, `OPENGL_API`, `AUDIO_API`). A class or a free function that the game, the app,
  another library or a test uses carries it, interfaces included; templates and inline functions carry none. Every
  library builds with hidden visibility (`CXX_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN`) and, on Linux,
  `-Wl,--no-undefined`, so a missing macro or a missing dependency fails on both systems and not only on Windows.
- **No mutable state defined in a header.** It lives in a `.cpp` behind an exported function, or each library would
  get its own copy.
- **An outside library has one owner, or it is shared.** libjpeg-turbo, FreeType and the LLVM-libc headers are private
  to `Engine` (`RoundedMath` keeps its own compile options inside it) and SDL3_mixer to `Audio`. SDL3 has two users,
  `Platform` and `Audio`, and one state, so it is a shared library on both systems: Windows already ships `SDL3.dll`,
  and Linux gets `libSDL3.so` beside the binaries, from a vcpkg triplet of the repo that builds only that port shared.
  A static copy in each of the two would be two SDLs in one process.
- **One runtime, one build.** The Windows triplet stays `x64-windows` (dynamic CRT), so memory and `std` types can
  cross a library. The libraries are built together and shipped together: no ABI is promised between builds.
- **One output folder.** Every binary goes to `bin/`: Windows looks for an executable's DLLs beside it, and Linux gets
  `$ORIGIN` as its `RPATH`.
- **The fidelity flags follow the code.** `mu_set_fidelity_flags(target)` puts `-ffp-contract=off` on every target that
  does the original's arithmetic.
- **Tests.** The tests of the engine's folders link `Engine` and `EngineTestSupport`, the ones of `tests/platform` add
  `Platform`, the ones of `tests/render_gl` add `OpenGL` and the test of `SdlAudio` adds `Audio`: `ClientTestSupport`
  splits into `EngineTestSupport` and the game's support.
- **Linked, not loaded.** The libraries are link dependencies of the executable. Nothing is loaded by name at run time
  and nothing registers itself (ADR 0023: no plugins).

## Consequences

- Windows ships four more files beside `main.exe`, five with `Game` in AR4, plus the outside DLLs it already ships.
  Linux ships `libSDL3.so` too, which today is inside the executable.
- The Linux build changes its vcpkg triplet, so CI builds SDL3 again once and its cache key includes the triplet.
- The linker checks, on both CI systems, that the engine links without the game and without a backend. Today only the
  includes are checked.
- Between the 12 modules inside `Engine` the check stays the include check. A boundary it fails to hold is what would
  give that module a library of its own.
- The tests of the engine's folders (181 of the 756 files) stop linking the game, and a game change stops relinking
  the engine. A test that reaches the game keeps linking `ClientLib` until AR4 gives the game its library.
- Task 8 marks, with a script, the public API in the engine's 196 headers and lists what it marked. From then on a
  new class that the game or another library uses needs its macro, and Linux fails without it as Windows does.
- No behavior change is expected: the same code with the same flags, called through an import. The behavior golden and
  every test, on the four CI configurations, are the proof.
- A call across libraries is not inlined; a call between modules inside `Engine` is compiled as today. Not measured:
  frame time and start-up time in the two shapes. `MU_MONOLITHIC` is the shape for a release if a measurement asks for
  it.
- The shared precompiled header needs the same options in every library. If a compiler refuses the reuse, each library
  builds its own.
- "One target per module" of ADR 0023 and the spec's `MuRet<Module>` static targets become, for the engine, one target
  per library: a module is a folder, its row in `modules.tsv` and its include check. The game's modules get their
  answer in AR4. `docs/CODING_RULES.md` gets the three rules a contributor meets (the macro, no state in a header, who
  owns an outside library) in Task 8's PR, with the build that enforces them.
- The [music plan](../superpowers/plans/2026-11-25-music-home.md) recommended no library for music in AR1. Its player
  keeps the home that plan gives it, in `Engine` behind its port; what gets a library here is the backend.
- What would reopen it: a measured cost in frame time or start-up that `MU_MONOLITHIC` does not cover, a second
  graphics or audio backend, or a module inside `Engine` whose boundary the include check does not hold.

## What building it changed (Task 8, 2026-11-25)

- **The macro follows the linker.** A class or a free function carries its library's macro when it is defined in a
  `.cpp` and another binary links it. A script reads the build's objects for the symbols one target defines and
  another asks for: 525 functions and 12 virtual tables, on 56 classes and 241 free functions in 132 of the 196
  headers. An interface that lives whole in its header carries no macro, which narrows "interfaces included" above: it
  exports no symbol, so nothing would check its macro. With this rule a missing macro is always a link error, on both
  systems.
- **No engine header defines mutable state.** A script read the 196 headers and found none, so no declaration moved.
- **A test links what its includes reach,** not what its folder says, because the test folders are mixed (in
  `tests/data`, 35 files see only the engine and 30 reach the game). 151 of the 755 test files link `Engine` and
  `EngineTestSupport` without the game; the estimate above was 181 of 756.
- **`bin/` holds the client's binaries and the labs'.** The DataServer links none of these libraries and keeps its
  paths.
- **Who links `Platform` sees SDL's headers and does not link SDL.** `Platform`'s headers name SDL's types, so it
  passes on `SDL3::Headers`; a binary that calls SDL itself (the executable, the backends' tests) links `SDL3::SDL3`.

## Open questions

1. **One test executable per file, or fewer?** Asked since 2026-11-02. Today there are 756, one per file; one per
   module would be 15 to 40 and far fewer links. Proposed: Task 8 keeps one per file, so its numbers measure only the
   change of library shape, and the answer comes after with those numbers.
