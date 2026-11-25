# 0028. The engine ships as three shared libraries

**Status** · Proposed on 2026-11-25 (AR1 Task 6), from what Emanuel asked on 2026-11-22. The first draft proposed one
library per engine module, 14 files; he rejected it the same day (14 DLLs make no sense), and this is the second. It
becomes accepted with his yes, and Task 8 of the [AR1 plan](../superpowers/plans/2026-11-22-ar1-engine-apart.md)
builds it.

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
  code sees: 277 files and 25,399 lines see neither SDL nor OpenGL, 29 files and 2,304 lines see SDL (`Platform/`),
  21 files and 1,919 lines see OpenGL (`Render/Gl/`), and the XOR and remap layer is 3 files and 327 lines.
- **Who uses the two backends.** From outside them, only `main.cpp` and the tests of `tests/platform`,
  `tests/render_gl` and `tests/support` include `Platform/` or `Render/Gl/`. `Render/Gl/` includes `Platform/` (it calls
  OpenGL through the function table `Platform` loads); `Platform/` does not include `Render/Gl/`. Six files of the
  `engine/Platform` module do not see SDL and live outside `Platform/` today (`WindowState`, `Resolution` and
  `VSyncMode` in `App/`, `IClipboard` in `UI/`; 192 lines): the game, the app and their tests use them.
- **Where the outside libraries enter.** SDL is included by 18 files of `Platform/` and by `main.cpp` (`SDL_main.h`),
  libjpeg-turbo by four files of `Data/Textures/` and FreeType by one file of `Render/Text/`. On Windows vcpkg gives
  them as DLLs, on Linux as static libraries. A first search of the 196 engine headers found no mutable state defined
  in a header.
- **What large projects do** (their documentation, read through Context7 on 2026-11-22 and again on 2026-11-25).
  Source and id Tech 3 (known, not read there) ship `engine.dll`, one library per graphics backend and the game as
  `client.dll`. Unreal compiles each module into a DLL loaded by one executable and can build a monolithic executable
  instead; its renderer is its own module, called through an interface. Its advice on many DLLs: better link times and
  faster iteration, against dealing with exports and interface classes more often. O3DE builds each Gem as a `.dll`
  and, with `LY_MONOLITHIC_GAME`, as a `.lib` linked into the launcher, which is smaller on disk and can start faster.
- **What a shared library changes.** On Windows a DLL exports only what is declared for export and must link
  completely by itself; on Linux a `.so` exports everything and may leave symbols undefined unless it is told
  otherwise. State defined in a header gets one copy per library. A function is inlined across libraries only if its
  body is in a header.

## Options considered

| | A. Three: `Engine`, `Platform`, `OpenGL` | B. Four: A and `Audio` | C. Two: `Engine` with SDL inside, `OpenGL` | D. One per engine module | E. Static, as today or one per module (the spec) |
| --- | --- | --- | --- | --- | --- |
| Our files beside `main.exe` | 3 | 4 | 2 | 14 | none |
| What the linker checks | the engine needs no backend and no game; each backend needs only what is below it | the same | the engine needs neither OpenGL nor the game | every engine module's dependencies | nothing until the executable links |
| Who can include SDL | only `Platform`, as today | `Platform` and `Audio` | any engine file | only `Platform` | only `ClientPlatform` |
| What an engine test links | `Engine`: no SDL, no game | `Engine` | `Engine`, which brings SDL | its module's library | all of `ClientLib`, or its module's library |
| Changing the renderer | replace `OpenGL` | replace `OpenGL` | replace `OpenGL` | replace `OpenGL` | relink |
| What it costs | exports at three boundaries | on Linux SDL has to become one shared library, or each of the two carries its own copy | the build stops guarding who sees SDL | 14 files, five from modules under 1,000 lines, for a boundary `check_layers.py` already checks | not what he asked |
| What Emanuel asked | his `Engine.dll` and `OpenGL.dll`, and one for SDL | A and his `Music.dll` | the fewest | rejected, 2026-11-25 | no |

A is the cut the build already has, turned into files: nothing new to draw. D put the linker between every pair of
modules, but the include check already holds those boundaries, so it paid 14 files for little. C saves one file and
loses the only guard that keeps SDL in one place. B is A plus the audio backend apart, and its cost is on Linux (open
question 2).

## Decision

- **The shape: A, three libraries cut by what the code sees.** `.dll` on Windows and `.so` on Linux.
  - `Engine`: everything in the engine that sees neither SDL nor OpenGL. The modules `Core`, `Math`, `Crt`, `Crypto`,
    `Diagnostics`, `Input`, `Audio`, `Network`, `Data`, `Pathfinding`, `Render` and `Models`, and `Platform`'s six
    files without SDL. 277 files, 25,399 lines.
  - `Platform`: what sees SDL. The window, the events, the GL context and its function loader, the clipboard, the
    clock, the display modes, the system fonts and the SDL3_mixer backend (`SdlAudio`). 29 files, 2,304 lines. It links
    `Engine`.
  - `OpenGL`: what sees OpenGL, `Render/Gl`. 21 files, 1,919 lines. It links `Engine` and `Platform`.
