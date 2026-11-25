# 0028. The engine's modules are shared libraries

**Status** · Proposed on 2026-11-25 (AR1 Task 6), from what Emanuel asked on 2026-11-22. It becomes accepted with his
yes, and Task 8 of the [AR1 plan](../superpowers/plans/2026-11-22-ar1-engine-apart.md) builds it.

## Context

- **What the build is today** (`main` `380760c8`). One static library, `ClientLib`, holds everything that sees neither
  SDL nor OpenGL: the engine, the game and the app. `ClientPlatform` (SDL3), `ClientRender` (OpenGL) and
  `ClientRoundedMath` are static too, and `Main` links them. Each test file is an executable (756 files) that links all of `ClientLib`: with
  debug information the ones of `tests/scenes` weigh about 100 MB each, which is why CI strips it
  (`.github/workflows/ci.yml`).
- **What the refactor already decided.** [ADR 0023](0023-client-module-architecture.md) gives every module a target,
  because a boundary between libraries is the only one the linker checks. The
  [spec](../superpowers/specs/2026-11-20-client-architecture-design.md) wrote those targets as static libraries.
- **What Emanuel asked** (2026-11-22): not one library. On Windows something like `Engine.dll`, `OpenGL.dll`,
  `Music.dll` and a few more, and the same on Linux with `.so`. No `MuRet` in any name: the executable is `main.exe`,
  like the original's. On 2026-11-23: the XOR and remap layer is not shipped as a DLL that says what it is.
- **How big the engine is.** 330 files and 29,949 lines in 14 modules, 11% of the client's 270,794 lines (the game has
  237,196 in 43 modules). The largest engine module is `Render` (4,535 lines without its OpenGL backend) and five are
  under 1,000 lines: `Audio` (237), `Pathfinding` (614), `Crypto` (673), `Crt` (860) and `Input` (865).
- **Where the outside libraries enter.** Only `main.cpp` includes `Render/Gl/` (21 files, 1,919 lines). SDL is
  included by 18 files of `Platform/` and by `main.cpp` (`SDL_main.h`), libjpeg-turbo by four files of
  `Data/Textures/` and FreeType by one file of `Render/Text/`. A first search of the 196 engine headers found no
  mutable state defined in a header.
- **What large projects do** (their documentation, read through Context7 on 2026-11-22 and again on 2026-11-25).
  Unreal compiles each module into a DLL loaded by one executable and can build a monolithic executable instead. Its
  renderer is its own module so that a rendering change does not relink the application, and the engine calls it
  through an interface. Its advice on many DLLs: better link times and faster iteration, against dealing with exports
  and interface classes more often, a trade-off it calls "the correct one for engine and editor code" and
  "questionable for gameplay". O3DE builds each Gem as a `.dll` and, with `LY_MONOLITHIC_GAME`, as a `.lib` linked
  into the launcher, which is smaller on disk and can start faster. Source and id Tech 3 (known, not read there) ship
  `engine.dll`, one library per graphics backend and the game as `client.dll`.
- **What a shared library changes.** On Windows a DLL exports only what is declared for export and must link
  completely by itself; on Linux a `.so` exports everything and may leave symbols undefined unless it is told
  otherwise. State defined in a header gets one copy per library. A function is inlined across libraries only if its
  body is in a header.

## Options considered

| | A. One shared library per engine module | B. A few big ones (`Engine`, `OpenGL`, `Audio`) | C. One static library per module (the spec) | D. `ClientLib` as today |
| --- | --- | --- | --- | --- |
| What the linker checks | every engine module's dependencies, when its library links | the three or four boundaries between the big ones | nothing until the executable links: a missing dependency resolves from any library on the line | nothing |
| Files beside `main.exe` | 14, plus the outside DLLs it already ships | 3 or 4 | none of ours | none of ours |
| Exports to mark | what one module uses of another | only what crosses the big boundaries | none | none |
| What an engine test links | its module's library | all of `Engine` | its module's library | all of `ClientLib` |
| Changing the renderer | replace `OpenGL` | replace `OpenGL` | relink | relink |
| What Emanuel asked | the same idea, one level finer | his words | no | no |

B is what he first described, and it ships fewer files. It leaves the 12 modules inside `Engine` with no linker
between them, which is the boundary ADR 0023 wants checked, and it adds a second map (which module goes in which
library) beside `modules.tsv`. C checks includes but not links. A costs more exports and more files, and gives one
rule: a module is a library.

## Decision

- **The shape: A.** Every engine module is a shared library, `.dll` on Windows and `.so` on Linux: `Core`, `Math`,
  `Crt`, `Crypto`, `Diagnostics`, `Input`, `Audio`, `Network`, `Data`, `Platform`, `Pathfinding`, `Render`, `Models`,
  and the OpenGL backend apart as `OpenGL` (a new module, `engine/Render/Gl`, layer 5). Each links only what its
  `may_use` row in `Client/modules.tsv` allows. A link that row does not allow is a layer bug, fixed in the code.
