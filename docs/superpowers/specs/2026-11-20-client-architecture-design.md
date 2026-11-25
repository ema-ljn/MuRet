# Client architecture: layers, modules and small classes (design)

> **Working document, 2026-11-20.** Emanuel's request: a plan to decouple, refactor, deduplicate
> and modularize the client, because "everything was done in a hurry" and "everything ends up in
> one ClientLib", without losing what is still unported (gaps and traces), which must stay
> tracked. The references he asked for are OpenMU and TrinityCore ("my models to learn how a large
> project is organized"), with classes as concise as OpenMU's, improving on them where possible. He
> also asked whether the engine should be its own module.
>
> This document says **what the code looks like** and **what is non-negotiable** (fidelity and
> tracking). The order of the work, by epics and milestones, is in the
> [plan](../plans/2026-11-20-client-architecture-refactor.md). Measurements were taken on `main`
> at `bb56680` (2026-11-20). Revised on 2026-11-21: more modules (from 31 to 58) and what is taken
> from TrinityCore and MaNGOS besides OpenMU.

## What and why

- **Goal:** every part of the client has an obvious place, dependencies go one way, classes and
  files are small with one responsibility, and the same logic is not written several times; adding
  a packet handler or a window touches two or three files of its module, not six spread across
  three layers.
- **Goal:** the remaining port (gaps, cuts, VM checks, rows of 08) stays measured per module, and
  every pending item has a target module before it is ported.
- **Non-goal:** changing behavior. No refactor milestone fixes a port bug or changes what is drawn,
  played or sent: that is a port PR, separate.
- **Non-goal:** changing the graphics engine, the language, the C++ version or the libraries (see
  [The engine apart](#the-engine-apart-yes-in-its-own-root-and-with-its-own-targets)).
- **It would be a failure:** a refactor that changes the order of calls or the number of `rand()`
  calls (port fidelity), that leaves the reference TSVs miscounting, or that stalls closing gaps
  and traces for weeks.

## What we measured

Three static analyses (the client does not build in the cloud container; see
`cloud-container-build`): the `#include` graph, per-function metrics (lizard) and clone detection
(jscpd and a token-based detector of our own). Round numbers come from the scripts; numbers
marked "≈" are estimates.

### A monolith in one folder

| folder | files | lines |
| --- | ---: | ---: |
| `Scenes/` | 687 (349 `.cpp`, all flat and in `Client::Scenes`) | **164,361** |
| `World/` | 148 | 28,289 |
| `UI/` | 115 | 26,588 |
| `Render/` | 98 | 11,269 |
| `Protocol/` | 97 | 11,101 |
| `Data/`, `App/`, `Core/`, `Network/`, `Platform/`, `GameLogic/`, `Audio/`, `Input/` and `main.cpp` | 273 | 27,451 |
| **total `Client/src`** | **1,418** | **269,059** |

`Scenes/` is 61% of the client. Everything except `Platform/` and `Render/Gl/` builds into a
single `ClientLib` with `GLOB_RECURSE`, and the 728 test executables link all of it. `Scenes/` is
not "the scenes": it holds actors, monsters, skills, effects, inventory, chat, guilds, events and
the HUD.

### Two god objects and two dispatch hubs

- **`WorldState` and `WorldRuntime`** are the original's `.bss` as two aggregates: the pool of 401
  characters, the own record, the map and ~90 more members (`hud`, `chat`, `guild`,
  `castleSiege`, `crywolf`, `quest`, `social`, `panels`, `music`...). They are included by 197 and
  194 files, 248 functions take `WorldState&`, `Runtime()` is called 1,273 times, and changing a
  field recompiles ~420 translation units. `WorldRuntime.h` includes 45 headers, 15 from `UI/`.
- **The path of a packet** crosses six files in three layers: the reader in `Protocol/X`, the
  alternative in the `std::variant` of `App/WorldEvent.h` (124 types), the declaration and
  registration in `App/ServerSession.h/.cpp` (2,878 lines, 125 registrations, 88 nearly identical
  `On*`: read, warn "ignored…", `ApplyWorld`), the chain of 123 `std::get_if` in
  `Scenes/SessionEventApplier.cpp` and the `Apply…` in `Scenes/XReply`. It is TrinityCore's
  `WorldSession` (509 `Handle…`) in two parts, and it forces `/bigobj` on all of `ClientLib`.
- **`WorldEntryResources`** is the bag every applier receives (75 users): 20 fields, 6 of them
  `std::function`. There are 74 `*Context` structs with 206 references and 164 pointers, and 335
  `std::function` in headers: sending is rewired 47 times and sound 22.
- **`GameScene`** (8 files, 3,430 lines, ~55 methods) does nine jobs and includes from 24 of the
  25 other modules. **`UI/PanelRegistry`** (`Open` with complexity 92) and
  **`Scenes/GamePanels`** (15 `Lend*`) wire the panels of every game feature by hand.

### Reverse dependencies

Outside `Scenes/` the layers are mostly respected; what is wrong is small and localized:

| from → to | includes | what it is |
| --- | ---: | --- |
| `Render`, `UI`, `Platform`, `Scenes` → `App/WindowState.h` | 119 | a window type lives in `App` |
| `UI` → `Scenes/ITextDrawer.h`, `SceneTextures.h` | 33 | drawing contracts in `Scenes` |
| `Render` → `Scenes/` (`SceneCanvas`, `SceneText`, `IFrameTarget`, `IWorldView`, `Screenshots`, `CharacterDrawing`) | 8 | the frame and character drawing pointing backwards |
| `Data`, `GameLogic`, `UI` → `World/OriginalFtol.h` | 3 | the original's math in `World`, duplicated in `World/Ftol.h` |
| 219 files → `Render/Models/Cloth.h` | 219 | almost all for `CrtRandom`, the CRT `rand()` (737 uses) |
| everything → `Scenes/CharacterDrawing.h` | 79 | defines `IModelLibrary` (8 implementations, 75 fakes in tests) |

Inside `Scenes/`, the split into 26 candidate modules ([annex](../plans/2026-11-20-client-architecture-refactor/))
shows that **all 26 form a single cycle** and that 100 pairs include each other. No header comes
back to itself: the cycles come from `.cpp` files that call another module. Moving the state
headers and the UI kit down cuts 52 of the 100 pairs; the remaining 48 are six concrete knots (the
character step `0x508f00`, the viewport appliers, the HUD composition, combat↔hero, the raven in
`WorldTransitionSupport` and the whole actor in `LetterPortrait`).

### Repetition

- **Few exact copies:** 1.3% of `src` according to jscpd. With literals normalized, 5.0% (13,472
  lines), and 14.0% in tests. Setting the monster tables (data) aside, 22% of the repetition copies
  clones in the original and **54% is MuRet's own repetition**.
- **22 functions of the original ported twice** (88 functions, ≈1,250 lines): `0x57ce80` in
  `HeroSkills.cpp` and in `Skill57ce80.cpp`, `0x4eac00` in `HeroSkillsElf` and in
  `HeroSkillsWizard`, `0x4eb860`, `0x5fccc0`, `0x4ce000`...
- **Copied helpers:** `F32` (the x87 conversion to `float`) in 115 files, although a shared one
  exists; `TextOf` in 39 (18 identical); 24 `InRange`; the `Quad` of `0x621b60` in 10; `FieldsOf`
  in 11 `Protocol` files; `FirstChaosCastleMap` defined 25 times; 1,224 `bit_cast<float>(0x…)`
  with 237 distinct values; `White` 37 times.
- **Hand-written error propagation:** 1,377 `return Core::Unexpected(x.Error())`, 973 of them on
  two lines, because `Core::Expected` has no `AndThen` or `Transform`.
- **Four ways to read a packet:** `Core::ByteReader` (28 files), two private `Fields` classes
  (one checks bounds, the other does not) and loose `ByteOf`/`WordAt` in `Scenes`.
- **Tests:** 728 executables; `LetterMeasure` copied into 40 files, `IModelLibrary` fakes in 60,
  168 helpers defined in four or more files (14,500 lines). `tests/scenes` (190k lines) is larger
  than `src/Scenes`.

### Sizes

8,800 functions: 505 exceed 50 lines, 94 exceed 100 and 14 exceed 200; 501 exceed complexity 15
and 21 exceed 60; 196 have more than 6 parameters (16 at most, `FaunaJointCreation.cpp`). The
`.cpp` files of `Scenes/` have a median of 241 lines and a p90 of 913. For comparison, in OpenMU a
packet handler has a median of 40 lines and a player action, 53.

## The references: what we take from each

Read at the source: OpenMU `master` f6b4243 (and 45678e3 for granularity, on 2026-11-21),
TrinityCore `master` 5d07da5, cMaNGOS `mangos-wotlk` 27cdfdf, MaNGOS Zero b7fface (with
ScriptDev3 6e2e749) and MuMain `main` b9f4614. None becomes a dependency.

**What TrinityCore and MaNGOS teach about modules, measured:** in both, `game` is a single library
and its folders are not modules. In TrinityCore, 49 of the 51 folders of `game` form a single
cycle (185 pairs include each other, such as `Entities`↔`Server` with 97 and 99 includes); in
cMaNGOS, 44 of 47; in MaNGOS Zero, 11 of 15. The only clean boundary is between libraries
(`common` ← `database` ← `shared` ← `game` ← `scripts`), with zero upward includes, because the
linker enforces it. The causes of the cycle are mechanical: every subfolder is an include directory
(`CollectIncludeDirectories`, so `#include "Player.h"` costs nothing), a PCH with game headers
hides missing includes, and freely accessible singletons (63 `sXxx` in TrinityCore; `sObjectMgr`
appears 1,013 times in 104 cMaNGOS files). The god classes come from there: `Player.cpp` has
31,765 lines in TrinityCore and 26,182 in cMaNGOS. MuRet takes the idea that does work for them one
level further in: **one target per module**, with an include root per layer and qualified paths,
and a PCH with only the standard library (already the case: `cmake/PrecompiledHeaders.cmake`), shared
by every module target through `REUSE_FROM` instead of built once per target (plan, AR1.2).

| pattern | from | in MuRet |
| --- | --- | --- |
| logic does not see bytes or the network (`GameLogic` does not reference `Network`) | OpenMU, CODING_RULES §7 | **adopted:** game functions receive the packet already read; layers are enforced with CMake targets and include roots |
| one small handler per packet (median 40 lines), grouped by feature (`MessageHandler/Items/…`) | OpenMU | **adapted:** one free function per packet in `game/<Module>/In/`, with no interface, plugin or GUID |
| declarative opcode → handler table (`DEFINE_HANDLER` in `Opcodes.cpp`) | TrinityCore | **adapted:** a `constexpr` table, with no macro or `unique_ptr` per row, carrying the address of the original `case` and `Unported{va}` rows |
| packets typed by feature (`WorldPackets::Chat::…`) | TrinityCore | **adopted:** `Protocol/<Module>/` |
| the field position in the type (`UpdateField<T, Block, Bit>`) | TrinityCore | **adapted:** `Field<T, Offset, Endian>` for packets and the `Data\` formats |
| the layout described as data and compared (DB2, with `ASSERT` at startup) | TrinityCore | **improved:** `static_assert` and tests at build time, not at startup |
| packets generated from XML (offsets only in the XML) | OpenMU | **deferred:** `constexpr` descriptors in C++ first; a generator only if the repetition persists (CODING_RULES §10) |
| one action per player intent (`PlayerActions/<Feature>/`) | OpenMU | **adopted:** `game/<Module>/Out/`, which sends the packet |
| "policy → plugin, mechanism → component, pure → free function" | OpenMU, `PlayerRefactoringPlan.md` | **adopted without the policy:** the policy is the original |
| reflection-based plugins, GUIDs, client versions, toggles, `async` | OpenMU | **rejected:** there is one client (1.02) and one game thread |
| unordered plugin points | OpenMU (admits it) | **rejected:** the order of calls and `rand()` is part of the contract; every fan-out is an ordered list that copies the original |
| 190 view interfaces with 292 implementations | OpenMU | **rejected:** with one client, an interface with one implementation is noise; interfaces only at the engine's edges and where a test needs them |
| the 2,665-line `Player` ("ten classes in a trench coat") | OpenMU (admits it) | **improved:** state split per module, and each handler declares in its signature the parts it touches |
| `WorldSession` with 509 handlers | TrinityCore | **rejected:** it is our `ServerSession` |
| serializing in stream order (`Read()`/`Write()`) | TrinityCore | **rejected** for fixed layouts: offsets stay visible and are compared against the research |
| "drawing does not change state", one frame behind (ADR 0003) | MuMain | **rejected:** it changes the timing and the `rand()` sequence; we separate *where code lives*, not *when it runs* |
| folders by layer and by domain (`GameLogic/{Items,Combat}`), ~40-line functions | MuMain | **the shape is adopted**; its `WSclient.cpp` (463 `case`s) and the `Zzz*` files are not |
| one target per library, zero upward includes between targets | TrinityCore, MaNGOS | **adopted and taken one level down:** one target per module, not per layer |
| `game` as one target with all its subfolders as include directories | TrinityCore, MaNGOS | **rejected:** it causes the 49-folder cycle; a root per layer, qualified paths and `check_layers.py` |
| format reader below, rows and tables above (`DB2FileLoader` → `DB2Storage<T>` → `DB2Structure.h`; `DBCFileLoader` → `DBCStructure.h` + `DBCfmt.h`) | TrinityCore, MaNGOS | **adopted:** `engine/Data` reads, `game/Tables` declares the rows, with `static_assert` instead of `ASSERT` at load; without the 297 (TrinityCore) or 114 (cMaNGOS) global `extern` tables |
| one folder per instance with an ids header (`molten_core/` + `molten_core.h`), `ZoneScript`/`InstanceScript` | TrinityCore, MaNGOS | **adapted:** `Events/<Event>/` and `MapScripts/<Map>/` with their `<Name>.h`; each map's script is called where the original calls that map's code, in a chain written in the original's order, with no table by map id over the chains, no registration by name and no factories |
| the script loader with `AddSC_` calls in written order (`ScriptLoader.cpp`) | cMaNGOS | **adopted:** everything that hooks in (extension points, scripts) is assembled in `app/` in the original's order, never with static constructors |
| `ScriptMgr` with hooks broadcast in registration order (`FOREACH_SCRIPT`) | TrinityCore | **the broadcast is rejected:** the order of calls is written by the original |
| the interface in the lower target and the implementation above (`proto/IWorldGateway.h`) | MaNGOS Zero | **adopted:** the `Kernel` ports |
| templates without game types kept apart (`framework`); a `terrain` that knows nothing of the world | cMaNGOS, MaNGOS Zero | **adopted:** `engine/Core`, `engine/Pathfinding`; `game/Terrain` does not include `Actors` (already true today) |
| logic-free enum headers (`*Defines.h`, 18 in cMaNGOS) and forward declarations (`Player.h`: 17 includes, 64 forward declarations) | cMaNGOS, TrinityCore | **adopted:** `<Module>Fwd.h` and `<Module>Defines.h` |
| entities build and send packets (`Entities` → `Server`) | TrinityCore | **rejected:** `Actors`, `Player` and `Items` do not include `Protocol` or `Session`; they send through the `Kernel` port |
| behavior as data (SmartAI: 160 actions; EventAI) | TrinityCore, MaNGOS | **rejected:** behavior is the binary's; an interpreter changes the order |
| scattered `#ifdef`s for optional modules (657 `ENABLE_ELUNA` in MaNGOS Zero); splitting files without splitting classes (`Player*.cpp`, 30,716 lines) | MaNGOS Zero | **rejected** |
| a separate project by dependency direction, not size (`Pathfinding`, `AttributeSystem`, a 109-line `LoginServer`) | OpenMU | **adopted:** the criteria of [The modules, one by one](#the-modules-one-by-one) |
| the root of `GameLogic` as a drawer (145 loose files, `Player` named in 86 of 139 plugins) | OpenMU | **rejected:** `Kernel` on a budget (8 files today) and nothing loose in `game/` |
| the full vertical `CastleSiege/` (actions, NPC, intelligence, schedule) versus the event spread over four folders | OpenMU | **the vertical is adopted:** each event in its folder |
| sizes: median of 40 lines per file, p90 of 218 | OpenMU | **adopted as a measurable goal** together with the reference sizes below |

**Clean Code and Clean Architecture** (Emanuel, 2026-11-21: Clean Code is the refactor's guide).
They are not one more reference but the rules every design PR is written and reviewed with, below
[fidelity](#non-negotiable-fidelity) and CODING_RULES. Almost every decision in this spec already
is a Robert C. Martin rule under another name: the layers are the dependency rule; "contract
below, implementation above" is dependency inversion, with the interface (the port) owned by the
layer that uses it; zero cycles is the acyclic dependencies principle; `app/` is the main
component, the composition root; one module per game feature is the common closure principle
("screaming architecture"); and `check_layers.py` is the boundary test. Where they clash with the
original, the original wins: state that mimics the `.bss` stays as data with the logic in its
module's functions (the data/object asymmetry of *Clean Code*), a transcribed `switch` is not
spread over classes, and *Extract Variable* keeps the type of every `float` intermediate. The rules, translated to MuRet, live in the
repo skills `clean-code`, `clean-architecture`, `refactoring` and `cpp-coding-standards`.

## Approaches

1. **By epics, interleaved with the port (chosen by Emanuel on 2026-11-21).** First the safety net
   (the behavior golden and move-proof tracking), then the engine apart, the protocol and the
   state, and finally `Scenes/` split into modules, one group per milestone. Each milestone is a
   script-generated move (no logic, verifiable), a PR that leaves its comments in English and only
   the necessary ones (verified by script) and a design PR. The port continues in parallel: new
   code goes into its target module.
2. **Close gaps and traces first, refactor afterwards.** Zero risk of clashing with the port, but
   everything still missing goes into the old structure (more `WorldRuntime`, more
   `ServerSession`) and has to be moved anyway.
3. **Freeze the port and do it all at once.** The fastest refactor, but it stalls the port for
   weeks and concentrates all the risk in a few giant PRs nobody can review.
4. **Folders and targets only.** Cheap (move files and split the CMake), but it keeps the god
   objects, the central dispatch and the repetition: it is the first third of approach 1, not a
   plan.

## Target architecture

### Three roots, each with its targets

```text
Client/src/
  engine/   the engine: knows nothing about game state (its own include root)
    Core/         Expected, ByteReader, Log and its sinks, IClock, text encodings, WrappingInt         0
    Math/         the original's math: Ftol, X87 (F32, Truncate), OriginalRotation,                    1
                  OriginalAngles, OriginalRetainedMath, HalfLifeMath
    Crt/          what mimics the MSVC 6 runtime: CrtRandom (rand), Msvc6Sort, the original sprintf    1
    Crypto/       MU's byte ciphers: SimpleModulus and its key file, Xor32, Xor3, K16                  1
    Diagnostics/  the crash handler: dump, report, logs, signals                                       2
    Input/        keyboard and mouse per frame, without SDL                                            2
    Audio/        play effects and music, 3D position; does not know which sounds MU has               2
    Network/      non-blocking socket, packet framing, Connection, the obfuscation contract            2
      FoxAnti/    the lab's FoxAnti layer between the wire and the client; removed at compile time     3
    Data/         the Data\ formats: BMD, OZJ, OZT, JPEG, terrain, the table reader                    2
    Platform/     SDL3: window, GL context, events, audio, WindowState, Resolution, clipboard          3
    Pathfinding/  the original's A* over the terrain wall grid; does not see game state                3
    Render/       SpriteBatch, ModelBatch, text, frame contracts (IFrameTarget, ITextDrawer), Gl/      4
    Models/       skeleton, pose, animation, skinning, cloth, IModelLibrary                            5
  game/     the 1.02 game (its own include root; sees engine/)
    Protocol/     packet layouts and writers, per module (Protocol/Guild/...)                          6
    Tables/       MU's tables: items, sets, skills, quests, gates, texts, NPC names...                 6
    Maps/         what each map is: named ids, its folder and its ambient sounds                       6
    Kernel/       the GameContext, shared state (actor pool, player, current map) and the              7
                  ports (send a packet, request a sound, an on-screen message)
    Assets/ Terrain/ Sound/ Music/ Camera/ Ui/   the game services (Ui is the widget kit)              8
    Actors/ Player/ Hero/ Monsters/ Pets/ Movement/ Skills/ Combat/ Items/ Tooltips/                   9
    Npc/ Quests/ ChaosMix/ Warehouse/ PetTrainer/ Party/ Social/ Messenger/ PersonalShop/
    Chat/ Guild/ Events/<Event>/ CastleSiege/ WorldObjects/ MapScripts/<Map>/ Fauna/ Weather/
    Particles/ Joints/ Effects/ Hud/ FrontEnd/            the game modules (vertical slices)
    Session/      the connection to the servers, keepalive, the opcode table and routing               10
    Scenes/       the frame scripts: Login, Selection, Loading, Game; SceneManager                     11
  app/      main.cpp and App/: config, command line, loop, the composition root                        12
```

The number on the right is the layer. There are 59 modules (14 engine, 44 game and `app`), with a
median of 3,200 lines; the reason for each is in
[The modules, one by one](#the-modules-one-by-one). `game/Music` was added on 2026-11-25 by the
[music plan](../plans/2026-11-25-music-home.md). The first version of this document had 31;
on 2026-11-21 Emanuel asked to split "as much as possible" (starting with sound), and the revision
came from measuring the map and reading how TrinityCore, MaNGOS and OpenMU are organized.

- **The direction of dependencies** is `app → game → engine`, and inside each root a module uses
  only what is higher in the listing: in `engine/`, `Core` and `Math` at the very bottom and
  `Models` on top (`Render` lends it `TextureSlot` and `SpriteVertex`); in `game/`, `Protocol`,
  `Tables` and `Maps` at the bottom, then `Kernel`, the services (`Assets`, `Terrain`, `Sound`,
  `Music`, `Camera`, `Ui`) and the modules, and on top `Session` (its opcode table names the modules'
  handlers) and `Scenes`. A module sends packets through the `Kernel` port, which `Session`
  implements: handlers do not know the session. A game module does not include another one's
  internals: it includes its public header, and cycles are forbidden (`check_layers.py` in CI).
- **How far we are, measured with the target map:** the engine has 11 upward includes (`Render` →
  `Scenes` and `Actors`, 8; `Xor32` → `Network/Packet.h`, 2; `SdlAudio` → the sound catalog, 1);
  in total 365 includes in 83 module pairs (210 files) go up a layer, and between game modules of
  the same layer there are 1,685 includes in 306 pairs, 80 of them mutual. More modules mean more
  boundaries, so these numbers are higher than in the first version (336, 54 and 49): there is no
  more coupling, it is just more visible.
- **Enforced by the compiler, not by good will.** Each root is its own include root: `engine/`
  does not have `game/` on its path, so an `#include "Scenes/…"` from the engine does not compile.
  Folders that only change root keep their includes (`"Core/Expected.h"` stays the same, because
  the root changes and the relative path does not); when a file changes folder (`Scenes/X.h` →
  `Guild/X.h`), the move rewrites its includes and no other line.
- **Targets:** one per engine folder (`MuRetCore`, `MuRetMath`, `MuRetCrt`, `MuRetCrypto`,
  `MuRetDiagnostics`, `MuRetInput`, `MuRetAudio`, `MuRetNetwork`, `MuRetData`, `MuRetPlatform`,
  `MuRetPathfinding`, `MuRetFoxAnti` (only with `MURET_FOXANTI`), `MuRetRender`, `MuRetModels`)
  and one per game module (the submodules of `Events/` and `MapScripts/` are folders of their
  target), with `target_link_libraries` following the layers and includes of other modules always with the
  qualified path (`"Guild/Guild.h"`), never the bare name. Headers go in `FILE_SET HEADERS` with
  `VERIFY_INTERFACE_HEADER_SETS` (CMake 3.24; the repo requires 3.25): every header compiles on
  its own, verified in CI. `/bigobj` stays where needed, not everywhere.
- **Fidelity flags in one place:** `-ffp-contract=off` is on `ClientLib` today; with several
  targets it becomes a `muret_set_fidelity_flags(target)` function applied to every target that
  does the original's arithmetic, and the `math-release` job keeps covering them.
- **Namespaces:** `Client::<Folder>` as today (rule 7); game modules go from `Client::Scenes` to
  `Client::<Module>` (`Client::Guild`) in their design PR, not in the move.

### The engine apart: yes, in its own root and with its own targets

Emanuel asked whether the engine should be decoupled. **Yes, and it is the cheapest part of the
plan:**

- It is almost clean already: apart from about 40 reverse includes (`Render` and `Platform` to
  `App/WindowState.h`, `Render` to `Scenes/`, `Data` to `World/OriginalFtol.h`), `Core`,
  `Platform`, `Input`, `Audio`, `Network`, `Data` and `Render` do not see the game. SDL reaches 24
  translation units (only `Platform` and `main.cpp`) and FreeType one.
- With its own root, the compiler guards the boundary; with its own targets, a game change does not
  recompile the engine and engine tests do not link 164k lines they do not use.
- It leaves the door open to swapping only the graphics backend if that is ever needed (SDL3 GPU or
  bgfx, `client-engine-migration`): all GL is already in `Render/Gl`.
- **What it is not:** not a generic engine reusable outside MU. `Data/` reads MU's formats and
  `Math/` copies the original's x87 math; "engine" means "what does not know game state".
- **An improvement on the way:** `Render/Models` is simulation (skeleton, pose, cloth), not
  drawing: it moves to `engine/Models/`, and its x87 math (also used by `GameLogic`, `World` and
  `Data`) to `engine/Math/`, together with `Ftol`; `CrtRandom` leaves `Cloth.h` for `engine/Crt`.
  Today `Render` and `Render/Models` include each other both ways (5 and 6 includes); with the
  math below, only one direction remains.

### The modules, one by one

**The criteria for splitting** (from OpenMU, TrinityCore and MaNGOS; see
[The references](#the-references-what-we-take-from-each)):

1. **Dependency direction decides, not size.** In OpenMU `Pathfinding` (1,614 lines) and
   `AttributeSystem` (1,867) are projects of their own because they depend on nothing, and
   `LoginServer` has 109 lines. A small module (`Maps`, 117 lines; `Camera`, 410) is fine if
   nothing below needs it and it needs nothing above.
2. **An algorithm that does not see game state is an engine leaf** (`Pathfinding`, `Crt`,
   `Crypto`), testable on its own.
3. **Data without behavior goes at the bottom** (`Protocol`, `Tables`, `Maps`), and those do not
   include each other: in OpenMU `DataModel` drags in `Network` through two files.
4. **Contract below, implementation above:** `Kernel` declares the ports (send a packet, request a
   sound, an on-screen message) and `Session`, `Sound` and `Ui` implement them, like
   `proto/IWorldGateway.h` in MaNGOS Zero.
5. **An event or a map lives in one folder with everything it owns** (state, windows, handlers,
   ids), like OpenMU's `CastleSiege/` or TrinityCore's `molten_core/`. What it owns is told by the
   key the original dispatches on (2026-11-24): a case of the effect, particle or joint switch and
   a monster keyed by its model belong to those modules even when they serve one event, and the
   values every layer compares (the map id, an event's stage) live at layer 6, in `Maps` and
   beside the packet's layout in `Protocol`.
6. **If two sibling modules include each other**, what they share moves down to `Kernel` or the two
   merge; a cycle never stays.

**What changed in the 2026-11-21 revision** (lines measured on `main` `bb56680`, with the annex
map):

| module | layer | what it holds | why separate |
| --- | ---: | --- | --- |
| `engine/Crt` | 1 | `CrtRandom` (inside `Cloth.h` today), `Msvc6Sort`, the original `sprintf` (788) | what mimics the MSVC 6 runtime: the pieces fidelity depends on, together and tested on their own |
| `engine/Crypto` | 1 | SimpleModulus and its key file, Xor32, Xor3, K16 (708; today in `Network`, `Core` and `Data`) | byte ciphers need neither sockets nor formats; `Xor32` stops taking a `Network::Packet` |
| `engine/Diagnostics` | 2 | the crash handler: dump, report, logs, signals (2,643) | it was 2,643 of the 4,154 lines of `Core`; `Core` keeps what everyone uses |
| `engine/Network/FoxAnti` | 3 | the FoxAnti layer: the mechanism of the translations the lab server asks for in each direction (327; today in `Network`); its MU values (the XOR key, the opcode remaps, the server codes and the heads it rewrites) come injected from `app/` as a `FoxAntiTables` | it is not part of `main.exe`: it is a DLL hooked into it that only the lab GameServer requires (`03-red-y-cifrado.md`, section "La capa de FoxAnti": "a MuRet server might not use it"), so it must be removable: `Network` keeps only the contract (`IObfuscation`, with its five hooks in the original's order) and the identity, which touches nothing and is what runs when there is no layer. Only `app/` links `FoxAntiObfuscation` (today only `main.cpp` includes it). It is removed at compile time with the CMake option `MURET_FOXANTI`, because the programmer decides it, not the player (Emanuel, 2026-11-21), and the rest of the client receives it injected as `const IObfuscation&`: `app/` builds it with `MakeObfuscation()`, whose definition CMake picks from two files, so there is no `#if` in the code. The `[Server] Obfuscation` ini key goes away. It is an optional library of its own (`MuRetFoxAnti`) inside the `Network` folder, because the network is part of the engine, as in Unreal, Source, Quake 3 and Godot (Emanuel, 2026-11-21) |
| `engine/Pathfinding` | 3 | the original's A* and the wall grid (614; today in `GameLogic` and `World`) | it only uses `Data` and `Math`: a leaf, like `Pathfinding` in OpenMU |
| `game/Tables` | 6 | MU's 14 tables: items, sets, skills, quests, gates, texts, NPC names... (1,772; today in `Data`) | the engine reads the format, the game declares the rows: `DB2FileLoader` / `DB2Structure.h` in TrinityCore, `DBCFileLoader` / `DBCStructure.h` in MaNGOS |
| `game/Maps` | 6 | named map ids and their predicates (`IsChaosCastle`, `IsKalima`...), each map's folder and ambience | today there are about 189 per-map comparisons in 62 files of 18 modules, with loose constants (`c.map == 7`, `IcarusMap`); they become names and stay where they are, because they are the original's logic |
| `game/Sound` | 8 | the catalog from ids to `.wav` files (today in `engine/Audio`), which sounds each map loads, the per-model sound table, the sound request (899) | the engine plays sounds and does not know which sounds MU has: it receives the definitions at startup. The 224 calls that request a sound (78 files, 19 modules) stay in their functions, because in the original they are inside them and some pick with `rand()`; they swap `WorldRuntime::RequestSound` for the `Kernel` port |
| `game/Music` | 8 | MU's 22 music tracks (the original's one table of paths), the map music of the scene loop and the sweep that stops every track (added 2026-11-25, [music plan](../plans/2026-11-25-music-home.md)) | the engine plays a path and does not know which tracks MU has; the map music is behavior, so it cannot sit in `Maps` (criterion 3). Crywolf's, the siege's, the login's and the credits' choices stay with their owners and name their tracks from here |
| `game/Assets`, `game/Terrain`, `game/Camera` | 8 | game models and textures (4,261), the terrain (4,335), the camera (410) | they leave `Kernel`, which stays at 8 files and 1,735 lines: shared state and ports, nothing else. `Terrain` does not know actors, like MaNGOS Zero's `terrain`, which "knows nothing of the world" |
| `game/Movement` | 9 | the player's path, gates, speed (2,004; today in `Hero`) | it uses `Pathfinding`; skills, the HUD and NPCs use it. Today it includes `Events` and `Chat` (3 and 3) to know whether a click belongs to them: that becomes an ordered extension point (see the anatomy) |
| `game/Skills` | 9 | the hero's skills and their replies (9,025; today in `Hero` and `Combat`) | `Spells/` is a module of its own in TrinityCore and cMaNGOS. `Hero` stays small (attack and facing, 788): AR4.2 decides whether it merges with `Player` |
| `game/Tooltips` | 9 | item, skill and pet tooltips (5,699; today in `Items`) | they sit above `Items`, `Skills` and `Pets` |
| `game/Messenger`, `game/PersonalShop`, `game/Party` | 9 | friends and letters (7,740), the personal shop (1,978), the party (832) | `Social` (15k) mixed four things; it keeps trade and duel (2,369), and its chat moves to `Chat`. They are OpenMU's `PlayerActions/Messenger`, `PlayerStore` and `Party` |
| `game/Quests`, `game/ChaosMix`, `game/Warehouse`, `game/PetTrainer` | 9 | quests (2,127), the Chaos Goblin machine and jewel mixes (2,682), the warehouse (552), the trainer (1,142) | `Npc` (9k) mixed five windows; it keeps the talk and the generic windows (2,589) |
| `game/Events/<Event>/` | 9 | Blood Castle, Devil Square, Chaos Castle, Kalima, Crywolf (with its camera tour) | one submodule per event with everything it owns: what a packet, a window, an NPC or the event's stage reaches; `EventPackets.cpp` (1,740) is split among them. An event with a map of its own (Crywolf) keeps that map's objects in `MapScripts/<Map>/`, which reads the event's state one way. OpenMU spreads each event over four places (rules, entry, schedule and views): not here |
| `game/MapScripts/<Map>/` | 9 | the objects of Noria, Atlans, Icarus, Tarkan, Kalima, Crywolf, Aida, Land of Trials, Dungeon and the castle (3,051; today in `WorldObjects`) | what each map does, with a `<Map>.h` of ids, like `scripts/<Zone>/` in TrinityCore: what a per-map chain of the original reaches with the map as its gate. The chains (`0x606660`, `0x604ab0`, `0x602ed0`, `0x6002e0`) stay written sequences of guarded calls in the original's order, because the order of the maps differs from chain to chain and a link may decline; a `constexpr` table by map id holds per-map data only (2026-11-24, [`crywolf-homes`](../plans/2026-11-24-crywolf-homes.md)) |
| `game/Fauna`, `game/Weather` | 9 | birds and fish (3,111), leaves, rain and clouds (1,000; today in `WorldObjects`) | ambient systems of their own; `Weather/` is a folder of its own in TrinityCore and cMaNGOS |
| `game/Particles`, `game/Joints`, `game/Effects` | 9 | particles (7,639), joints (8,173), model effects (15,910) | `Effects` (31,700) was three systems. Both use `Particles`, but `Joints` and `Effects` include each other both ways (13 and 6 includes): the AR4.3 design keeps one direction or merges them (criterion 6) |
| `game/FrontEnd` | 9 | login, the server list, character selection and creation (6,704) | the logic of the entry screens; each scene's script stays in `Scenes` |

### Anatomy of a game module

Each module is a vertical slice (what is spread today over `Scenes/`, `UI/` and `World/`):

```text
game/Guild/
  Guild.h            the public header: what other modules may use
  GuildState.h       its part of the original's .bss (the globals it owns)
  In/                one handler per incoming packet:  GuildList.cpp, GuildJoinResult.cpp...
  Out/               one action per player intent, which sends its packet
  GuildWindow.cpp    the window and its drawing (if it has one)
  GuildMarks.cpp     its own logic
```

- **A handler** (`In/`) is a free function of 40 to 80 lines: it receives the packet already read
  with its layout and the parts of the state it touches, **declared in the signature**:
  `void OnGuildList(const Protocol::Guild::ListView& packet, GuildState& guild, Kernel::Messages& messages);`
  It does not know the session or the sockets. What it needs from outside (sound, sending,
  messages) comes through a few stable ports, not through bags of `std::function`.
- **An action** (`Out/`) is an intent ("request the guild list") that validates against the state
  and sends its packet with `Protocol::Write`.
- **The state** of each module is a plain struct with the original's fields it owns. The
  `GameContext` in `Kernel/` owns them (the composition root) and lends each its own. It is not a
  service locator: nobody asks it for "everything" (OpenMU's `IGameContext`, with 20 properties
  and downcasts, is what we avoid).
- **Public headers:** `Guild.h` (what others use), `GuildFwd.h` (forward declarations) and, if
  needed, `GuildDefines.h` (enums and ids, no logic), like cMaNGOS's `*Defines.h`. A `<Map>.h` or
  `<Event>.h` gathers the ids shared by the files of that map or event, like `molten_core.h` in
  TrinityCore.
- **Ordered extension points.** When a module has to ask several siblings something (before
  walking on a click, `GameMovement` asks whether the Crywolf camera tour, its contract, the events'
  ticket window or the chat own the mouse), it declares a port with a fixed list of functions that
  `app/` assembles in the original's order, like cMaNGOS's `ScriptLoader.cpp` with its `AddSC_`
  calls in written order. There is no runtime registration and no subscription order: OpenMU's
  plugins have no order and TrinityCore's `FOREACH_SCRIPT` follows registration order, and in
  MuRet the order of calls and `rand()` is part of the contract.
- **Reference sizes** (for review, not for blind cutting): a handler up to ~80 lines, a file up to
  ~400, a function up to ~60. The exception is the transcription of a `switch` from the original
  (`0x518cf0`, `0x530860`, `0x5208a0`), which is marked and may stay long, split into one function
  per `case` when the `case` is long.

### Dispatch: one table, one handler per packet

```cpp
// game/Session/GameServerOpcodes.cpp (illustrative shape: except for F3 06, the values are examples)
inline constexpr OpcodeTable GameServerOpcodes{
    Route<&Player::OnLevelUpPointResult>{C1, 0xF3, 0x06, 0x4d3200},
    Route<&Guild::OnGuildList>{C2, 0xNN, NoSubcode, 0x4dXXXX},
    Unported{C1, 0xNN, 0xNN, 0x4dXXXX},
    // …
};
```

- **One row per `case` of the original dispatcher** (`0x4e2b10` and its sub-dispatchers), with its
  address. What is still unported is an `Unported{va}` row, and a test compares the table with the
  research's list of `case`s: a missing packet is a row, not silence. It is tracking, in the code.
- `Route<&Fn>` deduces from the handler's signature which layout to read and which parts of the
  `GameContext` to pass, at compile time and at no cost (no reflection, no registration at
  startup). The rows the original copies into the ring at `0x77f27b4` before the handler (`0x0B`,
  `0x12`, `0x1C`, `0x24`, `0x2A`) say so in a column.
- **The order does not change:** today the handler runs inline inside dispatch (`ApplyWorld` is
  synchronous) and it stays that way. `App/WorldEvent.h` (the 124-type `variant`),
  `ISessionEventSink`, `SessionEventApplier` and the 88 `On*` of `ServerSession` are deleted, and
  `ServerSession` becomes `Session/Connection`: keepalive, serial, keys and the login flow.

### State: from the `.bss` in two aggregates to parts per module

- **The globals registry.** `trazas.tsv` already has every citation of a data address (from
  `0x6b8000`) with its file and symbol. With it, the reference generator builds `globals.tsv`:
  original address → MuRet field → module. It is TrinityCore's "UpdateFields" for our case: where
  each global of the original lives in MuRet, and which module owns it. It guides the split of
  `WorldRuntime`.
- **The split.** Of the 94 fields of `WorldRuntime`, 17 are used by a single module (they move
  first; measured with the 26 candidate modules of `Scenes/`, so with the 58 of the revision there
  are fewer). The ones many modules use (`hero` in 22 modules, the sound request in 21,
  `ownRecordDepth` in 20, the map in 19) form a small core in `Kernel/`; the sound request becomes
  the port `Sound` implements, and fauna, its own state in `Fauna`. Mouse arbitration
  (`movement.pendingClick`, 125 uses; `ui.mouseOnUi`; `ui.field`) is its own state. `WorldState`
  splits into `ActorPool`, `PlayerState`, `MapState`, the frame scratch and fauna, and stays as the
  root that gathers them. `world.Hero()` replaces the 210 `Actors()[…hero]`.
- **Contiguous data is not split.** A struct that copies contiguous memory of the original because
  an overflow or a copy crosses it (the `TooltipLines` row, Q-C591; the two fields of
  `CharacterBalloons`, Q-C1503; the blocks a `REP STOSD` or a reset function clears together) stays
  whole, marked with `// Contiguous in the original: …` and its row in 07.

### Scenes as frame scripts

`GameScene` stops doing nine jobs: it becomes **the ordered list** of steps and draws of the game
frame, each with its address (`0x5208a0` → effects, …), calling into the modules. The order of the
original's calls lives in one place (the script), and the work in the modules. The same goes for
login and selection. `main.cpp` (1,010 lines) stops composing the running client: `App::GameClient`, in
`app/App`, is the composition root that `main` builds with the real devices and the behavior golden with
doubles, and `main.cpp` keeps the phases of the startup
([plan](../plans/2026-11-24-thin-main.md)).

### Addresses, offsets and citations

- **Comment citations stay** (`// 0x4d3200`): there are 55,800, tracking reads them, and a
  constant per address adds nothing that `funciones.tsv` does not already have. They go in English
  and on one line, with the address and at most a short label; any other comment only if strictly
  necessary (Emanuel, 2026-11-21; CODING_RULES §9). Today's Spanish comments are converted in a
  comments PR after each move, and its squash is listed in `.git-blame-ignore-revs` like the
  move's.
- **Layouts that matter carry offsets in the type:** packets (`Field<T, Offset, Endian>`) and the
  `Data\` formats (`static_assert(offsetof(...))` and `sizeof`). Today there are 9 layout
  `static_assert`s in all of `src`.
- **In-memory state does not copy the original's layout**, so a `static_assert` proves nothing
  there: the name by address (`dword723bd8`) becomes a meaningful name and the address stays in
  the comment and in `globals.tsv`.

## Non-negotiable: fidelity

1. **The behavior golden.** Before anything moves, a harness runs scripted scenes without a window
   (login → selection → Lorencia; walking and fighting with skills; inventory, shop and chat;
   entering an event) with a fake clock, recorded packets and scripted input, and stores per frame
   a hash of the draw, the packets sent, the `rand()` count and a state summary. Every refactor PR
   leaves it byte-for-byte identical.
2. **Moves do not touch logic.** A script generates them from `module-map.tsv`, and
   `check_pure_move.py` verifies that the lines of each moved file are the same (except
   `#include`, CMake and docs). If `main` changes, the script is rerun instead of resolving
   conflicts by hand.
3. **Order lives in two places:** the scene scripts and the opcode table. No unordered observers:
   a fan-out is an ordered list that copies the original.
4. **"Drawing does not change state" is not adopted** (MuMain's ADR 0003): where the original
   changes state while drawing, the function receives that mutable part and says so.
5. **Current tests and goldens stay green** on Linux and Windows, Debug and `math-release`.
6. **Contiguous stays contiguous** (see above), and the 22 functions ported twice are unified only
   if both copies give the same result (otherwise one of them is a port bug: separate PR).

## Not lost: tracking

- **Hand classifications** (`entradas/cortes-vivos.tsv`, 1,761 rows; `sin-cablear`, `sustitutos`,
  `chat-funciones` and the `V` rows reachable by playing) are matched today by `file` + `line`, so
  any move orphans them. They become matched by a **stable anchor** (the addresses, the cut's name,
  the normalized line text, and the symbol), and the generator resolves the current `file:line`.
  Moves are also recorded in `entradas/moves.tsv` (old path → new path).
- **Each citation's wave** comes from `git blame`. A commit without `CLn` inherits the previous
  wave, so a move would attribute thousands of citations to the wrong wave. Blame starts running
  with `-M -C` and `--ignore-revs-file .git-blame-ignore-revs`, refactor commits carry `AR` and
  inherit no wave, and the squash of every move and every comments PR is listed in
  `.git-blame-ignore-revs`.
- **Every pending item has a module:** the reference adds a `module` column to `funciones`,
  `cortes`, `huecos` and `vm`, and `consultar.py module game/Guild` lists what that module still
  lacks (live cuts, gaps, VM steps, rows of 08, `Unported` packets). A gap takes the module of its
  callers.
- **`modules.tsv`** (in `Client/`) is the single source of the modules: folder, layer, what it may
  depend on. `check_layers.py`, CMake and the reference read it.

## Risks

| risk | mitigation |
| --- | --- |
| a move changes the order of calls or `rand()` | the behavior golden; moves without logic |
| the reference tables miscount after moving | the stable anchor, `moves.tsv` and blame with `-M -C`; regenerated before and after each move and the numbers compared |
| clashes with port PRs | moves are scripts that get rerun; a module being moved receives no port work that day |
| the behavior golden does not run without a window | `GameScene` already runs without a window with real data and `RecordingFrameTarget` (`tests/scenes/test_game_scene_complete.cpp`), and login with the session (`test_login_scene_session.cpp`); if a scene cannot, the harness starts with the ones that can |
| MSVC (C1128, C4242, iterators) | every milestone passes the Windows CI; the rules in `client-msvc-ci-gotchas` |
| the plan eats weeks of port | see the lane split in the plan; the refactor never takes all 3 agents |
| 58 targets are a lot for CMake and CI | they are small static libraries: each test links only its own (today all 728 link the whole `ClientLib`), and `ccache` already caches per unit. If a module ends up with a single file nobody else uses, it merges with its neighbor (criterion 6) |

## Left to the builder

Whether `Hero` merges with `Player` and `Joints` with `Effects` (AR4.2 and AR4.3), the exact split
of the low-confidence files in the map (50 in the revision), the internal shape of `Route<>` and of
the `GameContext`, and when to split a `switch` of the original into one function per `case`.