- **A module is a folder, not a library.** The 14 modules keep their rows, layers and `may_use` in
  `Client/modules.tsv`, and `check_layers.py` keeps checking their includes. The libraries add the linker where a
  mistake costs most: the engine must link without the game and without a backend.
- **Each backend is a folder and a module of its own**, so the map, the folders and the libraries say the same:
  `engine/Render/Gl` (layer 5) is `OpenGL` and `engine/Platform/Sdl` (layer 4, the 29 files of `Platform/` today) is
  `Platform`. `engine/Platform` keeps its six files without SDL, in `Engine` as they are in `ClientLib` today. Their
  map rows change before the move (Task 7), so `move.py` puts every file in its library's folder.
- **Only the executable and the backends' tests link `Platform` and `OpenGL`**, so changing the renderer means
  changing one library (the spec, "The engine apart": the door stays open to swapping only the graphics backend).
- **`XorRemap` is not a library of its own.** It stays a module (`engine/Network/XorRemap`, `MU_XOR_REMAP`) and a
  static target that only the executable and its tests link: no shipped file names it and no library exports it.
- **The game: few libraries, starting with one.** `Game`, in AR4. Until then `ClientLib` stays static with the game
  and the app, and links `Engine`.
- **One switch for one binary.** `MU_MONOLITHIC=ON` builds the three static and links one executable. The
  "Windows (MSVC, Release, matemática)" job builds it, so both shapes build on every PR.
- **The names.** No prefix: `Engine.dll`, `Platform.dll`, `OpenGL.dll`, later `Game.dll`, and `main.exe` (`main` on
  Linux, where CMake names the libraries `libEngine.so`). Targets use the same words. CMake options and functions use
  `MU_` and `mu_`.
- **What a library exports is written down.** CMake's `generate_export_header` gives each library a macro
  (`ENGINE_API`, `PLATFORM_API`, `OPENGL_API`). A class or a free function that the game, the app, another library or
  a test uses carries it, interfaces included; templates and inline functions carry none. Every library builds with
  hidden visibility (`CXX_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN`) and, on Linux,
  `-Wl,--no-undefined`, so a missing macro or a missing dependency fails on both systems and not only on Windows.
- **No mutable state defined in a header.** It lives in a `.cpp` behind an exported function, or each library would
  get its own copy.
- **An outside library has one owner.** libjpeg-turbo, FreeType and the LLVM-libc headers are private to `Engine`
  (`RoundedMath` keeps its own compile options inside it), SDL3 and SDL3_mixer to `Platform`. Code that needs one
  goes through its owner. On Linux vcpkg gives them static, and a second owner would be a second copy of their state.
- **One runtime, one build.** The vcpkg triplet stays `x64-windows` (dynamic CRT), so memory and `std` types can cross
  a library. The libraries are built together and shipped together: no ABI is promised between builds.
- **One output folder.** Every binary goes to `bin/`: Windows looks for an executable's DLLs beside it, and Linux gets
  `$ORIGIN` as its `RPATH`.
- **The fidelity flags follow the code.** `mu_set_fidelity_flags(target)` puts `-ffp-contract=off` on every target that
  does the original's arithmetic.
- **Tests.** The tests of the engine's folders link `Engine` and `EngineTestSupport`, the ones of `tests/platform` add
  `Platform` and the ones of `tests/render_gl` add `OpenGL`: `ClientTestSupport` splits into `EngineTestSupport` and
  the game's support.
- **Linked, not loaded.** The libraries are link dependencies of the executable. Nothing is loaded by name at run time
  and nothing registers itself (ADR 0023: no plugins).

## Consequences

- Windows ships three more files beside `main.exe`, four with `Game` in AR4, plus the outside DLLs it already ships.
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
  answer in AR4. `docs/CODING_RULES.md` gets the three rules a contributor meets (the macro, no state in a header, one
  owner per outside library) in Task 8's PR, with the build that enforces them.
- What would reopen it: a measured cost in frame time or start-up that `MU_MONOLITHIC` does not cover, a second
  graphics or audio backend, or a module inside `Engine` whose boundary the include check does not hold.

## Open questions

1. **One test executable per file, or fewer?** Asked since 2026-11-02. Today there are 756, one per file; one per
   module would be 15 to 40 and far fewer links. Proposed: Task 8 keeps one per file, so its numbers measure only the
   change of library shape, and the answer comes after with those numbers.
2. **An `Audio.dll`, his `Music.dll`?** It would hold the SDL3_mixer backend, `SdlAudio` (2 files, 433 lines); the
   playback contracts and the music player (`Audio`, 5 files, 237 lines) stay in `Engine`, where the game uses them.
   `SdlAudio` needs SDL, and so does the window: with two of our libraries linking it, Linux has to get SDL as one
   shared library (another vcpkg triplet) instead of a static copy in each. Proposed: not in AR1, as the
   [music plan](../superpowers/plans/2026-11-25-music-home.md) recommends; it is option B, and it is one PR when he
   wants it.
