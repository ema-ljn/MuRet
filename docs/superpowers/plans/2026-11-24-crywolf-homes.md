# Crywolf's homes: findings and plan for where an event's code lives

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** every piece of the client that names Crywolf has one owner that a rule can tell, `game/Events/Crywolf` and
`game/MapScripts/Crywolf` stay two modules as the spec has them, and the pieces that do not depend on AR4 (the music,
the names of the stages, the map's name, the module map) get their home now.

**Architecture:** in `main.exe` Crywolf is one source file per map with three labelled blocks (the raid, the map's
objects, its monsters), a camera tour class and the `BD` receive wrappers, reached from 26 generic functions by calls
written by hand, in an order that changes from one chain to the next. In MuRet it is 5,742 lines in 98 files that the
module map sends to 34 modules. The plan answers "whose is this?" with the key the original dispatches on, keeps the
event (what a packet, a window, an NPC or the stage reaches) apart from the map script (what a per-map chain reaches
with the map as its gate), keeps every chain a written sequence of guarded calls, and puts what every layer compares
(the map, the stage, the occupation state) at layer 6.

**Tech Stack:** C++20, GoogleTest, the behavior golden (`Client/tests/characterization`), `check_layers.py`,
`module-map.tsv`.

**Spec:** the analysis "Dónde vive el código de Crywolf" (Spanish,
https://claude.ai/artifact/RBWKKGf8fd5NNmzm23BWUM) and its three reports (`inventario.md`,
`original-y-referencias.md`, `otros-eventos-y-capas.md`, with the tables and the scripts that reproduce each number,
in the project files under `cliente/organizacion-crywolf/`); the refactor design
[`2026-11-20-client-architecture-design.md`](../specs/2026-11-20-client-architecture-design.md), "The modules, one by
one"; the master plan [`2026-11-20-client-architecture-refactor.md`](2026-11-20-client-architecture-refactor.md),
"Epic AR4"; [`CODING_RULES.md`](../../CODING_RULES.md) §7 "Named values" and its plan
[`2026-11-23-named-values.md`](2026-11-23-named-values.md); the thin main plan
[`2026-11-24-thin-main.md`](2026-11-24-thin-main.md), whose Task 2 is PR 445.

Measured on PR 445's head, 8624bfd (2026-11-24): `refactor/client` with the loop's tail already in
`Scenes/WinMainLoop`. The original was read through the repository's research (`coverage/referencia/funciones.tsv`,
`06-render-escenas-y-ui.md`, `04-protocolo.md`), not from the ASM again. The names of its functions and files are not
in the binary: they come from the public MuMain source of a later season (a593de5), matched by order and size, and
are marked "later source" below. The other projects were read from their default branches the same day.

## The answer in short

- **Yes, Crywolf's code belongs with Crywolf, and what was in `main` was the smallest part of it.** After PR 445 that
  part is 13 lines of `Scenes/WinMainLoop.cpp`. It is one of 216 pieces: 5,742 lines in 98 files. The two Crywolf
  folders of the module map hold 47% of them; the rest is 84 whole functions inside generic files, 85 branches and 22
  fields that only carry its state.
- **The original has it in one place, but as a bag of hooks.** One source file holds the map, its monsters and the
  raid (35 functions, 29,683 bytes); 30 of the 35 are called from outside it, and 26 have exactly one caller. Generic
  code reaches them by chains written by hand.
- **Not everything that says Crywolf is Crywolf's.** The owner of a piece is the key the original dispatches on. A
  case of the effect, particle or joint switch belongs to that module, and a monster keyed by its model belongs to the
  monsters, whatever the name says. What a `BD` packet, the HUD, an altar or the stage reaches is the event's. What a
  per-map chain reaches with the map as its gate is the map script's.
- **Two modules** (Emanuel, 2026-11-24): `game/Events/Crywolf` and `game/MapScripts/Crywolf` stay apart, as the spec
  says. The map script reads the event's occupation state; the event never calls the map script.
- **The chains stay written.** The order of the maps differs from chain to chain, a link may decline, and two of
  Crywolf's hooks are keyed by the model and not by the map. The spec's sentence about a `constexpr` table by map id
  is corrected: such a table holds per-map data only.
- **Three things can move now and keep their place later:** the music (`0x430250` is split today between the engine
  and the loop's tail), the names (map 34 is written in 43 places under five names, and stage 5 has two names that
  disagree), and four files the module map sends to the wrong module. Everything else moves with its milestone, and
  the rule says where to.

## What `main.exe` does

Three pieces of source are Crywolf's own: 80 functions, 37,819 bytes.

| piece | range | functions | bytes | what it holds |
| --- | --- | ---: | ---: | --- |
| the map's unit (later source: `GMCrywolf1st.cpp`) | `0x42fdc0..0x437675` | 35 | 29,683 | the map's objects, its monsters and the raid, with all its globals as file statics |
| the camera tour | `0x403220..0x404cd9` | 36 | 6,390 | one class with one static instance (`0x6e57a8`) and its own copy of the stage (`+0x154`) |
| the `BD` wrappers | `0x4e0fb0..0x4e16bf` | 9 (5 dead) | 1,746 | thin receivers inside the protocol unit that forward to the map's unit |

The unit before it (`0x42ee20..0x42fd86`) is map 35, another map with the same shape. It is not part of the event.

The map's unit is laid out by theme, and the later source's header labels the same three blocks:

| block | functions | bytes | share |
| --- | ---: | ---: | ---: |
| monsters, with the mist and the screen smoke | 9 | 19,279 | 65.0% |
| the raid: state, packet setters, HUD, contract window, music, reset | 21 | 8,518 | 28.7% |
| the map's objects, with the map predicate `0x4303c0` | 5 | 1,886 | 6.4% |

**Generic code calls in by hand.** Twenty-six generic functions call into those units, and at least nine more hold a
Crywolf branch with no call. Ten of the calls are links of a per-map chain: the call is unconditional and the guard
is the first thing inside the hook.

