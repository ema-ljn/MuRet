# AR1, the engine apart: detailed plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** the engine (`Core`, `Math`, `Crt`, `Crypto`, `Diagnostics`, `Input`, `Audio`, `Network`, `Data`, `Platform`,
`Pathfinding`, `Network/XorRemap`, `Render`, `Models`) lives in `Client/src/engine/`, one shared library per module (`.dll` on Windows, `.so` on
Linux) except `XorRemap`, which only the executable links, statically, with a switch for a single binary, and no engine file includes `game/` or `app/`, without changing a byte of
behavior.

**Architecture:** first the design (AR1.1): cut the 11 engine includes of the game, give the engine types that live in
game headers a header of their own, merge the two `Ftol` and add the x87 kit, turn FoxAnti into `XorRemap`, a layer
chosen at compile time, and add the rest of the common kit. Then the move (AR1.2): `move.py --root engine` with the first three game modules, one shared
library per engine module with a shared precompiled header, the namespaces renamed after their new folders, and the comments.

**Tech Stack:** C++20, CMake 3.25 (`FILE_SET HEADERS`, `VERIFY_INTERFACE_HEADER_SETS`, `REUSE_FROM`,
`GenerateExportHeader`), GoogleTest,
Python 3 (standard library, `unittest`), GitHub Actions.

**Spec:** [`docs/superpowers/specs/2026-11-20-client-architecture-design.md`](../specs/2026-11-20-client-architecture-design.md),
and the epic in the master plan:
[`2026-11-20-client-architecture-refactor.md`](2026-11-20-client-architecture-refactor.md), "Epic AR1".

Measured on `refactor/client` at 75c33064 (2026-11-22). AR1.3, the command line, already landed in #407.

## Global Constraints

- No PR changes behavior: the behavior golden (`tests/characterization`) and every current test and golden stay
  identical on Linux and Windows, Debug and `math-release`. A difference is a refactor bug, not a golden to regenerate
  (`superpowers:systematic-debugging`).
- The order of the original's calls and of `rand()` calls does not change.
- Titles and branches: `refactor(AR1.1): …` on `refactor/ar1.1-<topic>`; the move is
  `refactor(AR1.2): move the engine to Client/src/engine`, the comments are `docs(AR1.2): trim and translate comments`.
  Never `CLn`. CI runs `check_pure_move.py` on every `type(ARn.m)` PR, so a design PR never renames a file.
- Everything in English. A comment only if strictly necessary, one line; keep the `// 0x…` citations.
- New code calls `RoundedMath.h` / `RoundedTransforms.h`, never `std::sin` or glm trig (`check_rounded_math.py`).
- New scripts: standard library only, `python3 -I`, tests with `unittest`.
- PRs target `refactor/client`. One task per PR, and each PR is one step: Emanuel says yes before the next one starts.
- Priority: fidelity, then `docs/CODING_RULES.md`, then `clean-code`, `clean-architecture` and `refactoring`.
- The cloud container cannot build the client: GitHub CI is the build and the final word.

## Review Focus

- A copy of `F32`, `D`, `Truncate` or `Bits` that looks the same but is not (`static_cast<std::int32_t>(v)` instead of
  `OriginalFtolLow(v).value_or(0)`): replacing it changes out-of-range results. Task 3's script compares normalized
  bodies and its tests pin NaN, infinity and 2^40.
- `IsKalimaMap` includes Kalima 7 (map 36) but most copies check only 24 to 29: those must keep 24 to 29. Task 5 pins
  36 and 30 to 35 in its tests.
- A sound registered while effects are off, or an id the catalog does not know: the engine must keep ignoring both once
  the catalog comes from the game. Task 1 tests `SdlAudio` with a recording catalog.
- An `.ini` that still has `[Server] Obfuscation`: the player gets an error with its line that says how to build without
  the `XorRemap` layer, never a silent default. Task 4 tests it.
- A design PR whose deleted and created files git pairs as a rename (`-M50%`): CI's pure-move check fails it. Each task
  that deletes a file checks `git diff -M50% --name-status` shows no `R` line before pushing.

---

## How this plan places files

- **Existing files change folder only in AR1.2**, by `move.py`, from their row in
  `Client/docs/research/coverage/module-map.tsv`. In AR1.1 a file whose module changes gets its `target` cell changed,
  not a `git mv`.
- **A new file is born in the folder it will keep inside its root** (`Crt/CrtRandom.h`, `Math/X87.h`, `Render/Frame/`),
  with that folder's namespace, and its row in the map in the same PR (`confidence` `high`, `why` ending in `(AR1.1)`).
  New folders also get their glob in `Client/modules.tsv` `folders_today`: `Crt/**` for `engine/Crt`, `Math/**` for
  `engine/Math`, `Maps/**` for `game/Maps`, `Assets/**` for `game/Assets`.
- **Existing files keep their namespace until Task 9**, which renames every moved file's namespace after its new folder
  (spec, "Namespaces": `Client::<Folder>`, in the design PR, not in the move).

## Deviations from the master plan, and why

