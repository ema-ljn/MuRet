# 0029. The game's module boundaries

**Status** · Accepted on 2026-11-27: Emanuel chose to start AR4 with the seven recommendations of the
[AR4 plan](../superpowers/plans/2026-11-27-ar4-scenes-into-modules.md#decisions) written in, and any of them can still
change in this ADR's review. It is the [Crywolf plan](../superpowers/plans/2026-11-24-crywolf-homes.md)'s Task 7,
widened to what AR4 must decide before its first move. Task 1 of the AR4 plan applies what needs no code
(["What Task 1 applied"](#what-task-1-applied)).

## Context

AR4 takes the game out of `Scenes/`, `World/`, `UI/`, `GameLogic/` and `App/` into its modules, in five milestones
(AR4.1 to AR4.5). A milestone is done when its modules have no row in `layers-baseline.tsv`. Seven questions change
how every milestone cuts, so they are answered once, before AR4.1 moves a file.

Measured at `main` `57ad3cd7`, before this ADR's changes:

- **The baseline has 1,180 rows**, by the milestone that removes them: AR4.1 232, AR4.2 189, AR4.3 210, AR4.4 231,
  AR4.5 287, other 31. `check_layers.py` prints an include cycle of 42 modules and a declared cycle of 19: the cycle
  that `may_use` allows, whether an include uses it yet or not.
- **The aggregate.** `Scenes/WorldRuntime.h` and `Scenes/WorldState.h` map to `game/Kernel` (layer 7) and include the
  state of every module: 46 of `Kernel`'s 71 baseline rows come from those two headers. Files of layers 8 and 9
  include them 346 times.
- **The hosts and the events.** The six modules that call per-map and per-event code, the hosts (`WorldObjects`,
  `Monsters`, `Hud`, `Movement`, `Npc`, `Weather`), and that code (`Events`, `CastleSiege`, `MapScripts`) include
  each other:
  - 78 rows go into the events and map scripts, from 36 files: `Hud` 22, `WorldObjects` 19, `Actors` 11, `Effects`
    8, `Monsters` 4, `Kernel` 4, `Movement` 3, `Npc` 2, `Guild` 2, `Chat`, `Assets` and `Terrain` 1 each. 50 of them
    come from the six hosts.
  - 87 rows go from the events and map scripts into the six hosts, from 45 files and to 16 headers: `MapScripts` to
    `WorldObjects` 31, `CastleSiege` to `Hud` 27, `Events` to `Hud` 13, `Events` to `WorldObjects` 5, `CastleSiege`
    to `WorldObjects` 4, and 7 more. Most name the types a script takes: `World/WorldObjects.h` 19,
    `Scenes/GameHud.h` 12, `Scenes/GamePanels.h` 10, `Scenes/WorldObjectsDrawing.h` 8.
- **What the original does** (Crywolf plan, "How generic code reaches them"): the generic function calls the map's or
  the event's code in place, under a guard on the map, in chains whose order is not the same in every chain (Aida
  before Crywolf in the four object chains, after it in four monster chains). No single registration order reproduces
  the ten chains, and generic work sits between their links.
- **The modules of today's check.** `game/Events` and `game/MapScripts` are one row each in `Client/modules.tsv`; their
  folders (`game/Events/Crywolf`, `game/MapScripts/Kalima`) are part of them, so a violation names only the module.
- **The libraries.** [ADR 0028](0028-shared-library-modules.md) gives the game "few libraries, starting with one",
  `Game`, in AR4. A shared library must link by itself, so the whole game has to be in `game/` first.
- **What no milestone moves.** `game/Scenes` (38 files, 11,394 lines) and the app (36 files, 4,173 lines) have targets
  in the map, but the master plan gives their move to no milestone; AR5 redesigns them.

## The questions

### 1. Who may use whom

| | A. The hosts use the scripts | B. The calls are handed down from `game/Scenes` |
| --- | --- | --- |
| The rule | a host may use the event and the map script, never the reverse; the types a script takes (`WorldObject`, the object pass, the HUD's context) move below both | the hosts never know the scripts; `game/Scenes` builds one function per chain and hands it to the host |
| The rows it cuts | the 87 from the scripts into the hosts, by moving what both share down (27 of them, `CastleSiege` to the HUD's panels, go with the panel design of AR4.5) | the 78 into the scripts, 50 of them from the hosts, by one injected function per chain |
| The order of a chain | stays written in the host's code, as the original reads | written once more where `Scenes` assembles the chain |
| What it adds | a lower home for each shared type | one slot per chain and its assembly above, a bag of functions the module anatomy avoids |

**Decided: A.** It keeps each chain where the original has it, in its order, and the rows it cuts are the ones a type
moving down removes.

### 2. How the state leaves `WorldRuntime`

| | A. Module by module | B. Retarget the aggregate now |
| --- | --- | --- |
| How | each module's design moves its state to `Session::ModuleStates` (layer 10, owned by `App::GameClient`), and its files stop including `WorldRuntime.h` and `WorldState.h` | `WorldRuntime.h` and `WorldState.h` go to `game/Session` before AR4.1 |
| `Kernel` in AR4.1 | keeps the 46 rows of the two headers until the modules they point at leave; Task 38 deletes `WorldRuntime.h` | loses those 46 rows at once |
| The baseline | shrinks with each design | gains the 346 includes from layers 8 and 9 as upward rows, in one rewrite |

**Decided: A.** It is what Emanuel chose for AR3 on 2026-11-26 ("En AR4, módulo por módulo"), and B trades 46 rows
for 346.

### 3. The monsters of a map

| | A. In `game/Monsters`, in files named for the map | B. With the map script |
| --- | --- | --- |
| The chains | stay in one module: two hooks are keyed by the model, not the map, and each sits in a chain with every other map's monster hook | each map's hooks leave the chain they sit in |
| Crywolf alone | 7 hooks, 18,312 bytes, about 1,200 lines in MuRet; map 35's helpers call four of them | the same lines, split across two modules that call each other |

**Decided: A,** for every map: Crywolf, Kalima, the siege and maps 31, 33 and 35 ask the same question.

### 4. One row per event and per map script

| | A. One `modules.tsv` row each | B. Folders of one module, as today |
| --- | --- | --- |
| A violation names | its event or its map | `game/Events` or `game/MapScripts` |
| Leaving the baseline | each event and each map by itself | only when every event or every map does |
| What it costs | 11 rows; the baseline's rows relabeled, and the includes between two rows that were one module become rows | nothing now |

**Decided: A**, for the events and maps that are their own folder: `game/Events/Crywolf` and the ten map scripts.
Blood Castle, Devil Square and Chaos Castle stay together in the flat `game/Events`, around the object and the windows
they share (Crywolf plan). Kalima's ten files keep their target until AR4.3 retargets them to `game/MapScripts/Kalima`.

### 5. "In no declared cycle" in the done criterion

| | A. It joins the criterion | B. Only the cycles the includes make |
| --- | --- | --- |
| A milestone is done when | its modules are in no cycle, made or declared | its modules are in no cycle the includes make |
| The `may_use` lists | shrink with the designs, so a cycle cannot come back through an allowed include | may keep allowing a cycle nobody uses yet |
| Today | none of AR4.1's modules is in the declared cycle; four of AR4.2's are (`Hero`, `Monsters`, `Movement`, `Pets`) | |

**Decided: A.** `check_layers.py` prints both kinds since the Crywolf plan's Task 6.

### 6. The game's library

| | A. One `Game`, after AR4.5 | B. A few |
| --- | --- | --- |
| When | Task 41, once the whole game is in `game/` | each as its part is ready |
| What it costs | exports at one boundary, `GAME_API` placed by AR1 Task 8's script | exports at each boundary, and every cut between them free of cycles first |

**Decided: A**, ADR 0028's "starting with one". A boundary the include check fails to hold is what would give a part
of the game a library of its own.

### 7. When `game/Scenes` and the app move

| | A. With AR4.5's move | B. In AR5 |
| --- | --- | --- |
| The old folders | empty when AR4 ends | `Scenes/` and `App/` keep 74 files until AR5 |
| `Game` (Task 41) | cut by folder: `Client/src/game/**` | cut by a list of files |
| AR5 | redesigns them at their final paths | moves and redesigns them |

**Decided: A.** The move is a script, and AR4.5's already carries the app's six headers that other modules include.

## Decision

1. The hosts use the event and the map script, never the reverse; the types a script takes move below both.
2. The state leaves `WorldRuntime` module by module, into `Session::ModuleStates`; Task 38 deletes `WorldRuntime.h`.
   Amended on 2026-11-28 ([below](#amendment-of-decision-2)): in AR4 Session holds the states and `Kernel` keeps a
   reference to each.
3. A map's monsters stay in `game/Monsters`, in files named for the map.
4. One `modules.tsv` row per event and per map script that is its own folder.
5. A milestone is done only when its modules are in no cycle, made or declared.
6. One `Game` library, after AR4.5.
7. `game/Scenes` and the app move with AR4.5.

## What Task 1 applied

- **`Client/modules.tsv`:** `game/Events/Crywolf` and `game/MapScripts/<Map>` for Aida, Atlans, CastleSiege, Crywolf,
  Dungeon, Icarus, Kalima, LandOfTrials, Noria and Tarkan, at layer 9. `game/MapScripts` has no file of its own, so
  its row gives way to the ten. Each new row's `may_use` names only what its files include today without a violation,
  so it allows no include that was not allowed before. `Hero`, `Skills` and `Npc`, which may use `game/Events`, may
  use `game/Events/Crywolf` too: their includes of `Scenes/CrywolfContract.h` stay allowed until AR4.2 removes the
  first two (Crywolf plan, the altar goal).
- **`layers-baseline.tsv`:** 67 rows relabeled with their event or map (still 1,180 of them), and one new row,
  `Scenes/EventPackets.cpp` to `Scenes/CrywolfContract.h` (`game/Events` to `game/Events/Crywolf`): the `BD 03`
  handler, which was inside one module and is now in the wrong one. Task 36 moves it to `game/Events/Crywolf/In/`.
- **The cycles:** the declared cycle has 20 modules, `game/Events/Crywolf` added through `Hero`, `Skills` and `Npc`.
  The include cycle counts 52, because the eleven new rows are counted apart.
- **The done criterion** with "made or declared" is already the plan's.
- **The snapshot:** the client reference regenerated at `57ad3cd7`, its `metricas.tsv` included, in the PR stacked on
  this one, to compare the port numbers when AR4 ends.
- **The spec's 58 modules stand.** The new rows are folders of `Events` and `MapScripts` that the check sees apart.

## Amendment of Decision 2

On 2026-11-28 Emanuel chose, for the AR4.4 design, that Session holds the modules' states ("Session guarda los
estados"). Moving each state out with its module's design meant passing it to about 4,500 call sites that read it
through `runtime.<field>` today, and moving the 57 state headers to `Kernel` instead meant about 20,800 lines in the
wrong layer.

- `Kernel/HeldModules.h` declares `HudModules`, `RuntimeModules` and `MapModules`, the bases of `HudState`,
  `WorldRuntime` and `WorldMap`. Each owns a box that only `game/Session` defines (`Session/HeldModules.cpp`) and has
  one reference member per state, bound to its own box, so `runtime.social` reads as before. A copy copies the box and
  a move moves its contents; a reference never names another holder's state.
- `WorldState` keeps the world's module states (the cloths, the sets, the particles, the action speeds, the fauna and
  the leaves) the same way, behind the accessors it already had, defined in `Session/HeldModules.cpp`.
- `Kernel/WorldRuntime.h`, `Kernel/WorldState.h` and `Kernel/WorldMap.h` include no module header, so `Kernel`'s
  upward rows leave the baseline. A file that uses a state includes that state's header.
- Passing each module its state as a parameter, so its files stop including `WorldRuntime.h`, is AR6's. Task 38 still
  moves the plain fields of `WorldRuntime` to their owners, and `WorldRuntime.h` stays with only the references until
  AR6.

## Consequences

- Every AR4 design PR applies the same direction: a script that needs a host's type gets it from below both, and a
  host's call into a script stays in place under its guard. A port appears only where the caller is below the event
  and is not misplaced.
- `WorldRuntime.h` and `WorldState.h` stay in `game/Kernel` until Task 38, so `Kernel` keeps rows through AR4.1 that
  its own design does not remove. The milestone's done criterion counts them with the modules they point at.
- A violation in an event or a map names it, and Crywolf or a map can leave the baseline alone.
- The `may_use` lists become part of what a design cuts, not only the includes.
- The game ships as `Game.dll` and `libGame.so` from Task 41; until then `ClientLib` keeps the game and the app.
- When AR4 ends, `Client/src` holds `engine/`, `game/` and `app/` and nothing else.