| generic function | Crywolf's hook | the chain, in call order |
| --- | --- | --- |
| `0x606660` object creation | `0x4303d0` | Chaos Castle, Kalima, the siege, map 31, map 35, Aida, **Crywolf** |
| `0x604ab0` object step | `0x4303e0` | Kalima, the siege, 31, 35, Aida, **Crywolf** |
| `0x602ed0` object effects | `0x430510` | `0x4059d0`, Kalima, the siege, 31, 35, Aida, **Crywolf** |
| `0x6002e0` object body | `0x4306a0` | Kalima, the siege, 31, 35, Aida, **Crywolf** |
| `0x5141e0` monster creation | `0x430c90` | the siege, 31, 35, **Crywolf**, Aida |
| `0x505de0` character step | `0x431470` | Kalima, the siege, 31, 35, **Crywolf**, Aida |
| `0x4fcee0` weapon trail | `0x432bd0` | 31, 35, **Crywolf**, Aida (all four always run) |
| `0x50ba30` character draw | `0x434180` | 31, 35, **Crywolf**, Aida |
| `0x4f5b70` attack step | `0x4324c0` | Kalima, 31, the siege, Aida, 35, **Crywolf** |
| `0x4f4a10` attack on arrival | `0x437580` | Kalima, the siege, 31, Aida, **Crywolf** |

Three facts of that table decide the mechanism:

- **The order of the maps is not the same in every chain.** Aida comes before Crywolf in the four object chains and
  after it in four monster chains; the attack chain has an order of its own. No single registration order reproduces
  the ten.
- **Two hooks are keyed by the model, not the map** (`0x431470`, `0x432bd0`), and a third (`0x4339d0`) only inherits
  its map guard from its caller.
- **Generic work sits between the links**: the object-effects chain runs after a common `rand()` (`0x602ef4`).

The other calls have a fixed place among their siblings: HUD children 21 (`0x4365c0`) and 26 (`0x436240`), the UI
step (`0x435bd0`) and the tour's step (`0x403590`) inside the game step, the music `0x430250(map)` among the other
maps' music tests in the frame, the occupation read at the map load (`0x42fe50`), the resets at the return to
selection. The `BD` packets go through the switch `0x4e2b10` by sub-code, with no map test at the dispatch.

**Crywolf is not "what map 34 does".** Seven behaviours run on any map or by another key: `BD 00` stores the state
and reloads the light and the `.att` of whatever map is loaded; `BD 02`, `07`, `08` and `0C` store or create on any
map; HUD child 21 draws the results table on any map while its flag is set; `0x430250` stops Crywolf's track on every
map that is not 34; the camera tour runs by its own copy of the stage, with no map test; `0x431470` and `0x432bd0`
act on a monster model wherever it is; the chat rows move with the stage.

**The state.** The unit's 33 file statics are two contiguous blocks (`.data` `0x6c73cc..0x6c73fb`, `.bss`
`0x7239f4..0x723c13`). Four values are read by everything: the occupation state `0x723c04`, the stage `0x723c05`,
the statue's HP `0x723c08` and the altar states `0x6c73cc`. The other 29 are the HUD's, the contract window's and the
results table's. The block ends with two function statics of the object-effects hook (`0x723c0c`, `0x723c10`). Three
globals that Crywolf's objects write are far from the block and are not its own: the weather roll `0x5763358`, the
screen smoke flag `0x576353c` and the object draw counter `0x5763538`.

## What MuRet does today

| what a piece is | pieces | lines | files |
| --- | ---: | ---: | ---: |
| a whole Crywolf file | 25 | 2,696 | 15 |
| a whole Crywolf function, type or table in a generic file | 84 | 2,441 | 44 |
| a Crywolf branch inside generic code (median 4 lines) | 85 | 547 | 50 |
| a field or parameter that only carries its state | 22 | 58 | 21 |
| **total** | **216** | **5,742** | **98** |

- **Where it sits.** The module map's `game/Events/Crywolf` (10 files) and `game/MapScripts/Crywolf` (5 files) hold
  the 2,696 lines. The largest block outside them is the monsters: 1,245 lines in `game/Monsters` and
  `game/Monsters/Drawing`. Then the `BD` handlers, the reset and the tour's state in the flat `game/Events` (342
  lines of `Scenes/EventPackets.cpp` and `Scenes/EventGlobals.h`). One source file of the original is spread over
  ten target modules.
- **Thirteen files of the inventory never say "Crywolf"** (`Scenes/CameraTour.cpp`, 809 lines, is one). They were
  found by the addresses they cite.
- **The same value has many names.** Map 34: 19 named definitions under five names (`CrywolfMap`, `Map34`,
  `Crywolf`, `Particle67ColorMap`, `SpecialMap::Crywolf`) and two types, 8 bare comparisons and 16 table rows, in 28
  files. The stage: 16 definitions under 12 names for five values. Stage 3 is `DefenseStage`, `FirstActiveCrywolfStage`,
  `FirstCrywolfLockStage` and `UndoStage`; stage 5 is `CrywolfFightStage` in `Scenes/EventPackets.cpp:239` and
  `CrywolfResultStage` in `Scenes/WinMainLoop.cpp:29`. The test "stage 3 or 4, in map 34" (`0x435a00` with
  `0x4303c0`) is written eight times.
- **The state has three owners in one struct.** 56 fields: `CrywolfInterface` (29), `CrywolfGlobals` (6, with the
  interface inside) and `CameraTour` (21, under `EventGlobals`). `CrywolfGlobals` mixes the event's `stage` and
  `state`, the object hook's two statics (`frameCount`, `glowRotation`) and two generic globals (`weather`,
  `sandOverlay`). Outside Crywolf's own files the game reads `stage` (from eight target modules) and `state` (from
  four) and touches four of the 29 interface fields.
- **The engine knows a Crywolf stage.** `0x430250` is one function in the original. In MuRet its track choice is
  `Audio::Music::StepCrywolf` (`Audio/Music.cpp:142-184`, `engine/Audio`), its `else` is `Music::StopCrywolfBase`
  (`:61-64`) and its map test and result sounds are the block PR 445 left in `Scenes/WinMainLoop.cpp:47-59`.
- **The graph.** `check_layers.py` reports 1,382 violations, all in the baseline; 31 rows name a file of Crywolf or
  of its camera tour. Taken together, the two Crywolf folders are five mutual pairs with their callers (`Actors`,
  `Hud`, `Monsters`, `Movement`, `WorldObjects`: 23 includes) and two upward includes, both from
  `Scenes/CameraTour.cpp`. The back edges have three causes: a Crywolf function that takes its caller's whole context
  (`GameHudContext`, `GameObjectPass`), the contract reading the movement's click state, and a piece of the
  pathfinder (`0x4eac0a..0x4eac41`) written as a Crywolf function that 14 call sites pass along.
- **`may_use` is today's includes, frozen.** For all 35 modules that have one, it equals their same-layer includes
  minus the mutual pairs. Read alone, the declared graph has one cycle of 19 modules at layer 9
  (`Events -> Chat -> FrontEnd -> Events` is its shortest path through `Events`), and `check_layers.py` prints cycles
  without failing. Nobody's `may_use` names `MapScripts`, although the object chains call every map script.