| master plan | this plan | why |
| --- | --- | --- |
| `App/WindowState`, `VSyncMode`, `Resolution` and the frame contracts move in AR1.1 | their map rows already say `engine/Platform`; the frame contracts' rows change to `engine/Render/Frame`; `move.py` moves them in AR1.2 | a hand move in a design PR fails CI's pure-move check, and `src/Platform/*.cpp` goes to `ClientPlatform`, which `ClientLib` does not link |
| `IPlayback::Register(SoundDefinition)` | `SdlAudio::Create` takes an `Audio::ISoundCatalog` (engine) that the game's catalog implements (`GameSoundCatalog()`); `IPlayback::RegisterSound(id, path)` stays | found while doing Task 1: the doubles that record `RegisterSound` (fauna, spearman) assert the path their caller passed, which `Register(SoundDefinition)` would replace with the catalog's; with a port the engine owns, no caller or double changes |
| `FoxAnti` in the names (`FoxAntiObfuscation`, `MURET_FOXANTI`, `MuRetFoxAnti`, `engine/Network/FoxAnti`) | `XorRemap` (`XorRemapObfuscation`, `MU_XOR_REMAP`, `XorRemap`, `engine/Network/XorRemap`) | Emanuel, 2026-11-22: not a DLL any more but a layer named after what it does |
| `LabFoxAntiTables()` defined in `MakeObfuscationFoxAnti.cpp` | `constexpr` in `App/LabXorRemapTables.h` | the layer's tests build the lab tables too, and they do not link the executable's sources |
| `Core/WrappingInt.h` and `UI::Rect::Contains` in the kit | left out | no copy of a wrapping integer was found, and `Rect::Contains` already exists (`UI/Widget.h`) |
| the 189 loose map comparisons switch to names | `MapId.h` replaces the 77 duplicated constants and the predicates (Task 5) and names every single map (Task 5d); the loose literals switch in each module's AR4 design PR, the fauna's first (Task 5e) | each literal needs a judgment (is it a map?); AR4 reads those files anyway. Emanuel, 2026-11-25: the single maps get their names now, and `Birds.cpp` and `Fish.cpp` go first |
| map enum | `inline constexpr std::int32_t` constants in AR1; AR6 turns them into an opaque `enum class MapId : std::int32_t {}` with the same names | the size of the change, not casts: estimated from the types review (2026-11-23), about 228 parameters and 30 fields to retype and about 182 literal uses in `Client/src` and 506 in the tests to inspect (not all of them stop compiling, see AR6's inventory), so the type changes once AR4 has named the literals |
| one static target per module | one shared library per engine module, `MU_MONOLITHIC` for a single binary (ADR in Task 6) | Emanuel, 2026-11-22: DLLs on Windows and the same on Linux, as large engines do (Unreal, O3DE, Source) |
| `MuRet<Module>` targets (`MuRetCore`…) | no prefix: `Core.dll`, `Audio.dll`…, the executable `main.exe` | Emanuel, 2026-11-22: like the original's `main.exe` |
| `MuRetTables`, `MuRetMaps`, `MuRetSound` targets in AR1.2 | the three game modules move in AR1.2 but stay in `ClientLib` | the game's library shape (one `Game` or a few) is the ADR's AR4 question |
| `MURET_`/`muret_` options, functions, macros and environment variables | `MU_`/`mu_`, and `MU_CLIENT_` for the client's own (Task 1b) | Emanuel, 2026-11-23: MuRet was a mistaken name; `cmake/` is shared with the DataServer, so the prefix names the game, not the client |
| `MuRetFoxAnti`, an optional library inside Network | `XorRemap`, a static target that only `main.exe` links, never a DLL (Task 6) | Emanuel, 2026-11-23: easy to remove, but not shipped as a DLL that says what it is |
| the non-item models get their names in AR4.1 | the fauna's models get theirs now, in the home AR4.1 will fill (Task 5f) | Emanuel, 2026-11-25, reviewing Task 5e's PR: the code has to say what `type` is and what `0xad` means |
| nothing on named values | the engine stops naming game values (Task 5b) and the shared float numbers get one home (Task 5c) | Emanuel, 2026-11-23: one home per space of named values (CODING_RULES §7, "Named values"; the [named-values plan](2026-11-23-named-values.md), Tasks 3 and 4) |

---

## AR1.1 (design): eleven PRs

### Task 1: the engine stops including the game

PR `refactor(AR1.1): stop including the game from the engine`, branch `refactor/ar1.1-engine-includes`. Skills:
`clean-architecture`, `cpp-pro`, `superpowers:test-driven-development`.

Today's 11 engine rows of `layers-baseline.tsv`: `Network/Crypto/Xor32.{h,cpp}` → `Network/Packet.h`;
`Platform/SdlAudio.cpp` → `Audio/SoundCatalog.h`; `Render/Gl/GlFrameTarget.{h,cpp}` → `Scenes/{SceneCanvas,SceneText,
IFrameTarget,IWorldView}.h` (6); `Render/Gl/GlScreenReader.h` → `Scenes/Screenshots.h`;
`Render/ModelBatchParticleBackend.cpp` → `Scenes/CharacterDrawing.h`.

**Files:**
- Modify: `Client/tools/architecture/check_layers.py`, `test_check_layers.py`, `.github/workflows/ci.yml` (step "Client
  layers": add `--clean-root engine`), `.claude/skills/muret-module/SKILL.md` (one line on the flag)
- Create: `Client/src/Render/Frame/SceneWorld.h`, `Client/src/Render/Frame/IScreenReader.h`
- Modify: `Scenes/SceneWorlds.h`, `Scenes/IWorldView.h`, `Scenes/Screenshots.h`, `Render/Gl/GlScreenReader.h`
- Map rows to `engine/Render/Frame`: `Scenes/ITextDrawer.h`, `IFrameTarget.h`, `IWorldView.h`, `SceneCanvas.h`,
  `SceneText.{h,cpp}`, `TextCacheDrawer.{h,cpp}`
- Create: `Client/src/Render/ITextureSlots.h`, `Client/src/Render/Models/IModelLibrary.h`
- Modify: `Scenes/CharacterDrawing.h`, `Render/ModelBatchParticleBackend.{h,cpp}`, the users of `Scenes::IModelLibrary`
  (141 files in `src` and `tests`); map row of `Scenes/TextureMetadata.h` to `engine/Models`
- Create: `Client/src/Network/Xor32Packet.{h,cpp}`; modify `Network/Crypto/Xor32.{h,cpp}` and the about 80 callers of
  the `Packet` overloads
- Create: `Client/src/Audio/SoundDefinition.h`, `Client/src/Audio/ISoundCatalog.h`; modify `Audio/SoundCatalog.{h,cpp}`,
  `Platform/SdlAudio.{h,cpp}`, `main.cpp`, `tests/audio/test_sound_catalog.cpp`, `tests/platform/test_sdl_audio.cpp`
- Regenerate: `Client/tools/architecture/layers-baseline.tsv`

**Interfaces:**
- Produces: `check_layers.py --clean-root ROOT` (repeatable). `clean_root_violations(found, roots: tuple[str, ...]) ->
  list[tuple[str, str, str, str]]`: the violations whose source module is in one of `roots`. They fail even when listed
  in the baseline, and `--write-baseline` refuses to write them (exit 1, listing them).
- Produces: `Client::Render::Frame::SceneWorld { std::uint8_t map = 0; }` (moved out of `SceneWorlds.h`, which includes
  it); `IWorldView::Draw(Render::Frame::SceneWorld, const Render::Pass3D&, float)`.
- Produces: `Client::Render::Frame::IScreenReader` with `ReadTheScreen(App::PixelSize) -> std::vector<std::uint8_t>`
  (moved out of `Screenshots.h`, which includes it).
- Produces: `Client::Render::ITextureSlots` with `virtual std::optional<TextureSlot> TextureSlotOf(std::uint32_t
  selector) const = 0`. `ModelBatchParticleBackend` takes `std::reference_wrapper<const Render::ITextureSlots>`.
- Produces: `Client::Render::Models::IModelLibrary : public Render::ITextureSlots`, the class as it is in
  `CharacterDrawing.h` today; its `TextureSlotOf` keeps the default body that returns `std::nullopt`.
- Produces: `Client::Network::Xor32Encode(Packet) -> Packet` and `Xor32Decode(Packet) -> Packet`. `Network::Crypto`
  keeps only `Xor32Key`, `Xor32EncodeBytes` and `Xor32DecodeBytes`, and `Xor32.h` no longer includes `Network/Packet.h`.
- Produces: `Client::Audio::SoundDefinition` (fields as today: `id`, `path`, `voices`, `positional`) in
  `Audio/SoundDefinition.h`; `Client::Audio::ISoundCatalog` in `Audio/ISoundCatalog.h` with `Find(std::int32_t id,
  std::string_view path) const -> std::optional<SoundDefinition>` and `StartupSounds() const ->
  std::span<const SoundDefinition>`; `const ISoundCatalog& GameSoundCatalog()` in `SoundCatalog.h`;
  `SdlAudio::Create(dataRoot, const Audio::ISoundCatalog& catalog, effectsEnabled, musicEnabled)`.

- [ ] **Step 1: Write the failing check test** in `test_check_layers.py`:

```python
def test_a_clean_root_violation_fails_even_in_the_baseline(self):
    # engine/Render -> game/Scenes listed in the baseline still fails under --clean-root engine
    self.assertEqual(self.run_check(baseline=[ROW], clean_roots=['engine']), 1)

def test_write_baseline_refuses_a_clean_root_violation(self):
    self.assertEqual(self.run_check(write_baseline=True, clean_roots=['engine']), 1)
    self.assertNotIn(ROW, self.read_baseline())
```

`ROW` is an `engine/Render` file including a `game/Scenes` one; follow the scratch-tree fixtures the file already uses.

- [ ] **Step 2: Run it and see it fail:** `python3 -I -m unittest Client/tools/architecture/test_check_layers.py -v`,
  failing with "unrecognized arguments: --clean-root".
- [ ] **Step 3: Implement `--clean-root` and `clean_root_violations`.** Run the tests: pass.
- [ ] **Step 4: Write the failing sound tests.** In `tests/audio/test_sound_catalog.cpp`:

```cpp
TEST(SoundCatalogTest, GameCatalogFindsWhatFindSoundFinds)      // 0x146 with "Sound\mShadowAttack2.wav"; unknown path and id: none
TEST(SoundCatalogTest, GameCatalogStartsTheNativeStartupSounds) // same span as StartupSounds()
```

  In `tests/platform/test_sdl_audio.cpp`, with a catalog double that records each `Find` and knows one sound:
  `PlaysOnlySoundsTheInjectedCatalogKnows` (disk driver: the known sound is audible, another id is silent, lookups
  `{7, 8}`) and `IgnoresRegistrationsWhileEffectsAreOff` (no lookup). They fail to compile (`ISoundCatalog` does not
  exist).
- [ ] **Step 5: Inject the catalog.** `SdlAudio::Create` takes the catalog and keeps its `StartupSounds()` loop over
  `catalog.StartupSounds()`; `RegisterSound` keeps its body with `catalog.Find(id, path)` in place of `FindSound`, and
  `SdlAudio` stops including `Audio/SoundCatalog.h`. `main.cpp` passes `Audio::GameSoundCatalog()`. Run the audio
  tests; the platform ones run in CI.
- [ ] **Step 6: Split the frame contracts and `IModelLibrary`, and move the `Packet` overloads,** per Interfaces. A file
  that includes `Scenes/CharacterDrawing.h` and uses no other name it declares includes
  `Render/Models/IModelLibrary.h` instead (a script lists them; it goes in the PR description, not in the repo).
- [ ] **Step 7: Retarget the map rows** listed in Files, and regenerate the baseline:
  `python3 -I Client/tools/architecture/check_layers.py --src Client/src --modules Client/modules.tsv --map
  Client/docs/research/coverage/module-map.tsv --baseline Client/tools/architecture/layers-baseline.tsv
  --write-baseline`. The baseline diff only removes rows, among them the 11 engine ones; then the same command without
  `--write-baseline` and with `--clean-root engine` prints `0 new`.
- [ ] **Step 8: Push, open the PR and let CI build.** Expected: all jobs green, the characterization tests unchanged.

### Task 1b: the `MU_` prefix

PR `refactor(AR1.1): rename the MURET_ prefix to MU_`, branch `refactor/ar1.1-mu-prefix`. Before Task 2, so Tasks 4
and 8 create their options and functions with the new prefix. A rename only: no behavior, no file moves.

- **The rule** (Emanuel, 2026-11-23): `MURET_CLIENT_` becomes `MU_CLIENT_`, every other `MURET_` becomes `MU_`, and
  `muret_` becomes `mu_` (`muret_set_warnings` → `mu_set_warnings`, `muret_llvm_libc` → `mu_llvm_libc`).
- **Where:** the CMake files and `CMakePresets.json` (Server/DataServer and labs/ too), the workflows, sources and tests
  (macros, compile definitions, environment variables), the research scripts (`MURET_MAIN_EXE` and the capture
  variables), and the live docs: `AGENTS.md`, `CLAUDE.md`, the READMEs, `docs/CODING_RULES.md`, the skills,
  `Client/modules.tsv`. Dated documents (plans, specs, ADRs, audits, `docs/learning/`, the research notes) keep the
  names they had, as history.
- **Out of scope:** the CMake project name `MuRet`, the repository and the skill names (`muret-module`…).
- **Check:** `git grep -n -E "\bMURET_|\bmuret_"` lists only dated documents; CI green with the golden unchanged. The
  PR lists the environment variables to rename on Emanuel's machines and the VM, and says that an existing build folder
  needs a fresh configure, since the cache variables change name.

### Task 2: `CrtRandom` and `PixelSize` get their own headers

PR `refactor(AR1.1): give CrtRandom and PixelSize their own headers`, branch `refactor/ar1.1-crt-random`. Skills:
`refactoring` (Move Function), `cpp-pro`. No new behavior: the existing tests and the golden are the net.

**Files:**
- Create: `Client/src/Crt/CrtRandom.h` (module `engine/Crt`), `Client/src/Core/PixelSize.h`
- Modify: `Render/Models/Cloth.h` (includes `Crt/CrtRandom.h`, uses `Crt::CrtRandom`), `App/WindowState.h` (includes
  `Core/PixelSize.h`), and their users

**Interfaces:**
- Produces: `Client::Crt::CrtRandom`, the class of `Cloth.h` lines 24 to 44 as it is.
- Produces: `Client::Core::PixelSize { int width = 0; int height = 0; }`.

- [ ] **Step 1: Create both headers** and include them from `Cloth.h` and `WindowState.h`.
- [ ] **Step 2: Rename the uses by script:** `Render::Models::CrtRandom` → `Crt::CrtRandom` (about 1,650 uses and 233
  `using` declarations) and `App::PixelSize` → `Core::PixelSize` (about 460 uses). A file that includes `Cloth.h` and
  uses no cloth name (today about 440 of its 556 includers) includes `Crt/CrtRandom.h` instead. The compiler finds the
  unqualified uses the script leaves.
- [ ] **Step 3: Check** `grep -rn "Render::Models::CrtRandom\|App::PixelSize" Client/src Client/tests` prints nothing,
  `check_layers.py` prints `0 new`, then push and let CI build.

### Task 3: one `Ftol` and the x87 kit

PR `refactor(AR1.1): merge the two Ftol and share the x87 helpers`, branch `refactor/ar1.1-x87-kit`. Skills:
`refactoring`, `superpowers:test-driven-development`.

**Files:**
- Modify: `World/Ftol.h` (gains `OriginalFtolLow`; stays in place, its row already says `engine/Math`)
- Delete: `World/OriginalFtol.h`, `World/OriginalFtol.cpp`
- Create: `Client/src/Math/X87.h`; `tests/world/test_x87.cpp`
- Modify: `tests/world/test_ftol.cpp`, the 84 includers of `World/OriginalFtol.h`, and the files whose copies the script
  replaces

**Interfaces:**
- Produces, in `World/Ftol.h` (namespace `Client::World` until Task 9): `std::optional<std::int32_t>
  OriginalFtolLow(double)` (today's body, inline) and `std::int32_t FtolLow(double)` defined as
  `OriginalFtolLow(value).value_or(0)`. Both give today's results: NaN is 0; infinity and |x| ≥ 2^63 are `nullopt` and
  0; otherwise the low dword of the truncation.
- Produces, in `Math/X87.h` (namespace `Client::Math`): `constexpr float F32(double)`, `constexpr double D(float)`,
  `std::int32_t Truncate(double)` (calls `World::FtolLow`), `std::uint32_t Bits(float)`.

- [ ] **Step 1: Write the failing tests.** In `test_ftol.cpp`, `FtolLow` and `OriginalFtolLow` agree for NaN (0 and 0),
  ±infinity (0 and `nullopt`), 0x1p63 (0 and `nullopt`), 0x1p40 + 5 (5 and 5), -1.5 (-1 and -1). In `test_x87.cpp`:

```cpp
TEST(X87Test, TruncateIsFtolLow)   // NaN, inf, 0x1p40 + 5, -1.5, 2147483648.0 → 0, 0, 5, -1, INT32_MIN
TEST(X87Test, F32RoundsLikeTheStore)   // F32(0.1) == 0.1f; F32(D(0.1f)) == 0.1f
TEST(X87Test, BitsIsTheFloatPattern)   // Bits(1.0f) == 0x3F800000u; Bits(-0.0f) == 0x80000000u
```

- [ ] **Step 2: Run them and see them fail** (no `Math/X87.h`, no inline `OriginalFtolLow` in `Ftol.h`).
- [ ] **Step 3: Merge and create** per Interfaces; delete `OriginalFtol.{h,cpp}` and point their includers to
  `World/Ftol.h`. Run the tests: pass.
- [ ] **Step 4: Replace the identical copies by script.** A copy goes only if its body, with comments and whitespace
  removed and the parameter renamed, is exactly the shared one: today 158 `F32(double)`, 63 `D(float)`, 21
  `Bits(float)`, 17 `Truncate(double)` and 2 `Truncate(float)` in the `OriginalFtolLow(…).value_or(0)` form. The six
  `Truncate(double)` that are `static_cast<std::int32_t>(v)` stay. Each file gets `#include "Math/X87.h"` and a
  `using Math::F32;` (one per name it used) in the `.cpp`'s anonymous namespace, or `Math::` qualifiers in a header. The
  script prints the list and the counts; they go in the PR description.
- [ ] **Step 5: Check** `git diff -M50% --name-status origin/refactor/client` has no `R` line, `check_rounded_math.py`
  passes, then push and let CI build.

### Task 4: the XOR and remap layer, optional at compile time

PR `refactor(AR1.1): make the XOR and remap layer a compile-time option`, branch `refactor/ar1.1-xor-remap-option`.
Skills: `cpp-pro`, `clean-architecture` (composition root), `superpowers:test-driven-development`.

**The name** (Emanuel, 2026-11-22: not a DLL any more but a layer of our own, easy to take out, named after what it
does). It XORs every byte that comes in and remaps opcodes, and around that it rebuilds the server list, the server
code of the selection and the F3 11 head, so our names say `XorRemap`: `XorRemapObfuscation` next to `IObfuscation` and
`IdentityObfuscation`, `XorRemapTables`, the option `MU_XOR_REMAP`, the target `XorRemap` and the module
`engine/Network/XorRemap` (its row in `Client/modules.tsv` and the map targets change in this PR). Only the citations keep
`FoxAnti.dll`, because that is the original file they cite, and so does `tests/vectors/net/tools/foxanti_cases.c`,
which drives the research's port of that DLL.

**Two PRs, because the files change name too** (Emanuel, 2026-11-22: if the name changes, rename the files). A file
renamed and edited in the same PR fails CI's pure-move check, so the rename goes first, on its own:
`refactor(AR1.1): rename the FoxAnti files to XorRemap`, branch `refactor/ar1.1-xor-remap-rename`. It runs `git mv` on
`Network/FoxAntiObfuscation.{h,cpp}` to `Network/XorRemapObfuscation.{h,cpp}`, `tests/network/test_fox_anti_obfuscation.cpp`
to `test_xor_remap_obfuscation.cpp` and `test_golden_fox_anti_obfuscation.cpp` to `test_golden_xor_remap_obfuscation.cpp`,
and changes only include lines, the map's `file` column and the build paths; `check_pure_move.py` passes. The design PR
below starts from it, and every file it creates is born with the new name.

**Files:**
- Modify: `Network/XorRemapObfuscation.{h,cpp}` (the class becomes `XorRemapObfuscation` and takes `XorRemapTables`),
  `Network/IObfuscation.h` (comment only if it names the mode)
- Create: `Client/src/Network/XorRemapTables.h` (map row `engine/Network/XorRemap`), `Client/src/App/MakeObfuscation.h`,
  `App/MakeObfuscationXorRemap.cpp`, `App/MakeObfuscationNone.cpp`, `App/LabXorRemapTables.h` (rows `app/App`)
- Delete: `App/ObfuscationMode.h`
- Modify: `App/Config.{h,cpp}` (no `obfuscation`, no `ParseObfuscation`), `main.cpp` (no `ChooseObfuscation`),
  `Client/CMakeLists.txt`, `tests/CMakeLists.txt` and `tests/{app,network,protocol,scenes}/CMakeLists.txt`,
  `tests/app/test_config.cpp`, the 12 test files that build the layer, `Client/modules.tsv`, the map,
  `.github/workflows/ci.yml`, `Client/README.md`

**Interfaces:**
- Produces: `struct Client::Network::XorRemapTables` with `std::byte inboundXor`; `std::array<OpcodeRemap, 3>
  encryptedRemaps`; `std::array<OpcodeRemap, 2> clearRemaps`; `std::uint8_t serverOpcode, labServerListSub,
  serverListSub, serverSelectSub, characterOpcode, skillListSub`; `std::uint32_t codeGroupSize`;
  `std::array<std::uint16_t, 9> listCodeDeltas`; `std::uint16_t ladderStepCodes`; `std::array<LadderStep, 8>
  selectLadder`. `OpcodeRemap {from, to}` and `LadderStep {first, subtract}` move there from the `.cpp`. Offsets and
  sizes of the packets (`ListSubIndex`, `ServerSelectSize`…) are the mechanism and stay in the `.cpp`.
- Produces: `explicit XorRemapObfuscation(const XorRemapTables&)`.
- Produces: `constexpr Network::XorRemapTables Client::App::LabXorRemapTables()` with today's values (0xAA;
  {0x10, 0x1D}, {0x19, 0xA5}, {0x1E, 0xA6}; {0xD3, 0xA3}, {0xD7, 0xA4}; F4, 02, 06, 03, F3, 11; 11; 0x190 … 0xA0;
  0x14; the 8 ladder steps).
- Produces: `std::unique_ptr<const Network::IObfuscation> Client::App::MakeObfuscation()`. CMake option
  `MU_XOR_REMAP` (default `ON`) sets `MU_MAKE_OBFUSCATION_SOURCE`, which `Main` and `test_make_obfuscation`
  compile: `ON` is `MakeObfuscationXorRemap.cpp`, and both link `XorRemap`, a static library with
  `Network/XorRemapObfuscation.cpp` that links `ClientLib`; `OFF` is `MakeObfuscationNone.cpp`, and no source of the
  layer is built. `ClientLib`'s glob excludes `Network/XorRemapObfuscation.cpp` and `App/MakeObfuscation*.cpp`.
- Produces: `mu_add_module_tests(XOR_REMAP <file> …)`: those files build only with `MU_XOR_REMAP`, link
  `XorRemap`, and count as registered either way (like `OTHER_SYSTEM`).

- [ ] **Step 1: Write the failing tests.** In `test_config.cpp`:

```cpp
TEST(ConfigTest, RejectsTheRemovedObfuscationKey)
{
    const auto error = ErrorOf("[Server]\nObfuscation = foxanti\n");
    EXPECT_EQ(error.line, 2u);
    EXPECT_NE(error.message.find("MU_XOR_REMAP"), std::string::npos);
}
```

  In `test_xor_remap_obfuscation.cpp`: `XorRemapObfuscation(LabXorRemapTables())` passes every current case, and
  `TEST(XorRemapObfuscationTest, UsesTheInjectedInboundXor)` builds tables with `inboundXor = std::byte{0x55}` and
  expects `InboundMask` to return 0x55. A new `tests/app/test_make_obfuscation.cpp` expects `InboundMask(std::byte{0xC1})`
  to be 0xAA with the option on and 0 with it off (one file, compiled with `MU_MAKE_OBFUSCATION_SOURCE`; the
  expectation comes from a definition CMake passes).
- [ ] **Step 2: Run them and see them fail** (the key is still known; no constructor with tables).
- [ ] **Step 3: Rename, inject the tables and add `MakeObfuscation`.** `main.cpp` keeps the result next to the
  connection (where `Online` lives) and passes `const Network::IObfuscation&` to `Online`. The config's unknown-key path
  gets a special message for `[Server] Obfuscation`: `Obfuscation no longer exists: the server's packet shape is chosen
  when building (-DMU_XOR_REMAP=OFF for a server that speaks like main.exe)`.
- [ ] **Step 4: Split the tests.** A file where every test needs the layer goes whole into its folder's `XOR_REMAP`
  list; otherwise those tests move to `<file>_xor_remap.cpp` (new file, so no rename) and that file goes in the list.
- [ ] **Step 5: CI and docs.** The job "Windows (MSVC, Release, matemática)" adds `-DMU_XOR_REMAP=OFF` to its
  configure. `Client/README.md` drops the key from the `.ini` table and example and says how to build without the layer.
- [ ] **Step 6: Check** `grep -rn "FoxAnti" Client/src Client/tests` shows it only in citations of `FoxAnti.dll` and in
  `foxanti_cases.c`, no file outside the layer, `App/MakeObfuscationXorRemap.cpp`,
  `App/LabXorRemapTables.h` and the `XOR_REMAP` tests names `XorRemap`, the `git diff -M50%` check, then push.
  Expected: CI green, and the Windows Release job builds and links `Main` without the layer.

### Task 5: the rest of the common kit

PR `refactor(AR1.1): add the common kit`, branch `refactor/ar1.1-common-kit`. Skills: `cpp-coding-standards`,
`superpowers:test-driven-development`.

**Files:**
- Modify: `Core/Expected.h`, `tests/core/test_expected.cpp`
- Create: `Client/src/Core/Id.h` (module `engine/Core`), `tests/core/test_id.cpp`; a `PrintTo` for `Core::Id` in
  `tests/support/`
- Modify: `Data/Text/TextTable.h`, `tests/data/test_text_table.cpp`; `UI/ChatCommands.cpp`, `UI/Emotes.cpp`
- Create: `Client/src/Maps/MapId.h` (module `game/Maps`); `tests/world/test_map_id.cpp`
- Modify: the files that define `First/Last{BloodCastle,ChaosCastle,Kalima}Map` as `std::int32_t` (30 in `Scenes`, 2 in
  `UI`, 2 in `World`), the copies of `InChaosCastle`, `IsChaosCastleMap`, `IsChaosCastle`, `InBloodCastle`, and
  `World/KalimaWorld.{h,cpp}` (`IsKalimaMap` moves out)

**Interfaces:**
- Produces, on `Core::Expected<T, E>` and `Expected<void, E>`, `const&` and `&&` overloads, with the `std::expected`
  meaning: `AndThen(F)` (F takes the value, or nothing for `void`, and returns an `Expected<U, E>`), `Transform(F)`
  (returns `Expected<U, E>` with `F`'s result), `OrElse(F)` (F takes the error and returns an `Expected<T, G>`). No
  caller changes in AR1: each module adopts them in its own design PR.
- Produces, in `Core/Expected.h`: `template <typename T> using Result = Expected<T, std::string>;` and
  `using Status = Result<void>;` (CODING_RULES §7, "Types and aliases"). No caller changes in AR1.
- Produces, in `Core/Id.h`: `template <class Tag, IdRepresentation Rep> class Id`, where the concept
  `IdRepresentation` accepts exactly `std::int8_t`, `std::uint8_t`, `std::int16_t`, `std::uint16_t`, `std::int32_t`
  and `std::uint32_t`, and nothing else (not `bool`, plain `char` or a cv-qualified type). That list is the first
  scope: `std::uint64_t` joins it when `World::WorldObjectIdentity` migrates. It has `constexpr explicit Id(Rep)`,
  `[[nodiscard]] constexpr Rep Value() const noexcept` and a defaulted `operator==`, and nothing else: no default
  constructor (whether a value means "none" is each domain's decision: texture slot 0 is a slot, world object identity
  0 is none), no ordering and no `std::hash` until a consumer needs them, no arithmetic and no implicit conversion
  either way. It separates domains; it does not promise a wire layout or a calling convention. No user in AR1: modules
  adopt it in their design PRs.
- Produces: `std::string_view Client::Data::Text::TextOr(const TextTable& texts, std::size_t index, std::string_view
  fallback = {})`, `texts.Text(index).value_or(fallback)`. It replaces the `TextAt` of `ChatCommands.cpp` and
  `Emotes.cpp`, the only identical copies.
- Produces, in `Client::Maps`: `inline constexpr std::int32_t FirstBloodCastleMap = 11, LastBloodCastleMap = 17,
  FirstChaosCastleMap = 18, LastChaosCastleMap = 23, FirstKalimaMap = 24, LastKalimaMap = 29, SeventhKalimaMap = 36`;
  `constexpr bool IsBloodCastle(std::int32_t)`, `IsChaosCastle(std::int32_t)`, `IsKalimaMap(std::int32_t)` (24 to 29,
  or 36: today's `KalimaWorld.cpp` body).

- [ ] **Step 1: Write the failing tests.** `test_expected.cpp`: `AndThen` runs only on a value and passes an error
  through untouched; `Transform` maps the value and keeps the error; `OrElse` runs only on an error; each for `void`
  too, and a move-only value goes through `&&` without a copy; `static_assert`s that `Status` is
  `Expected<void, std::string>` and `Result<int>` is `Expected<int, std::string>`. `test_id.cpp`: two ids compare equal
  exactly when their values do, and `Value()` returns what was given; `static_assert`s that the tested instantiations
  have `sizeof(Rep)` and are trivially copyable, that an `Id` is not default-constructible, that it does not convert
  from or to `Rep` implicitly, that two `Id`s with different tags do not convert into each other, that braces from a
  non-constant `std::int32_t` do not build an `Id<Tag, std::uint8_t>` (through a concept, since a narrowing outside a
  template is a hard error), and that `IdRepresentation<bool>`, `<char>` and `<const std::int32_t>` are false. `test_map_id.cpp`:

```cpp
EXPECT_FALSE(IsBloodCastle(10)); EXPECT_TRUE(IsBloodCastle(11)); EXPECT_TRUE(IsBloodCastle(17)); EXPECT_FALSE(IsBloodCastle(18));
EXPECT_FALSE(IsChaosCastle(17)); EXPECT_TRUE(IsChaosCastle(18)); EXPECT_TRUE(IsChaosCastle(23)); EXPECT_FALSE(IsChaosCastle(24));
EXPECT_TRUE(IsKalimaMap(24)); EXPECT_TRUE(IsKalimaMap(29)); EXPECT_FALSE(IsKalimaMap(30)); EXPECT_FALSE(IsKalimaMap(35)); EXPECT_TRUE(IsKalimaMap(36));
```

  and `TextOr` returns the text, and the fallback for an empty index.
- [ ] **Step 2: Run them and see them fail.**
- [ ] **Step 3: Implement** per Interfaces. Run the tests: pass.
- [ ] **Step 4: Replace the copies by script**, only the `std::int32_t` definitions with these exact values and the
  predicates with the same body (the two `std::uint8_t` Blood Castle copies stay). A check that compares 24 to 29 keeps
  `FirstKalimaMap`/`LastKalimaMap`; only a check that also takes 36 becomes `IsKalimaMap`.
- [ ] **Step 5: Check** `grep -rn "constexpr std::int32_t \(First\|Last\)\(ChaosCastle\|BloodCastle\|Kalima\)Map"
  Client/src` prints only `Maps/MapId.h`, the `git diff -M50%` check, then push and let CI build.
- [ ] **Step 6:** `Maps/MapId.h` is the `map` space's home: its row in `Client/tools/architecture/named-values.tsv`
  switches from `watch` to `single-definition` and the baseline loses the rows this PR replaced, in the same diff.

### Task 5b: the engine stops naming game values

PR `refactor(AR1.1): pass the game values the engine compared as data`, branch `refactor/ar1.1-engine-game-values`.
After the [named-values plan](2026-11-23-named-values.md)'s Task 2 (`check_named_values.py`) and before the move.
Details: that plan's Task 3.

Four engine sites compare a game value. The comparison keeps the original's order, and the value or the decision
comes from the game:

| site | today | after |
| --- | --- | --- |
| `Render/Models/Cloth.cpp:60,517` | `LoginMap = 0x37`; `else if (map == LoginMap)` chooses the wind (`0x4551f4`) | the cloth update takes the map it compares, or the login-map flag, from the game; the `rand()` call stays where it is |
| `Render/Models/Skinning.cpp:157-163` | `ShadowSpreadOf(map)` with `CastleSiegeMap = 0x1e` | `ShadowSpreadOf` moves to `game/Actors`, beside its callers in `Scenes/CharacterDrawing.cpp`; the engine takes the spread |
| `Render/Models/Animation.cpp:23-24,92-95` | `WingKeysPerOriginalFrame(action)` (`0x50ba30`) with `FastWingAction` `0x1e` and `0x1f` | moves to `game/Actors`, beside its callers (`Scenes/CharacterAnimation.cpp`, `Scenes/GameHanging.cpp`); the engine takes the keys per frame |
| `Audio/Music.cpp`: `WorldTracks`, `StopWorld`, `StepWorld` | the 22 track names of MU and the choice of track by map, with the map ids as bare literals | not done by PR 439; the [music plan](2026-11-25-music-home.md) does it in its Tasks 3 to 5, before AR1.2's move: the list and the two functions go to a module of their own, `game/Music` (layer 8), over the engine's `Audio::MusicPlayer` and in the same order; called from `Scenes/WinMainLoop.cpp` and `Scenes/ReturnToSelection.cpp`. Crywolf's part left first: [Crywolf plan](2026-11-24-crywolf-homes.md), Task 2 (PR 447) |

Each moved function keeps its tests, and a test compares the old and the new result over every map id and every
action (0 to 255). Done when `check_named_values.py --clean-root engine` lists only what other tasks own: the FoxAnti
opcodes (`Network/FoxAntiObfuscation.cpp:51,101`, AR1 Task 4) and `PlayerSkeletonModel`, `PlayerShadowModel` and
`FirstItemModel` in `Data/Models/PlayerModels.h` (the item plan's Task 10). The PR that removes the last of them adds
`--clean-root engine` to CI's "Client named values" step (#439 left it out for that reason).

### Task 5c: the numbers the x87 code shares

PR `refactor(AR1.1): give the shared float numbers one home`, branch `refactor/ar1.1-float-constants`. After Task 5.
Details: the [named-values plan](2026-11-23-named-values.md)'s Task 4.

`Client/src/Math/FloatConstants.h` and `Client/src/Math/Angles.h` (`engine/Math`, `Client::Math`), with numeric names
only (`Tenth`, `Half`, `Hundredth`, `FullTurn`, the degree-to-radian factors) and **one name per representation**: the
plain name is the number in the type the original keeps it in (`Hundredth`, a `float` from its bit pattern, as
`Render/Models/Cloth.cpp` uses it; `FullTurn`, the integer 360) and a suffix names another type (`HundredthAsDouble`,
the `double` widened from that `float`, as `Scenes/CameraTour.cpp` uses it; `FullTurnAsFloat`, `FullTurnAsDouble`).
The two degree-to-radian factors that differ in the last bit stay two names: `DegreesToRadians` (`0x3c8efa35`, in
`Render/Models/OriginalRotation.cpp`) and `DegreesToRadiansRoundedUp` (`0x3c8efa36`, in `Render/Models/Pose.cpp`).

A script replaces a private definition only when its declared type and its initializer give the home's value bit for
bit in that type, whatever the spelling; the compiler checks every replaced shape with a `static_assert`. A copy with
the home's name and type becomes a using-declaration in the same place; any other keeps its name and its type and
takes the home's name as its initializer. Role names (`Fade`, `Growth`) are not touched, nor a numeric name with
another value. Gate: the behavior golden in Debug and `math-release` on Linux and Windows, and the reference gives the
same numbers. The registry's `number` space becomes `home-only`.

### Task 5d: every map has its name

PR `refactor(AR1.1): name every map in Maps/MapId.h`, branch `refactor/ar1.1-map-names`. After Task 5 (Emanuel,
2026-11-25, reviewing Task 5's PR: the numeric maps get their names in one header).

`Maps/MapId.h` lists every map the client knows, as `inline constexpr std::int32_t` beside the range bounds:
`LorenciaMap` 0, `DungeonMap` 1, `DeviasMap` 2, `NoriaMap` 3, `LostTowerMap` 4, `ExileMap` 5, `ArenaMap` 6,
`AtlansMap` 7, `TarkanMap` 8, `DevilSquareMap` 9, `IcarusMap` 10, `CastleSiegeMap` 30, `LandOfTrialsMap` 31,
`SecondDevilSquareMap` 32 (the loader turns it into 9), `AidaMap` 33, `CrywolfMap` 34, `SecondCrywolfMap` 35,
`CharacterSelectMap` 54 and `LoginMap` 55. `tests/world/test_map_id.cpp` writes each number. `Maps/ClothWind.h` drops
its own `LoginMap`.

A script replaces the private definitions that name one of these maps, and nothing else in their files. A
`std::int32_t` copy with the home's name becomes `using Maps::<Name>;` in the same place. A copy with another type
(`std::uint8_t`, `int`) or another name for the same map (`CastleMap`, `CastleSiege`, `LandOfTrialMap`, `TrialsMap`,
`Map33`, `Map34`, `Map35`, `SelectionMap`, `SelectionWorldMap`) keeps its name and its type and takes the home's name
as its initializer (the [named-values plan](2026-11-23-named-values.md)'s "replace initializers, not types"). No
comparison, citation or test changes. The role names for the same numbers (`BubbleMap`, `NoShadowMap`,
`SpectatorMap`) and the range bounds under other names (`FirstBloodCastle`, `FirstKalima`) stay for their module's
design PR. The registry's baseline loses the rows this PR replaced.

### Task 5e: the fauna's loose map numbers

PR `refactor(AR1.1): name the maps the fauna compares`, branch `refactor/ar1.1-fauna-map-names`. After Task 5d.

`World/Birds.cpp` and `World/Fish.cpp` (`game/Fauna`) compare the map with a bare number 46 times. Each one takes the
home's name, by hand and with the original read at its address: `map == 7` becomes `map == AtlansMap`, and a range
becomes `IsBloodCastle(map)` or `IsKalimaMap(map)` only where the original's compares at that site are that
predicate's (`0x44644c..0x44649b` checks 0, 1, 3, 4 and 10, then 11 to 17, then 24 to 29 or 36). `Fish.cpp`'s
helper for the switch's case 7, `MapSeven`, takes the map's name too (`SpecializeAtlans`). The other loose literals
(about 130 in 27 files) stay with each module's AR4 design PR.

### Task 5f: the fauna's models have names

PR `refactor(AR1.1): name the models of the fauna`, branch `refactor/ar1.1-fauna-model-names`. After Task 5e
(Emanuel, 2026-11-25, reviewing Task 5e's PR: the code has to say what `type` is and what `0xad` means).

`type` is the model: the word at `+2` of a bird or a fish, a slot of the model table. `Assets/ModelIds.h`
(`game/Assets`, the home the [named-values plan](2026-11-23-named-values.md) gives the non-item models in AR4.1) is
born with the slots the fauna uses, as `inline constexpr std::uint16_t`, the type the model library takes:
`BirdModel` `0xad`, `ButterflyModel` `0xae`, `BatModel` `0xaf`, `RatModel` `0xb0`, `Bug01Model` `0xb1`,
`Fish01Model` `0xb2`, `Fish02Model` `0xb3`, `Fish04Model` `0xb5`, `Fish08Model` `0xb9`, `Fish09Model` `0xba`,
`CrowModel` `0xbc`, `RidingSpearModel` `0x10f`, `ScorpionModel` `0x148`, `Monster32Model` `0x171` and
`Monster34Model` `0x173`, each named after the file the loader puts in the slot. `Bug02Model` is `Fish01Model`:
Tarkan and Aida load `Bug02` in that slot. `tests/scenes/test_model_ids.cpp` writes each number and checks each name
against the loader's tables.

The 50 bare numbers of `World/Birds.cpp`, `World/Fish.cpp`, `Scenes/FaunaBirdHandlers.cpp`,
`Scenes/FaunaDrawing.cpp` and `Scenes/FaunaStep.cpp` take the home's names, with every comparison and cast as it
was. The role names (`IcarusBird`, `StormBird`, `InvisibleBird`) keep their type and take the home's name as their
initializer. The field, the parameters and the locals called `type` become `model` (`FaunaObject::model`,
`FishStepView::model`), and the bird handlers named after a number take the model's name (`MoveAe` becomes
`MoveButterfly`). A comparison that cannot tell the map uses the slot's first name (`Fish01Model`). What stays: the
tests' numbers, the loader's tables, the joint type `0x10f` and the monster numbers `0x1f` and `0x21` (other
spaces), and the record `Birds.cpp` writes by offset, for the Fauna design PR (AR4.3).

---

## AR1.2 (move, targets, namespaces, comments)

### Task 6: the ADR for modules as shared libraries

Before Task 8, with `engineering:architecture`: `docs/adr/0028-shared-library-modules.md`, written from what Emanuel asked
on 2026-11-22 (Windows `Engine.dll`, `OpenGL.dll`, `Music.dll` and a few more; Linux the same with `.so`) and from what
large projects do. It goes in its own PR (`docs(AR1): decide modules as shared libraries`) and Task 8 starts on his yes.

- **What large projects do** (Context7, 2026-11-22): Unreal compiles each module into a DLL loaded by one executable and
  can build monolithic instead; its docs call many DLLs right for engine code and questionable for gameplay (faster links
  against more exports and interfaces). O3DE builds each Gem as a `.dll` and, with `LY_MONOLITHIC_GAME`, as a `.lib`
  linked into one smaller binary that starts faster. Source and id Tech 3 (known, not from Context7) ship `engine.dll`,
  a `shaderapi` per graphics backend and the game as `client.dll`/`server.dll`.
- **The decision to write:** every engine module is a shared library (`Core`, `Audio`, `Render`…), and the
  OpenGL backend (`Render/Gl`) is its own, `OpenGL`, as module `engine/Render/Gl` (layer 5) in `modules.tsv`, so
  changing the renderer means changing one library. The game becomes few libraries, starting with one, `Game`, in
  AR4; the executable keeps the original's name, `main.exe` (`main` on Linux). `MU_MONOLITHIC=ON` builds every module static into one binary.
- **XorRemap is not a library of its own** (Emanuel, 2026-11-23): easy to remove, but not shipped as a DLL that says
  what it is. It stays a module in the code (`engine/Network/XorRemap`, its folder, `MU_XOR_REMAP`) and a static target
  that only `main.exe` and its tests link, so no shipped file names it and no library exports it.
- **The names** (Emanuel, 2026-11-22: no MuRet prefix): the libraries are `Core.dll`, `Audio.dll`, `Render.dll`,
  `OpenGL.dll` and so on, the game `Game.dll`, and the executable `main.exe`, like the original. CMake
  targets use the same words; on Linux CMake gives the usual `libCore.so`. The export macros follow Unreal (`CORE_API`).
  CMake options and functions, macros and environment variables use `MU_`/`mu_` (Emanuel, 2026-11-23; Task 1b).
- **The rules it fixes:** exports with CMake's `generate_export_header` per module (`CORE_API` in
  `Core/CoreExport.h`, generated), `CXX_VISIBILITY_PRESET hidden` and `VISIBILITY_INLINES_HIDDEN` on both systems and
  `-Wl,--no-undefined` on Linux, so a missing export or dependency fails on both CI machines; no mutable state defined in
  a header (each DLL would get its own copy); all binaries in one output folder (`bin/`), with `$ORIGIN` as the Linux
  `RPATH`; the vcpkg triplet stays `x64-windows` (dynamic CRT), which DLLs that pass `std` types need.
- **Tests:** a module's tests link only its library, and `ClientTestSupport` splits into `EngineTestSupport` and
  the game's. The question left from 2026-11-02 goes in the same ADR: one test executable per file, as today (728), or one
  per module (15 to 40). Without an answer, one per file stays.

### Task 7: the move

PR `refactor(AR1.2): move the engine to Client/src/engine`, branch `refactor/ar1.2-engine-move`. Skill: `muret-move`.

- [ ] **Step 1: Run the move** on an up-to-date `refactor/client`:
  `python3 -I Client/tools/architecture/move.py --root engine --modules game/Tables,game/Maps,game/Sound`.
- [ ] **Step 2: Do by hand only what it printed:** `target_include_directories(ClientLib PUBLIC src src/engine src/game)`,
  and the `list(FILTER … REGEX)` lines and globs that named an emptied folder (`/src/Platform/` →
  `/src/engine/Platform/`, `/src/Render/Gl/`, `/src/Render/Models/RoundedMath/`, `Network/XorRemapObfuscation.cpp`).
  move.py moves only sources, so also move `src/Render/Models/RoundedMath/CMakeLists.txt` next to its
  `RoundedMath.cpp` (and its `add_subdirectory`), and point the `SdlAudio.cpp` row of
  `Client/tools/architecture/rounded-math-exceptions.tsv` at the new path (review of #424).
- [ ] **Step 3: Check** `check_pure_move.py --base origin/refactor/client --head HEAD` lists nothing, `check_layers.py
  --clean-root engine` prints `0 new`, and `git diff -M50% --diff-filter=M` only touches include lines and build paths.
  Push and let CI build. Expected: green, the golden unchanged.
- [ ] **Step 4: After the squash**, in the next PR: `move.py --record <squash>`.

### Task 8: one shared library per engine module

PR `build(AR1.2): build each engine module as a shared library`, branch `refactor/ar1.2-engine-libraries`. Skill:
`cpp-pro`. Starts after Emanuel's yes to the Task 6 ADR.

**Interfaces:**
- Produces: `mu_add_module(<target> MODULE <module> [LINK <targets>…])` in `cmake/AddModule.cmake`: `SHARED`, or
  `STATIC` with `MU_MONOLITHIC`; sources and `FILE_SET HEADERS` from the module's folder, include root
  `Client/src/engine`, `VERIFY_INTERFACE_HEADER_SETS ON`; `generate_export_header` with `EXPORT_MACRO_NAME` `<MODULE>_API`
  (`STATIC_DEFINE` set when monolithic), its header in a second `FILE_SET`; hidden visibility; `--no-undefined` on Linux;
  `mu_set_fidelity_flags`; `mu_reuse_precompiled_headers(<target> StdPch)`.
- Produces: libraries (target name = file name, no prefix) `Core`, `Math`, `Crt`, `Crypto`, `Diagnostics`, `Input`, `Audio`,
  `Network`, `Data`, `Platform`, `Pathfinding`, `Render`, `OpenGL` and `Models`, each linking only what its `may_use`
  row allows. `XorRemap` (only with `MU_XOR_REMAP`) stays `STATIC` in both shapes, with no export macro, and only `Main`
  and its `XOR_REMAP` tests link it. `ClientLib` (game and
  app until AR4 and AR5) stays static and links them; the `Main` target gets `OUTPUT_NAME main`; `ClientPlatform`, `ClientRender` and `ClientRoundedMath` disappear
  into `Platform`, `OpenGL` and `Math` (the latter keeps its own compile options).
- Produces: the row `engine/Render/Gl` in `Client/modules.tsv` (layer 5, `may_use` `*layer<5`) and the `Render/Gl` rows of
  the map pointing to it; only `main.cpp` includes `Render/Gl/` today, so only the executable links `OpenGL`.
- Produces: `mu_set_fidelity_flags(target)` in `cmake/` (`-ffp-contract=off` on GCC and Clang, whatever MSVC needs).
- Produces: `StdPch` (static, one empty source, the standard-library list) with the modules' position-independent
  code, visibility and warnings, so GCC and MSVC accept the reuse; a second owner for the targets that see
  `SDL_OPENGL_1_FUNCTION_TYPEDEFS` unless the Windows job shows it is not needed.
- Produces: `CMAKE_RUNTIME_OUTPUT_DIRECTORY` and `CMAKE_LIBRARY_OUTPUT_DIRECTORY` at `${CMAKE_BINARY_DIR}/bin`,
  `BUILD_RPATH_USE_ORIGIN ON`.

- [ ] **Step 1: Measure before:** the Windows Debug job's build time, link time of the tests and number of `.pch` files,
  from a CI run of the base.
- [ ] **Step 2: Find the state defined in headers:** a script lists the non-`const` `inline` variables and the `static`
  locals of functions defined in engine headers; each becomes a declaration with its module's macro and a definition in
  a `.cpp` (such as `IdentityObfuscation::Shared()`). The list goes in the PR description.
- [ ] **Step 3: Write the libraries** per Interfaces, and mark each module's public API with its macro (a script adds it
  to every class and free function declared in the module's headers that another module or a test uses; templates and
  `inline` functions get none). A link a module's `may_use` row does not allow is a layer bug: stop and fix the code in a
  design PR, not the row.
- [ ] **Step 4: Tests and CI.** The engine test folders link only their library and `EngineTestSupport`. The job
  "Windows (MSVC, Release, matemática)" adds `-DMU_MONOLITHIC=ON`, so both shapes build on every PR.
- [ ] **Step 5: Push and let CI build.** Expected: green on all jobs, `VERIFY_INTERFACE_HEADER_SETS` included, the golden
  unchanged; measure after and put both numbers in the PR description.

### Task 9: the namespaces follow the folders

PR `refactor(AR1.2): name the engine namespaces after their folders`, branch `refactor/ar1.2-engine-namespaces`. Skill:
`refactoring` (Rename). One rule: every file under `Client/src/engine/<Folder>/` (and the three game modules) declares
`Client::<Folder>` (`engine/Render/Frame` is `Client::Render::Frame`), and every reference follows.

The biggest renames (uses today): `Render::Models` → `Models` (5,344, the math files to `Math`), `Network::Crypto` →
`Crypto` (223), `World::OriginalFtolLow`/`FtolLow` → `Math` (187), `GameLogic` path types → `Pathfinding` (85),
`Core::Crash` → `Diagnostics` (57), `App` window types and `UI::IClipboard` → `Platform` (57), the frame contracts and
`TextureMetadata` from `Scenes` to `Render::Frame` and `Models`.

- [ ] **Step 1: Rename by script,** one folder at a time, each folder its own commit. The compiler finds what the script
  misses; a new ambiguity is resolved by qualifying, never by a `using namespace`.
- [ ] **Step 2: Check** no `namespace Client::<Old>` is left in a moved file, then push and let CI build. Expected: green,
  golden unchanged.

### Task 10: the comments

PR `docs(AR1.2): trim and translate comments`, branch `refactor/ar1.2-engine-comments`. Skill: `muret-move`, "The
comments PR". The comments of `Client/src/engine/`, the three game modules and their tests, to CODING_RULES §9.

- [ ] **Step 1: Rewrite the comments.** `check_comments_only.py --base origin/refactor/client --head HEAD` lists nothing.
- [ ] **Step 2: Push, and after the squash** `move.py --record <squash>`.

---

## AR1 is done when

- `check_layers.py --clean-root engine` prints `0 new` with no engine row in the baseline (CI runs it on every PR).
- No engine file defines a game value: `check_named_values.py --clean-root engine` finds no game space in `engine/`.
- Every engine module is a shared library that links with `--no-undefined` and builds with
  `VERIFY_INTERFACE_HEADER_SETS`; the client also builds as one binary (`MU_MONOLITHIC=ON`) and with
  `MU_XOR_REMAP=OFF`.
- The behavior golden and every test are identical on the four CI configurations.
- The reference regenerated on `refactor/client` gives the same port numbers as #422's (the AR1 start snapshot), except
  for paths (`reference_summary.py --nueva … --commiteada …`).
- The move's and the comments' squashes are in `.git-blame-ignore-revs` and `moves.tsv`.
