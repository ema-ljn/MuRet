# 0026. The client keeps the C++ standard library

**Status** · Accepted. Emanuel decided it on 2026-11-21, on the analysis of that day.

## Context

The question was whether the C++ standard library is fit for a game client, or whether the client should move to a
game-oriented replacement such as EASTL. Measured on `main` `bfaad8b` with GCC 13.3 and libstdc++:

- **How the client uses it.** `Client/src` (1,422 files) uses `std` mostly as vocabulary: `std::string` 5,860 times,
  `string_view` 1,933, `optional` 1,773, `bit_cast` 1,684, `array` 1,346, `span` 1,191, `vector` 517, `function` 437,
  `map` 53, `deque` 50 and `unordered_map` once.
- **What a frame costs.** A probe that runs `GameScene` frames without GL (Lorencia with 40 monsters, 250 frames) gives
  2,252 allocations and 2.5 to 3.2 ms of CPU per frame at `-O2`, of the 40 ms a frame has at 25 per second. `malloc`
  and `free` take 0.15 ms of it, and jemalloc, mimalloc or tcmalloc change nothing measurable. 97% of the drawing's
  allocations are temporary `std::vector` per mesh in the skinning and the character drawing; the step rebuilds about
  85 `std::function`.
- **Debug.** The same frame takes 39 to 41 ms at `-O0`, half of it inside libstdc++ functions that are not inlined,
  and 7.8 to 8.1 ms at `-Og`, which builds in the same time.
- **`std` against EASTL 3.27.01**, in one microbenchmark: at `-O2` they tie, except EASTL's hash map (1.5 to 1.7 times
  faster) and `std::string` (1.3 times faster than EASTL's). EASTL is 1.2 to 3.6 times faster only at `-O0`. A
  `std::array` with a count beats `eastl::fixed_vector` at both levels.
- **Fidelity** depends on pieces of the MSVC 6 runtime that MuRet already reproduces by hand (`CrtRandom`,
  `Msvc6Sort`, `SprintfFormat`, `OriginalFormat`, `Ftol`), not on the containers. `main.exe` itself uses the MSVC 6
  STL. No library brings those pieces, and EASTL's `sort` changes its threshold between CPUs (28 elements on x86, 16
  elsewhere).
- **What similar projects use.** Of ten open-source game reimplementations, MMO clients and servers (MuMain,
  devilutionX, OpenMW, OpenTTD, OpenRCT2, otclient, TrinityCore, fheroes2, VCMI and Godot), nine build on `std` and
  none uses EASTL, Abseil, folly or `std::pmr`. The ones that add something add one piece: `ankerl::unordered_dense`
  (devilutionX), a static vector (devilutionX, OpenRCT2), `boost::container` (TrinityCore, VCMI). Among studios, only
  EA published a standalone library of this kind; O3DE's `AZStd`, Valve's `CUtl*`, id's idLib, Gaijin's Dagor and
  King's Defold ship theirs inside an engine, and the maintained MMO code that studios released (Ryzom Core, the
  Second Life viewer, Plasma) uses `std`.
- **EASTL itself** targets C++14, had 10 commits in 2025 and leaves 2026 issues unanswered (CMake 4, aarch64). It has
  no `format`, `filesystem`, `ranges` or `error_code`, which the client uses about 1,900 times, and its own FAQ says
  some standard libraries have better runtime debug checks.

## Decision

- The client keeps `std` for its containers and vocabulary types, with no replacement library.
- The parts of `std` whose result the standard does not fix are fenced by rules: sorting, random numbers, hash-table
  order and formatted text ([CODING_RULES §10](../CODING_RULES.md#10-pensar-antes-de-agregar)), and allocations in the
  frame ([§12](../CODING_RULES.md#12-rendimiento-con-criterio-en-los-caminos-calientes)).
- If a measurement ever shows a container problem, a single piece comes in, in the order §10 gives, and not a whole
  library.

## Consequences

- No migration: the 32 refactor PRs, the MSVC Debug iterator checks in CI and a single vocabulary in code and tests
  stay as they are.
- The per-frame allocations go away by reusing buffers in AR4.1 (character drawing and skinning) and by deleting
  `GamePanels` in AR4.5, with the behavior golden identical.
- `-O0` stays slow. The `linux-x64-fastdebug` preset (`-Og`) is the cheap fix for debugging on Linux; on Windows the
  `windows-x64-relwithdebinfo` build preset is offered but not measured, and CI does not build it.
- Not measured: Windows with MSVC's STL, GL drawing, fights and effects.
- What would reopen it: a Release scene whose CPU work passes 10 ms per frame with the time inside the containers, a
  port to a console or a low-memory device, or large hash tables on a hot path.
- The rules are checked in review; no tool checks them yet.