- **The split is the one the graph accepts.** The object, weather and HUD pieces need the types of `WorldObjects`,
  `Weather` and `Hud`, which `game/Events` cannot use without closing a cycle, and the altar and the contract are
  called by `Npc`, `Hero` and `Skills`, which may use `Events` and not `MapScripts`.

## What other projects do

| project | where an event's code is | how generic code reaches it |
| --- | --- | --- |
| MuMain, 2021 tree (a593de5) | one file per map, `GMCrywolf1st.cpp`, everything public; the tour is a class apart | 42 call lines in 12 files, the same chains as the binary link for link, plus 18 inline map tests |
| MuMain today (9235f10) | split by layer over four folders (map, UI, cinematic, network) | the same chains; a later registry by map (`MapProcess`) is appended at the end of each chain, and Crywolf was never moved into it |
| TrinityCore (ef88cb1) | a folder per instance with an ids header (`Karazhan/karazhan.h`) | narrow interfaces owned by the core (`ZoneScript`, `InstanceScript`), the one script found by id; and a broadcast of 86 hooks in registration order |
| cMaNGOS (da5731f) | the same folders | one written loader in source order (`ScriptLoader.cpp`) and, where the core must pick one, a `switch` by id |
| OpenMU (a4768ac) | a vertical folder for the rules (`GameLogic/Crywolf/`, 23 files), with the handlers, the view and the data in three other projects | plug-in interfaces without order |

What transfers to a client that must keep the original's order of calls: the folder per event with its ids header,
the narrow set of hooks as a shape, and the written ordered list. What does not: the broadcast in registration order,
the registry walked until one map answers, and the plug-ins without order. The strongest evidence is the authors'
own: they built a registry by map later and left Crywolf and every older map as written chains.

## The target

### Who owns a piece

The owner is **the key the original dispatches on**:

1. **An effect, particle or joint type, or a monster model:** `Effects`, `Particles`, `Joints` or `Monsters`,
   whatever the name says. It stays there, in a function or file named for the map.
2. **An opcode:** a handler, a row of the opcode table (AR2), in the module whose state it writes: the event for
   eight of the nine `BD` sub-codes, `ChaosMix` for `BD 09`.