- **Only the executable and the tests of `Render/Gl` link `OpenGL`**, so changing the renderer means changing one
  library (the spec, "The engine apart": the door stays open to swapping only the graphics backend).
- **`XorRemap` is not a library of its own.** It stays a module (`engine/Network/XorRemap`, `MU_XOR_REMAP`) and a
  static target that only the executable and its tests link: no shipped file names it and no library exports it.
- **The game: few libraries, starting with one.** `Game`, in AR4, as Unreal advises for gameplay code. Until then
  `ClientLib` stays static with the game and the app.
- **One switch for one binary.** `MU_MONOLITHIC=ON` builds every module static and links one executable. The
  "Windows (MSVC, Release, matemática)" job builds it, so both shapes build on every PR.
- **The names.** No prefix: `Core.dll`, `Audio.dll`, `Render.dll`, `OpenGL.dll`, later `Game.dll`, and `main.exe`
  (`main` on Linux, where CMake names the libraries `libCore.so`). Targets use the same words. CMake options and
  functions use `MU_` and `mu_`.
- **What a module exports is written down.** CMake's `generate_export_header` gives each module a macro (`CORE_API`,
  in a generated `Core/CoreExport.h`). A class or a free function that another module or a test uses carries it,
  interfaces included; templates and inline functions carry none. Every library builds with hidden visibility
  (`CXX_VISIBILITY_PRESET hidden`, `VISIBILITY_INLINES_HIDDEN`) and, on Linux, `-Wl,--no-undefined`, so a missing
  macro or a missing dependency fails on both systems and not only on Windows.
- **No mutable state defined in a header.** It lives in a `.cpp` behind an exported function, or each library would
  get its own copy.
- **An outside library has one owner.** libjpeg-turbo is linked privately into `Data`, FreeType into `Render`, SDL3 and
  SDL3_mixer into `Platform`, and the LLVM-libc headers stay inside `Math`, which keeps `RoundedMath`'s own compile
  options. A module that needs one goes through its owner. On Linux vcpkg gives them static, and a second owner would
  be a second copy of their state.
- **One runtime, one build.** The vcpkg triplet stays `x64-windows` (dynamic CRT), so memory and `std` types can cross
  a library. The libraries are built together and shipped together: no ABI is promised between builds.
- **One output folder.** Every binary goes to `bin/`: Windows looks for an executable's DLLs beside it, and Linux gets
  `$ORIGIN` as its `RPATH`.
- **The fidelity flags follow the code.** `mu_set_fidelity_flags(target)` puts `-ffp-contract=off` on every target that
  does the original's arithmetic.
- **Tests.** A module's tests link only its library and `EngineTestSupport`: `ClientTestSupport` splits into that and
  the game's support.
- **Linked, not loaded.** The libraries are link dependencies of the executable. Nothing is loaded by name at run time
  and nothing registers itself (ADR 0023: no plugins).

## Consequences

- The linker checks the engine's layers on both CI systems, with the same table `check_layers.py` reads. Today only
  the includes are checked.
- An engine test stops linking the game, and a game change stops relinking the engine. A test that reaches the game
  keeps linking `ClientLib` until AR4 gives the game its library.
- Windows ships 14 more files beside `main.exe`; five of them come from modules under 1,000 lines.
- Task 8 marks, with a script, the public API in the engine's 196 headers and lists what it marked. From then on a
  new class that another module uses needs its macro, and Linux fails without it as Windows does.
- No behavior change is expected: the same code with the same flags, called through an import. The behavior golden and
  every test, on the four CI configurations, are the proof.
- A call across libraries is not inlined. Not measured: frame time and start-up time in the two shapes. `MU_MONOLITHIC`
  is the shape for a release if a measurement asks for it.
- The shared precompiled header needs the same options in every library. If a compiler refuses the reuse, each library
  builds its own.
- The spec's `MuRet<Module>` static targets and "one target per module" of ADR 0023 stay true with another library
  type and other names. `docs/CODING_RULES.md` gets the three rules a contributor meets (the macro, no state in a
  header, one owner per outside library) in Task 8's PR, with the build that enforces them.
- What would reopen it: a measured cost in frame time or start-up that `MU_MONOLITHIC` does not cover, or an export
  list so long that B or C is the cheaper way to the same boundary.

## Open questions

1. **One test executable per file, or one per module?** Asked since 2026-11-02. Today there are 756, one per file;
   one per module would be 15 to 40 and far fewer links. Proposed: Task 8 keeps one per file, so its numbers measure
   only the change of library shape, and the answer comes after with those numbers.
2. **A `Music.dll`?** `Audio` holds the playback contracts and the music player (5 files, 237 lines) and the SDL3_mixer
   backend, `SdlAudio`, is in `Platform` with the rest of SDL (2 files, 433 lines). Proposed: no library for music in
   AR1, as the [music plan](../superpowers/plans/2026-11-25-music-home.md) recommends; after its Task 4 the player
   depends only on its port, and a library of its own is one PR.