3. **The stage, a window, an NPC of the event:** the event, `game/Events/<Event>`.
4. **The map alone, from a per-map chain:** the map script, `game/MapScripts/<Map>`.
5. **A value every layer compares** (the map id, the stage, the occupation state): layer 6, in the home of its space
   (`Maps/MapId.h`; beside the packet's layout in `game/Protocol`), by CODING_RULES §7.

### The two modules

| | `game/Events/Crywolf` | `game/MapScripts/Crywolf` |
| --- | --- | --- |
| what | the raid: what a packet, the HUD, an altar or the stage reaches | what map 34 does: what a per-map chain reaches with the map as its gate |
| in the original | the "raid" block (21 functions, 8,518 bytes), the live `BD` wrappers and the camera tour (36 functions, 6,390 bytes) | the "objects" block (4 hooks, 1,875 bytes), the mist `0x435600` and the screen smoke `0x435930` |
| in MuRet today | about 2,480 lines in 40 pieces | about 650 lines in 14 pieces |
| holds | the ids and the contract messages; `stage`, `state` and the 29 interface fields; the eight `BD` handlers (`00`, `02`, `03`, `04`, `05`, `07`, `08`, `0C`); the two requests (`C1 04 BD 00`, `C1 06 BD 03`); HUD children 21 and 26 and the UI step `0x435bd0`; the contract (`0x435b00`, `0x435ad0`, the altar click); the music `0x430250`; the reset `0x42fdc0`; the camera tour in its own files with its own state | the object hooks (`0x4303d0`, `0x4303e0`, `0x430510`, `0x4306a0`) with the altar meshes; the water bubbles (`0x5fa695`, inline in the original's terrain code, handed to the terrain by the scene in MuRet); the mist and the screen smoke, the map's ambience, which today sit with their callers in `Weather` and `Hud`; the object hook's two statics (`0x723c0c`, `0x723c10`) |
| reads from the other | nothing | the occupation state and the statue's light, one way (`MapScripts` may already use `Events`) |

Neither holds:

- **The monsters** (7 hooks, 18,312 bytes; 1,200 lines in MuRet). Recommended: they stay in `game/Monsters`, in
  files named for the map. Two hooks are keyed by the model, each sits in a chain with every other map's monster
  hook, and map 35's helpers call four of Crywolf's. It is the same question for Kalima, the siege and maps 31, 33
  and 35, so it is decided once for all of them (Task 7).
- **The effect, particle and joint cases** that carry its name (`CrywolfFanta`, the death spirit `0x147`, the
  missile tail): they are cases of the generic switches, and two of them also serve the siege.
- **`BD 09`**: it writes the chaos machine's rate. It goes to `game/ChaosMix`.
- **The weather roll, the smoke flag and the object draw counter**: generic globals that `Weather` and `Hud` own.
- **Map 35.**

### How generic code reaches them

- **A value or a short condition inside the generic function's own code:** the branch stays where it is and takes
  its names from layer 6. A predicate replaces a comparison only where the original calls one (`0x4303c0`,
  `0x435a00`). Example: the leaves' test of the map and the weather roll (`Scenes/GameLeaves.cpp:220`).
- **The original has a `CALL` there:** a free function of the event or the map script, called in place under the
  same guard, with a narrow signature: its own state, plain values and types from below it. MuRet already does this
  in most places (`Scenes/GameHud.cpp:1816`, `Scenes/WorldObjectsDrawing.cpp:551-552`); what is missing is the
  narrow signature.
- **The caller is below the event:** first ask whether the calling code is misplaced. `Audio::Music::StepCrywolf`
  needs no port: its caller is in `game/Scenes`, above both. Only if it is not misplaced, a port with fixed, named
  slots, assembled in `app/`.
- **Never** a run-time registration, a broadcast or a table by map id over the chains. A `constexpr` table by map id
  is right for per-map data (the name, the folder, the track, a count), tested over every id from 0 to 255.

The direction between the hosts and what they call is one decision for every map and event, taken before AR4.1
(Task 7). Recommended: the hosts (`WorldObjects`, `Monsters`, `Hud`, `Movement`, `Npc`, `Weather`) may use the event
and the map script, never the reverse, and the types a script takes (`WorldObject`, the object pass) move below both.

### The other events and maps

The reports measured them with the same scripts (`eventos.tsv`). The rule gives the following, as a recommendation
that each milestone's design PR confirms:

- **A map without an event is `game/MapScripts/<Map>`.** Kalima is one: the ten files the module map sends to
  `game/Events/Kalima` (800 lines) are map code, and `IsKalimaMap` goes to `Maps/MapId.h` (AR1 Task 5).
- **Devil Square, Blood Castle and Chaos Castle stay together** in the flat `game/Events`, around the object and the
  windows they share, with files per event; `Scenes/EventMapObjectFrame.*` joins them.
- **Castle Siege** is already two modules, `game/CastleSiege` and `game/MapScripts/CastleSiege`, like Crywolf.
- **Lost Tower, Devias and Lorencia stay in `WorldObjects`:** the original has them as cases of the main switch.

### Where it fits in the AR plan

Tasks 2 to 6 below keep their place when the files move, because none depends on a design that is still open. The
rest of Crywolf moves with the milestone of the module that hosts it today:

| piece | milestone | what the rule adds |
| --- | --- | --- |
| the eight `BD` handlers and the two requests out of `Scenes/EventPackets.cpp` and `App/ServerSession.cpp` | AR2.3 | `In/` and `Out/` of `game/Events/Crywolf`; `BD 09` to `game/ChaosMix`; `BD 00` tells the tour its stage after it stores its own, in that order (`0x4e5965`, then `0x4e597f`) |
| `CrywolfGlobals` and `CameraTour` out of `WorldRuntime` | AR3.2 | three owners: `stage`, `state` and the interface to the event; `frameCount` and `glowRotation` to the map script, after checking that no block copy crosses `0x723c04..0x723c13`; `weather` and `sandOverlay` to `Weather` and `Hud`; the tour's state stays its own struct |
| the altar goal of the pathfinder (14 call sites) | AR4.2 | it lives inside the single port of `0x4eac00` and calls the two predicates; the includes from `Actors`, `Hero` and `Skills` disappear |
| the monsters | AR4.2 | Task 7's answer |
| the object hooks stop taking `GameObjectPass`; `StepCrywolfObject` (`World/WorldObjectStepServices.cpp`) joins them; the altar meshes leave `Scenes/CharacterDrawing.cpp:1920-1951`; `CreateArrivalSparks` leaves `Scenes/CameraTour.cpp:734-770` for `game/Effects` | AR4.3 | the chains stay written; no table by map id |
| HUD children 21 and 26 stop taking `GameHudContext`; the UI step leaves `Scenes/GameUi.cpp:301-330` | AR4.5 | they stay steps of the HUD's script; the planned `PanelRegistry` does not absorb them |
| the camera tour takes its creations (monsters, joints) and the draw tail as arguments | AR4.5 | its two upward includes disappear |

Sixty baseline rows into `Events`, `CastleSiege` and `MapScripts` fall due before AR4.5 designs `Events` (20 in
AR4.1, 8 in AR4.2, 27 in AR4.3, 5 in AR4.4), because a milestone is done when its modules have no violation. That is
why the direction is decided before AR4.1.

### Decisions

Emanuel, 2026-11-24: the plan now, and two modules as the spec has them.

Recommended and written into the tasks unless Emanuel says otherwise:

1. **The stage and the occupation state live at layer 6**, beside the `BD 00` layout in `Protocol/Events.h`, and not
   in the event's header. `game/Ui` (layer 8) compares the stage in the message area and the chat log, where the
   original reads `0x723c05` directly, and a layer 9 header cannot reach it. The master plan's "each event's ids in
   `<Event>.h`" (AR4.5) stands for the ids only the event and its callers above use.
2. **Their names come from the later source's enumerations, matched by value**: stages none, first notice, second
   notice, ready, start, end, end of cycle (0 to 6); occupation peace, occupied, war (0 to 2). They fit what 1.02 does
   with each value (the tour at 2, the altars at 3, the clock at 4, the result at 5; the smoke with 1, the lit altar
   with 2).
3. **The first code is the music**, the piece that was in `main`.
4. **The monsters stay in `game/Monsters`** until Task 7 decides for every map.
5. **The camera tour is part of `game/Events/Crywolf`**, as the spec says, in its own files and with its own state.
6. **The declared `may_use` graph gets a report** (Task 6), and the done criterion of a milestone gains "in no
   declared cycle" only once the report exists and Emanuel agrees.

### Found on the way

Two differences with the original, verified in MuRet's code against the research documents. They are not part of this
plan; each is a port fix that needs its own yes.

- **`BD 09` writes a field nobody reads.** `0x7d2dc9d` exists twice: `CrywolfInterface::byte7d2dc9d`, which the
  packet writes (`Scenes/EventPackets.cpp:1441`), and `NpcWindowState::field7d2dc9d`, which the chaos machine reads
  (`Scenes/ChaosBoxDrawing.cpp:267,275`). Each has its test, so both are green and the bonus never reaches the window.
- **The `C1 04 BD 00` at world entry is never sent.** The research has it at `0x4c421a`, in the reply to `F3 03` on
  map 34. `LoadTheWorld` does not send it, and the comment that excuses it (`Scenes/WorldEntry.cpp:83-84`) is stale:
  `LoadMap` no longer cuts on that map. A character that logs in inside Crywolf does not ask for the state.

## Global Constraints

- Fidelity first. No task changes the order of the calls, of the `rand()` draws or of the music's `Stop` and `Play`.
- Every task is a refactor: no golden and no test expectation changes. A test that moves keeps its assertions.
- A comparison keeps its shape (CODING_RULES §7, rule 4): its operator, its cast and its citation stay, and a
  predicate replaces it only where the original's code at that address calls one.
- Every address cited in a block that moves is cited where the block lands; the PR description gives the count of
  `0x` citations in the files it touches, before and after.
- Code that lands in a new file is cleaned there: named values, comments only if strictly necessary and in English,
  its own test.
- New files go in today's folder (`Scenes/`) and add their row to `Client/docs/research/coverage/module-map.tsv` in
  the same PR; `check_layers.py` prints `0 new`. No file is renamed outside `move.py`.
- PRs target `refactor/client`. One task per PR, and Emanuel says yes before the next.
- GitHub CI is the build and the final word.

## Review Focus

- **No playback device.** `runtime.audio` is null in most tests and in a client without one. The music must not
  crash, and the result's sound is still recorded in `lastSoundId`. Task 2 pins it.
- **A stage that skips one** (`BD 00` with 4 while the track of 2 plays). `0x430250` stops only the previous stage's
  track, so the new one replaces the stream without a `Stop`. Task 2 pins it, so nobody "tidies" it into a stop of
  every other track.
- **Leaving the map in the middle of a stage.** On another map only `crywolf1st.mp3` is stopped; a stage's track
  keeps playing until another track replaces it or the world's music is stopped. Task 2 pins it.
- **A stage outside 0 to 6 from the server.** The music plays the map's track, as for 0, 1 and 6, and the predicates
  are false. Tasks 2 and 3 pin it.
- **A name that changes value.** `CrywolfEndStage` is 6 in `Scenes/EventPackets.cpp:240` today and 5 in the home.
  Task 3 replaces by value, never by name, and the tests of the `BD 00` handler, which use literals, stay untouched
  as the proof.
- **A site of the sweep that no test reaches.** The message area moves its rows with the stage in 3 or 4
  (`UI/MessageArea.cpp:253`), and no test sets that stage (`grep -rn crywolfStage Client/tests` finds only the chat
  log's). Task 3 pins every such site before it touches it.

---

## Tasks

Task 1 is this document. Task 2 needs PR 445 merged and nothing of AR1. Task 3 needs Task 2. Task 4 needs AR1 Task 5
(`Maps/MapId.h`). Tasks 5 and 6 depend on nothing. Task 7 is a decision, before AR4.1.

### Task 1: the plan

This PR. Title: `docs(refactor): plan the homes of Crywolf's code and the rule for events and maps`.

**Files:**
- Create: `docs/superpowers/plans/2026-11-24-crywolf-homes.md`
- Modify: `docs/superpowers/specs/2026-11-20-client-architecture-design.md` ("The references", criterion 5, the rows
  of `game/Events/<Event>/` and `game/MapScripts/<Map>/`),
  `docs/superpowers/plans/2026-11-20-client-architecture-refactor.md` (the open decisions, AR3.2, AR4.3 and a
  paragraph under "Epic AR4"), `docs/superpowers/plans/2026-11-23-named-values.md` and
  `docs/superpowers/plans/2026-11-22-ar1-engine-apart.md` (the fourth engine site),
  `.claude/skills/muret-module/SKILL.md` (who owns a file that names an event or a map)

- [x] **Step 1:** write the findings and the tasks.
- [x] **Step 2:** correct the spec's sentence about the table by map id, and point the master plan at this document.

### Task 2: Crywolf's music in the event

Title: `refactor(crywolf): give the music of 0x430250 one home in the event`, branch `refactor/crywolf-music`.
Skills: `refactoring` (Move Function), `muret-module`, `superpowers:test-driven-development`.

**Files:**
- Create: `Client/src/Scenes/CrywolfMusic.h`, `Client/src/Scenes/CrywolfMusic.cpp` (target `game/Events/Crywolf`)
- Modify: `Client/src/Scenes/WinMainLoop.cpp` (`StepTheGameMusic`, lines 47 to 59, and the five constants of lines
  26 to 32), `Client/src/Audio/Music.h`, `Client/src/Audio/Music.cpp` (`StepCrywolf`, `StopCrywolfBase`),
  `Client/src/Protocol/Events.h` (the stage names), `Client/docs/research/coverage/module-map.tsv`
- Test: create `Client/tests/scenes/test_crywolf_music.cpp`; `Client/tests/audio/test_music.cpp` loses its two
  Crywolf cases; `Client/tests/scenes/test_win_main_loop.cpp` does not change

**Interfaces:**
- Produces, in `Protocol/Events.h`, under `struct CrywolfState`:

```cpp
// The values of CrywolfState::stage (0x723c05), named after the later source's CRYWOLF_STATE_TYPE by value.
inline constexpr std::uint8_t CrywolfNoStage = 0;
inline constexpr std::uint8_t CrywolfFirstNoticeStage = 1;
inline constexpr std::uint8_t CrywolfSecondNoticeStage = 2;
inline constexpr std::uint8_t CrywolfReadyStage = 3;
inline constexpr std::uint8_t CrywolfStartStage = 4;
inline constexpr std::uint8_t CrywolfEndStage = 5;
inline constexpr std::uint8_t CrywolfEndCycleStage = 6;
```

- Produces, in `Scenes/CrywolfMusic.h`:

```cpp
// 0x430250(map), once per frame on every map (0x637700): in Crywolf's map the track of the stage and, at the end
// stage, the result's sound (0x244, or 0x245 with the failure banner in 0x6c73dc); on any other map it stops the
// map's own track.
void StepTheCrywolfMusic(WorldRuntime& runtime);
```

  It is today's code, in today's order: the map test, `Music::StepCrywolf`, the end stage's sound, and in the `else`
  `Music::StopCrywolfBase`. It uses only `Music::Play`, `Music::Stop`, `IPlayback::MusicProgressPercent` and
  `WorldRuntime::RequestSound`. Private to the `.cpp`, each with the address it comes from: the four tracks, the map
  (it moves from `WinMainLoop.cpp` and takes its name from `Maps/MapId.h` in Task 4), the failure banner and the two
  sounds (their home is `game/Sound`, AR4.1).
- `Audio::Music` keeps `Play`, `Stop`, `StopWorld`, `SetEnabled`, `SetBlocked`, `Current` and `StepWorld`. Its
  `WorldTracks` keeps the four Crywolf names, which `StopWorld` needs, until the list leaves the engine (named-values
  plan, Task 3).
- Until Task 3, `Scenes/EventPackets.cpp` keeps its private `CrywolfEndStage`, which is 6. It is found first there,
  so nothing changes in that file; the PR description says the two coexist for one PR.
- No CMake change: sources and tests are picked up by glob.

- [ ] **Step 1: Write the failing tests.** A recording playback notes `"play:<path>"`, `"stop"` and
  `"sound:<id in hex>"` in one list and reports progress 0 unless the test sets it;
  `WorldRuntime runtime; runtime.audio = &playback;`.

```cpp
TEST(CrywolfMusicTest, EachStagePlaysItsTrackAndStopsThePreviousStages)
{
    // map 34, progress 99; stages 0, 2, 3 and 4 in turn
    EXPECT_EQ(playback.events,
              (std::vector<std::string>{"play:Music\\crywolf1st.mp3", "stop", "play:Music\\crywolf_before-01.ogg",
                                        "stop", "play:Music\\crywolf_ready-02.ogg", "stop",
                                        "play:Music\\crywolf_back-03.ogg"}));
}

TEST(CrywolfMusicTest, AStageWithoutATrackOfItsOwnPlaysTheMaps)
{
    // map 34; a fresh runtime for each stage of 0, 1, 6, 7 and 0xff
    EXPECT_EQ(runtime.music.Current(), "Music\\crywolf1st.mp3");
}

TEST(CrywolfMusicTest, OnlyProgressOneHundredEndsTheTrackAndTheNextFrameStartsItAgain)
{
    // map 34, stage 4; steps with progress 99 and 101
    EXPECT_EQ(runtime.music.Current(), "Music\\crywolf_back-03.ogg");
    // a step with progress 100
    EXPECT_TRUE(runtime.music.Current().empty());
    EXPECT_EQ(playback.events.back(), "stop");
    // a step with progress 0
    EXPECT_EQ(playback.events.back(), "play:Music\\crywolf_back-03.ogg");
}

TEST(CrywolfMusicTest, AStageThatSkipsOneReplacesTheTrackWithoutAStop)
{
    // map 34; stage 2, then stage 4
    EXPECT_EQ(playback.events, (std::vector<std::string>{"play:Music\\crywolf_before-01.ogg",
                                                         "play:Music\\crywolf_back-03.ogg"}));
}

TEST(CrywolfMusicTest, TheEndStageStopsTheBattleTracksAndRequestsTheResult)
{
    // map 34 with crywolf_back-03.ogg playing; stage 5; the banner 0xb, and in a fresh runtime 0xa
    EXPECT_EQ(playback.events,
              (std::vector<std::string>{"play:Music\\crywolf_back-03.ogg", "stop", "sound:0x244"})); // 0x245 with 0xa
    // one more step
    EXPECT_EQ(playback.events.back(), "sound:0x244");
    EXPECT_EQ(playback.events.size(), 4u);
}

TEST(CrywolfMusicTest, TheResultNeedsTheMapAndTheEndStage)
{
    // (map 34, stage 4), (map 34, stage 6), (map 33, stage 5)
    EXPECT_FALSE(runtime.lastSoundId.has_value());
}

TEST(CrywolfMusicTest, OnAnotherMapItStopsOnlyTheMapsTrack)
{
    // map 33, every stage from 0 to 6. With crywolf1st.mp3 playing:
    EXPECT_EQ(playback.events, (std::vector<std::string>{"play:Music\\crywolf1st.mp3", "stop"}));
    // with crywolf_ready-02.ogg playing: no stop
    EXPECT_EQ(runtime.music.Current(), "Music\\crywolf_ready-02.ogg");
}

TEST(CrywolfMusicTest, WithoutAPlaybackDeviceTheResultIsStillRecorded)
{
    // runtime.audio == nullptr; map 34, stage 3, then stage 5 with the banner 0xb
    EXPECT_TRUE(runtime.music.Current().empty());
    EXPECT_EQ(runtime.lastSoundId, std::optional<std::int32_t>(0x244));
}
```

- [ ] **Step 2: Run them and see them fail** (`StepTheCrywolfMusic` does not exist).
- [ ] **Step 3: Add the stage names and write `StepTheCrywolfMusic`** from the block of `WinMainLoop.cpp` and the
  bodies of `Music::StepCrywolf` and `Music::StopCrywolfBase`, statement by statement.
- [ ] **Step 4: Call it** from `StepTheGameMusic` where the block was, after `StepWorld`. Delete the block and the
  five constants there, the two methods of `Audio::Music` and the two `MusicTest.Crywolf*` cases, whose assertions
  the first and third tests above carry.
- [ ] **Step 5: Add the two rows** to the module map and run `check_layers.py`. Expected: `0 new`.
- [ ] **Step 6: Run** `test_crywolf_music`, `test_win_main_loop`, `test_music` and `test_characterization`.
  Expected: the eight new tests pass, the four Crywolf cases of `test_win_main_loop` pass unchanged, and the four
  goldens are identical.
- [ ] **Step 7: Commit and push.** Expected in CI: green on the four configurations.

### Task 3: the stage and the occupation state, named once

Title: `refactor(crywolf): name the stages and the occupation state once`, branch `refactor/crywolf-stage-names`.
Skills: `refactoring` (Replace Magic Literal), `clean-code`.

**Files:**
- Modify: `Client/src/Protocol/Events.h`, `Client/src/Scenes/CrywolfContract.h`
- Modify, the sites of the table below: `Scenes/CrywolfContract.cpp`, `Scenes/CrywolfInterfaceDrawing.cpp`,
  `Scenes/EventPackets.cpp`, `Scenes/NpcTalk.cpp`, `Scenes/InventoryMoves.cpp`, `Scenes/CameraTour.cpp`,
  `Scenes/ModelEffects.cpp`, `Scenes/MonsterViewport.cpp`, `Scenes/CrywolfObjectServices.cpp`,
  `Scenes/WorldObjectsDrawing.cpp`, `Scenes/MapLoad.cpp`, `UI/ChatLogDraw.cpp`, `UI/MessageArea.cpp`
- Test: `Client/tests/protocol/test_events.cpp`, `Client/tests/scenes/test_crywolf_contract.cpp`, and a test for
  each site Step 2 finds without one; no existing test changes

**Interfaces:**
- Consumes: the seven stage names of Task 2.
- Produces, in `Protocol/Events.h`:

```cpp
// The values of CrywolfState::state (0x723c04), named after the later source's CRYWOLF_OCCUPATION_STATE_TYPE.
inline constexpr std::uint8_t CrywolfPeaceState = 0;
inline constexpr std::uint8_t CrywolfOccupiedState = 1;
inline constexpr std::uint8_t CrywolfWarState = 2;

// 0x435a00: the stage is the ready one or the started one.
[[nodiscard]] constexpr bool CrywolfIsReadyOrStarted(std::uint8_t stage);
```

- Produces, in `Scenes/CrywolfContract.h`, the event's public header until AR4.5 gives it `Crywolf.h`:

```cpp
// 0x435a00 && 0x4303c0: the stage is the ready one or the started one, in Crywolf's map.
[[nodiscard]] bool InTheCrywolfDefense(const WorldRuntime& runtime);
```

  It replaces the two private copies, `InTheDefense` (`CrywolfContract.cpp:67-71`) and `CrywolfIsActive`
  (`NpcTalk.cpp:68-73`).

| today | where | after |
| --- | --- | --- |
| `DefenseStage`, `CrywolfDefenseStage` (3) | `CrywolfContract.cpp:29`, `CrywolfInterfaceDrawing.cpp:44`, `EventPackets.cpp:255` | deleted; the uses take `CrywolfReadyStage` |
| `WarningStage`, `CrywolfWarningStage` (4) | `CrywolfContract.cpp:30`, `CrywolfInterfaceDrawing.cpp:45`, `EventPackets.cpp:238` | deleted; `CrywolfStartStage` |
| `CrywolfFightStage` (5) | `EventPackets.cpp:239` | deleted; `CrywolfEndStage` |
| `CrywolfEndStage` (6) | `EventPackets.cpp:240` | deleted; `CrywolfEndCycleStage` |
| `FirstActiveCrywolfStage`, `SecondActiveCrywolfStage` | `NpcTalk.cpp:64-65` | deleted with `CrywolfIsActive` |
| `FirstCrywolfLockStage`, `LastCrywolfLockStage` | `InventoryMoves.cpp:194-195` | deleted; the lock calls `CrywolfIsReadyOrStarted`, as the equipment test `0x5bec50` calls `0x435a00` alone at `0x5bf0f6` |
| `TourStage` (2), `UndoStage` (3), both `std::int32_t` | `CameraTour.cpp:42-43` | stay, as the tour's roles for its own copy of the stage, initialized with `CrywolfSecondNoticeStage` and `CrywolfReadyStage` |
| `Siege` (4, local) | `ModelEffects.cpp:1192` | stays, initialized with `CrywolfStartStage` |
| the literals 3 and 4 | `UI/ChatLogDraw.cpp:365`, `UI/MessageArea.cpp:253`, `MonsterViewport.cpp:441` | the comparison stays, with the names |
| `CrywolfPeaceState` (0), `CrywolfLostState` (2) | `EventPackets.cpp:241-242` | deleted; `CrywolfPeaceState`, `CrywolfWarState` |
| `LastLitCrywolfState` (2) | `EventPackets.cpp:246` | stays, as the bound of `>`, initialized with `CrywolfWarState` |
| `SmokeState` (1), the literals 0 and 2 | `CrywolfObjectServices.cpp:32,178,246`, `WorldObjectsDrawing.cpp:703` | `CrywolfOccupiedState`, `CrywolfPeaceState`, `CrywolfWarState` |
| `> 2`, `!= 0`, `== 1` | `MapLoad.cpp:283,325,328` | the same operators, with the names |
| `== 0`, and the two zeros of the reset | `EventPackets.cpp:1260,1625-1626` | `CrywolfPeaceState`; the reset writes `CrywolfPeaceState` and `CrywolfNoStage` |

- [ ] **Step 1: Write the failing tests.**

```cpp
TEST(EventsProtocolTest, OnlyTheReadyAndTheStartedStagesAreTheDefense)
{
    for (int stage = 0; stage <= 0xff; ++stage)
        EXPECT_EQ(CrywolfIsReadyOrStarted(static_cast<std::uint8_t>(stage)), stage == 3 || stage == 4) << stage;
}

TEST_F(CrywolfContractTest, TheDefenseNeedsTheStageAndTheMap)
{
    // (map 34, stage 3) and (map 34, stage 4)
    EXPECT_TRUE(InTheCrywolfDefense(runtime));
    // (map 34, stage 2), (map 34, stage 5), (map 33, stage 3)
    EXPECT_FALSE(InTheCrywolfDefense(runtime));
}
```

  And `static_assert`s in `test_events.cpp` that the ten names have the values 0 to 6 and 0 to 2.
- [ ] **Step 2: Pin the sites no test reaches.** For each row of the table, check that a test sets the stage or the
  state on both sides of the comparison. Where none does, add it and see it pass before the sweep. Known today: the
  message area's rows (`0x89` lower with the stage in 3 or 4, in place with 2 and 5). Check the mount lock of
  `InventoryMoves.cpp:813` the same way.
- [ ] **Step 3: Run the tests of Step 1 and see them fail.**
- [ ] **Step 4: Add the names and the two predicates.**
- [ ] **Step 5: Replace the definitions of the table, by value.** A site becomes a call to a predicate only if the
  original calls `0x435a00` there. The research repo's ASM has nine calls in eight functions: eight are followed by
  `0x4303c0` (the pair: `CrywolfContract.cpp`, `CrywolfInterfaceDrawing.cpp`, `EventPackets.cpp`, `NpcTalk.cpp`) and
  the ninth is the equipment test's (`0x5bf0f6`, `InventoryMoves.cpp`), which calls it alone. Every other site keeps
  its comparison and its operator: the chat log (`0x45cbb0`) and the message area (`0x5554e0`) read `0x723c05` and
  compare it themselves.
- [ ] **Step 6: Check.** `grep -rnE "constexpr std::uint8_t [A-Za-z]*(Stage|State) = [0-6];" Client/src` shows no
  definition of a Crywolf stage or state with a literal outside `Protocol/Events.h`. Run the tests of the touched
  files (`test_event_packets`, `test_crywolf_contract`, `test_crywolf_interface`, `test_npc_talk`,
  `test_camera_tour`, `test_crywolf_object_services`, `test_map_load`, `test_chat_log_draw`, `test_monster_viewport`)
  and `test_characterization`. Expected: all pass with no existing test edited, and the goldens are identical.
- [ ] **Step 7: Commit and push.** Expected in CI: green on the four configurations.

### Task 4: map 34, named once

Title: `refactor(crywolf): take map 34's name from the map ids`, branch `refactor/crywolf-map-id`. After AR1 Task 5
creates `Maps/MapId.h`.

**Files:**
- Modify: `Client/src/Maps/MapId.h`, `Client/tests/world/test_map_id.cpp`
- Modify: the 19 files that define map 34 under a name (the inventory's table "The map, 34"), and
  `Client/src/Scenes/CrywolfMusic.cpp`

**Interfaces:**
- Produces, in `Client::Maps`: `inline constexpr std::int32_t CrywolfMap = 34;` and
  `[[nodiscard]] constexpr bool IsCrywolf(std::int32_t map);` (`0x4303c0`).

- [ ] **Step 1: Write the failing test:**
  `EXPECT_FALSE(IsCrywolf(33)); EXPECT_TRUE(IsCrywolf(34)); EXPECT_FALSE(IsCrywolf(35));`
- [ ] **Step 2: Run it and see it fail.**
- [ ] **Step 3: Add the constant and the predicate.**
- [ ] **Step 4: Replace the named definitions.** An identity name (`CrywolfMap`, `Map34`, `Crywolf`,
  `SpecialMap::Crywolf`) is deleted and its uses take `Maps::CrywolfMap`. A role name (`Particle67ColorMap`) and the
  three `std::uint8_t` definitions stay, initialized with it. A comparison becomes `Maps::IsCrywolf` only where its
  comment cites `0x4303c0`. The 8 bare literals and the 16 table rows wait for each module's AR4 design PR, as AR1
  decided for every map.
- [ ] **Step 5: Check** that `grep -rnE "(CrywolfMap|Map34) = (34|0x22)" Client/src` prints only `Maps/MapId.h`; run
  `test_map_id` and `test_characterization`. Expected: pass, goldens identical.
- [ ] **Step 6: Commit and push.** Expected in CI: green on the four configurations.

### Task 5: four effect files leave the event in the module map

Title: `refactor(crywolf): map the death spirit and the missile tail to the effects`. No code changes.

**Files:**
- Modify: `Client/docs/research/coverage/module-map.tsv`, `Client/tools/architecture/layers-baseline.tsv`

`Scenes/CrywolfDeathSpiritEffects.{h,cpp}` and `Scenes/CrywolfMissileDrawing.{h,cpp}` are cases of the generic
effect creation, step and drawing (`0x51c952`, `0x52709b`, `0x6025fc`), called by the effect step
(`Scenes/GameSceneLogic.cpp:335`), the effect drawing (`Scenes/GameDrawTail.cpp`) and a monster's visual
(`Scenes/MonsterHelpers.cpp:237`). Their siblings are already mapped to `game/Effects`.

- [ ] **Step 1:** set the `target` of the four rows to `game/Effects` and their `why` to "a case of the effect
  switch (addr.), named after its first user".
- [ ] **Step 2:** run `check_layers.py`. Expected: five baseline rows no longer happen and one is new, the same
  include under its new owner (`Scenes/CrywolfDeathSpiritEffects.cpp -> Scenes/ActorState.h`, now
  `game/Effects -> game/Actors`, a pair the baseline already has 27 times).
- [ ] **Step 3:** rerun with `--write-baseline`. Expected: `wrote 1378 violations` (1,382 today). The PR description
  says why the one row changed owner.
- [ ] **Step 4:** run `python3 -I -m unittest discover -s Client/tools/architecture -p 'test_check_layers.py'`,
  commit and push.

### Task 6: the declared graph's cycles, reported

Title: `feat(AR0.2): report the cycles of the declared may_use graph`.

**Files:**
- Modify: `Client/tools/architecture/check_layers.py`, `Client/tools/architecture/test_check_layers.py`

**Interfaces:**
- Produces: `def declared_cycles(modules: dict[str, Module]) -> list[list[str]]`. Its edges are what
  `Module.may_include` allows, so a name and a `*layer<N` bound count alike: a module that names a higher one closes
  a cycle with that module's bound, and a bound above the module's own layer permits its own layer. It returns the
  strongly connected components of more than one module, each sorted, as `cycles` does for the include graph. `main`
  prints `declared cycle of N modules: …` for each, after the include cycles, and its exit code does not change.

- [ ] **Step 1: Write the failing tests:** `test_declared_cycle_between_two_modules` (each names the other: one
  cycle of two), `test_declared_ring_of_three_is_one_cycle`, `test_one_way_permission_is_no_declared_cycle`,
  `test_layer_bounds_of_the_own_layer_declare_no_cycle`,
  `test_name_of_a_higher_module_closes_a_cycle_with_its_layer_bound`,
  `test_layer_bound_above_the_own_layer_declares_a_cycle`, and `test_declared_cycle_does_not_fail_the_check` (exit
  code 0, the line printed).
- [ ] **Step 2: Run them and see them fail.**
- [ ] **Step 3: Implement**, sharing the component code with `cycles`.
- [ ] **Step 4: Run** the tests and the check on the repo. Expected today:
  `declared cycle of 19 modules: game/CastleSiege, game/ChaosMix, game/Chat, game/Events, game/FrontEnd, game/Guild,
  game/Hero, game/Hud, game/Items, game/Joints, game/Monsters, game/Movement, game/Npc, game/Pets, game/Quests,
  game/Social, game/Tooltips, game/Weather, game/WorldObjects`, and the same last line as before.
- [ ] **Step 5: Commit and push.**

### Task 7: the direction between the hosts and the events and maps (ADR, before AR4.1)

A decision, with the `engineering:architecture` skill: an ADR in `docs/adr/` with the options and the cost of each,
and Emanuel chooses. Its questions, with what this plan recommends:

1. **Who may use whom.** Recommended: the hosts use the event and the map script, never the reverse; the types a
   script takes move below both. The other option hands each chain's calls down from `game/Scenes`, which keeps the
   hosts ignorant of the scripts and costs one function per chain, assembled above.
2. **Where the state headers live.** The spec puts `<Module>State.h` in the module (layer 9) and the `GameContext`
   that owns every state in `Kernel` (layer 7), which would include every module: 80 upward includes of
   `game/Kernel` are in the baseline today. Either the aggregate lives above the modules, or the state headers form a
   low module of plain data.
3. **The monsters of a map:** with the map script, or in `game/Monsters` in files named for the map (recommended).
4. **One row per event in `modules.tsv`** (`game/Events/Crywolf` as a module of the check, so a violation names the
   event and Crywolf can leave the baseline by itself), or folders of one module as today.
5. **Whether "in no declared cycle" joins the done criterion** of a milestone, once Task 6 measures it.

---

## Self-review

- **Coverage of the findings:** the piece that was in `main` and the engine's knowledge of a stage (Task 2); the
  twelve names of the stage and the eight copies of the predicate (Task 3); the five names of the map (Task 4); the
  module map's wrong rows (Task 5); the cycle the layer check does not report (Task 6); the direction, the state's
  owner and the monsters (Task 7); the handlers, the state, the pathfinder's altar goal, the object hooks, the HUD
  children and the tour, each with its milestone in "Where it fits in the AR plan".
- **What stays out:** the two port differences of "Found on the way"; the rest of the per-map music in the engine
  (`Music::StepWorld`, `StopWorld` and the track names), which is the fourth site of the named-values plan's Task 3;
  the names of the other maps (143 definitions in 58 files), which AR1 Task 5 leaves to each module's AR4 design PR;
  the other events, which get the rule and no task here.
- **Names:** `StepTheCrywolfMusic`, the seven `Crywolf…Stage` and three `Crywolf…State` constants,
  `CrywolfIsReadyOrStarted`, `InTheCrywolfDefense`, `Maps::CrywolfMap`, `Maps::IsCrywolf` and `declared_cycles` are
  used the same way in every task.
- **Numbers that are estimates:** the sizes of the two modules (about 2,480 and about 650 lines) come from the
  inventory's proposed home of each piece. Everything under "What `main.exe` does" and "What MuRet does today" was
  counted by the reports' scripts; the baseline numbers of Tasks 5 and 6 were run on `refactor/client` at 38eb5ed.
