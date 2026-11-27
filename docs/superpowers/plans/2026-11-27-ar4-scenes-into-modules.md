# AR4, `Scenes/` into vertical modules: detailed plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** every game file lives in its module's folder under `Client/src/game/`, every module owns its state, reads
its packets in its own handlers and sends through `Kernel` ports, and no module includes one above it or one that
includes it back. No byte of behavior changes.

**Architecture:** five milestones, bottom-up in the module graph. Each one is a scripted move, a namespace rename, a
comments PR and a few design PRs, one per knot. A design PR moves the module's state out of `WorldRuntime`, turns its
appliers into `In/` handlers and its send functions into `Out/` actions, gives its shared values a home and removes
its rows from the layer baseline. AR4.5 also splits the session and deletes the event variant AR2 left. A last task
builds the `Game` library.

**Tech Stack:** C++20, CMake, GoogleTest, Python 3 (standard library, `unittest`), libclang 18 for the renames, GitHub
Actions.

**Spec:** [`docs/superpowers/specs/2026-11-20-client-architecture-design.md`](../specs/2026-11-20-client-architecture-design.md)
("The modules, one by one", "Anatomy of a game module", "State"), the epic in the master plan
([`2026-11-20-client-architecture-refactor.md`](2026-11-20-client-architecture-refactor.md), "Epic AR4"),
[ADR 0028](../../adr/0028-shared-library-modules.md) (the `Game` library), Task 8 of the
[named-values plan](2026-11-23-named-values.md), Tasks 11 and 12 of the
[item vocabulary plan](2026-11-23-item-vocabulary.md), and "Where it fits in the AR plan" and Task 7 of the
[Crywolf plan](2026-11-24-crywolf-homes.md). It takes what the [AR2 plan](2026-11-26-ar2-protocol-and-dispatch.md) and
the [AR3 plan](2026-11-26-ar3-state-per-module.md) left to AR4 ("What AR2 and AR3 leave here").

Measured at 4cbf432b (2026-11-27): `main` at a604377e plus the AR2 stack, #521 to #529.

## Global Constraints

- No PR changes behavior. The behavior golden (`tests/characterization`) and every current test and golden must stay
  identical on Linux and Windows, in Debug and `math-release`. A difference is a refactor bug, not a golden to
  regenerate (`superpowers:systematic-debugging`).
- These do not change: the order of the original's calls and of `rand()` calls, the bytes the client writes, the
  moment each reset clears its fields, and what a field holds before its first write. Every fan-out between modules is
  an ordered list.
- Titles and branches, with the milestone as the scope:
  - Move: `refactor(AR4.m): move <modules> to Client/src/game`, branch `refactor/ar4.m-move`.
  - Namespaces: `refactor(AR4.m): the namespaces follow the folders`, branch `refactor/ar4.m-namespaces`.
  - Comments: `docs(AR4.m): trim and translate comments`, branch `docs/ar4.m-comments`.
  - Design: `refactor(AR4.m): <what>`, branch `refactor/ar4.m-<topic>`.
  - Never `CLn` in a scope.
- CI runs `check_pure_move.py` on every `type(ARn)` or `type(ARn.m)` title and `check_comments_only.py` on every
  `docs(ARn.m)` one. Only the move PRs rename files: before pushing any other PR, `git diff -M50% --name-status` shows
  no `R` line.
- Everything in English. A comment only when strictly necessary, on one line; the `// 0x…` citations travel with the
  code they cite. A cut keeps `not ported` or `MuRet does not …` on the line that cites its address.
- New code calls `RoundedMath.h` / `RoundedTransforms.h`, never `std::sin` or glm trig (`check_rounded_math.py`). No
  new allocation per frame (ADR 0026).
- New scripts: standard library only, `python3 -I`, tests with `unittest`. One-off measuring scripts stay out of the
  repo.
- A module being moved or designed receives no port work that day: before a milestone's move, no open PR touches its
  files.
- PRs target `main` and may be stacked. Nothing merges without Emanuel's yes (2026-11-26).
- Priority: fidelity, then `docs/CODING_RULES.md`, then `clean-code`, `clean-architecture` and `refactoring`.
- The cloud container builds the client only with the stub recipe and no SDL; GitHub CI is the build and the final
  word.

## Review Focus

- **The order behind a port.** When a direct call becomes a `Kernel` port or an ordered extension point (the
  character step by actor class, the click's owners, the viewport's side effects), the calls must still run in the
  original's order, including the `rand()` calls inside them. Each extension point gets a test that records the order
  of its slots and compares it with the research's order, written before the call changes.
- **A reset that clears several modules.** The return to the selection (`0x4c2480`), the world's exit and Crywolf's
  `0x42fdc0` clear fields that now leave for different modules. AR3.2 pinned what the return to the selection does to
  five module states. Every task that moves a state keeps those tests and adds one that sets each moved field, runs
  the reset and checks it.
- **A reused buffer.** When the character drawing and the skinning reuse their buffers instead of allocating, a frame
  that draws a smaller model after a bigger one must not read the bigger one's tail. Task 9 tests a big model, then a
  small one, against a fresh buffer.
- **A rename that rebinds a call.** A namespace rename can make an unqualified call bind a same-named function of
  another namespace (AR1 got an infinite recursion this way). Each namespaces task rereads the tree with libclang and
  checks that every reference binds the same declaration as before.
- **The session's flows after the split.** The login, the selection, the return to the server list and the map
  server change keep their keepalive timing and their order of packets. The flow tests of `test_server_session.cpp`
  (the traces `entry` and `inventory`, the world states) keep their bytes and their assertions when they move to
  per-handler files in Task 40.

---

## How this plan places files

- **A file changes folder only in a move PR**, made by `move.py` from its row in
  `Client/docs/research/coverage/module-map.tsv`.
- **The map is fixed before the move.** Step 0 of every move task reads the milestone's rows and changes the `target`
  of each file whose code belongs to another module, so the move puts it in its final folder. A target found wrong
  after its move is carried by the next move of its new module (`move.py` moves every file whose path differs from
  its target), or by a small move PR if that module has already moved.
- **A new file is born in the folder it will keep**, takes that folder's namespace (`Client::<path>`, CODING_RULES
  "Namespaces") and gets its map row in the same PR (`confidence` `high`, `why` ending in `(AR4.m)`).
- **Tests do not move.** A test belongs to the module of its first project include; the comments PR of that module's
  milestone takes it.

## Deviations from the master plan, and why

| master plan | this plan | why |
| --- | --- | --- |
| one design PR per milestone | a few stacked design PRs per milestone, one per knot | as AR2.3 and AR3 did; each knot can be reviewed and rejected alone, and Emanuel asked for stacks (2026-11-26) |
| the namespace changes in the design PR (spec) | its own PR after the move | it is mechanical and checked by a rebind test (AR1 Task 9, AR2.1 Task 5), and the design PR stays readable |
| a regenerated `scenes-cycles.tsv` has no pairs of the milestone's modules | `check_layers.py` prints no cycle, made or declared, that contains one of them | the check prints both kinds since the Crywolf plan's Task 6; the annex TSV was a one-off |
| `WorldEvent`, `ISessionEventSink` and `SessionEventApplier` go in AR2.3 | in AR4.5, with `FrontEnd` (Task 40) | Emanuel, 2026-11-27: the session split and Task 18 move to AR4 |
| `In/` handlers and `Out/` actions in AR2.3 | in each module's design | Emanuel, 2026-11-27 ("Rutear a los aplicadores de hoy") |
| the module states leave `WorldRuntime` in AR3.2 | in each module's design | Emanuel, 2026-11-26 ("En AR4, módulo por módulo") |
| no milestone moves `game/Scenes` and `app` | AR4.5's move carries them | so no file stays in the old folders and `Game` can be cut by folder; AR5 redesigns them at their final paths (Decision 7) |
| AR4.1 moves `Sound` "(what is left)" | `Sound` moved in AR1.2; AR4.1 does only its design | measured at 4cbf432b |
| 17 fields of a single module | 10 at 4cbf432b (`field_users.py --single`) | AR3.2 Task 6 gathered the castle's fields into `castleSiege` |
| the `Game` library is ADR 0028's "in AR4" | a last task after AR4.5 (Task 41) | a shared library must link alone, so the whole game has to be in `game/` first |

## What AR2 and AR3 leave here

| piece | from | where |
| --- | --- | --- |
| `In/` handlers for every routed row (today's appliers) and `Out/` actions for the 32 `Send…` and `Request…` functions of `ServerSession.h` | AR2.3 Tasks 14 to 17, Steps 1 and 2 | each module's design |
| F3 03, F3 10 and the 15 flow members of `ServerSession` (login, selection, server list, map server, the hellos and the main check) | AR2.2 Task 12, second half | Task 39 |
| `WorldEvent` (14 alternatives), `CharacterEvent` (3), `ISessionEventSink`, `SessionEventApplier`, `/bigobj`, and the split of `test_server_session.cpp` (5,059 lines) | AR2.3 Task 18, Step 3 of Tasks 14 to 17 | Task 40; the 3 chat-room alternatives in Task 31 |
| `guild`, `social`, `npcWindows`, `quest` and `castleSiege` out of `WorldRuntime` | AR3.2 Task 6, Step 2 | Tasks 29 to 31 and 37 |
| the single-module fields: `reportedCellX`, `reportedCellY`, `reportedCellTime` (Actors), `terrainPolygonState` (Camera), `phoenixBones`, `namedLineTime`, `namedLineFirst`, `modelPatches` (Monsters), `byte6c8e4d` (Session), `trialsAmbientTime` (Scenes) | AR3.2 Task 7, Steps 2 and 3 | Tasks 6, 8, 14 and 39; `trialsAmbientTime` in AR5 |
| Crywolf in three, and the mouse's arbitration | AR3.2 Task 8 | Tasks 13, 22, 23 and 36 |

What other plans put in AR4:

| piece | plan | where |
| --- | --- | --- |
| the homes of each milestone's spaces | named-values Task 8 | the milestone's design tasks |
| one record per model slot (`LoadedModel`), the link bones | item vocabulary Task 11 | Task 6 |
| the item names reach every module | item vocabulary Task 12 | Tasks 6, 28 and 37 |
| the item glow's last task, before AR4.1 moves the character drawing | item glow Task 4 (waits for Emanuel's yes) | before Task 2 |
| the direction between the hosts and the events and maps (an ADR) | Crywolf Task 7 | Task 1 |
| the altar goal, the object hooks, the HUD children, the tour | Crywolf, "Where it fits in the AR plan" | Tasks 13, 22 and 36 |
| the loose map literals of each module (about 130 in 27 files); the record `Birds.cpp` writes by offset | AR1 Task 5 | each design; Task 24 |
| the two `BoxPixels` helpers of the HUD | reference screen plan | Task 35 |

## The milestone recipe

Every milestone runs these tasks in this order. The milestone's own section says only what differs.

**The move task's Step 0:**

- [ ] For each module of the milestone, `python3 -I Client/docs/research/coverage/referencia/consultar.py module
  <module>`, and the files of the open PRs: no open port work on its files.
- [ ] Read the milestone's map rows and retarget each file whose code belongs to another module (it holds another
  module's state, or all its callers are there). Rerun `check_layers.py --clean-root engine`: `0 new`.

**The move:** `python3 -I Client/tools/architecture/move.py --modules <the milestone's modules>`, then by hand only
what it printed. Then `check_pure_move.py --base origin/<the PR's base> --head HEAD` lists nothing, `check_layers.py`
prints `0 new`, and `git diff -M50% --diff-filter=M` touches only include lines and build paths. After its squash,
the next PR runs `move.py --record <squash>` (skill `muret-move`).

**The namespaces:** each moved file declares its folder's namespace (`Client::Actors`, `Client::MapScripts::Crywolf`),
with the libclang method of AR1 Task 9 and AR2.1 Task 5: read the base tree's references, plan the qualifier edits
from that data, apply them, reread, and check that every reference binds the same declaration. Then the GCC build and
ctest. By hand, then grep: the `PrintTo` helpers of `tests/support/*Printers.h`, the `_WIN32` branches and macro
bodies.

**The comments:** the moved files and their tests, by CODING_RULES §9: the citation plus a short why, in English, on
one line; a long finding moves to `Client/docs/research/`. `check_comments_only.py --base origin/<the PR's base> --head
HEAD` passes. After its squash, `move.py --record <squash>`.

**Each design task:**

- [ ] **Measure** the task's rows of `layers-baseline.tsv`, sorted by kind: upward, mutual (the other module includes
  this one back) and one-way to a sibling. Then its rows of `named-values-baseline.tsv`, and its fields with
  `field_users.py --header Scenes/WorldRuntime.h --struct WorldRuntime`, each field's owner taken from `globals.tsv`.
- [ ] **Pin** the tests the change could break that do not exist yet: the order of an extension point, a reset of a
  moved field. They pass against today's code.
- [ ] **Change**, one coherent step per commit, the module's tests green after each:
  - **The state.** The module's state and fields leave `WorldRuntime` for `Session::ModuleStates` (Decision 2). Its
    functions take the parts they touch in their signatures; `Route` hands them by type. Its files stop including
    `WorldRuntime.h` and `WorldState.h`, and reach the shared core through `Kernel::GameContext`.
  - **The handlers.** Each routed row of the module gets its handler in `<Module>/In/<Packet>.cpp`, a free function of
    up to ~80 lines that names its parts; the row points at it, and today's applier goes.
  - **The actions.** Each `Send…` or `Request…` of `ServerSession` that the module's intent starts becomes
    `<Module>/Out/<Intent>.cpp`: it validates against the module's state and writes through `Kernel::IPacketSender`.
  - **The rows.** Every row of the module leaves the baseline. A mutual pair moves what both share down (to `Kernel`
    or to a lower module's `<Module>Defines.h`) or inverts the call with a port. An upward include becomes a port, or
    the call moves up to its caller. A one-way use that makes sense joins `may_use` in `Client/modules.tsv`, with the
    why in the PR.
  - **The homes.** The milestone's spaces of named-values Task 8: the home, the values a second file needs, the
    baseline rows that go; the PR lists the literals it leaves and why.
- [ ] **Check** the module's tests, `tests/scenes`, `check_layers.py --clean-root engine` with `--write-baseline` (the
  baseline only shrinks), `check_named_values.py --clean-root engine`, `dispatcher_cases.py check`, clang-format 18 and
  the include cleaner, then push. CI runs the golden.

**A milestone is done when** its modules have no row in `layers-baseline.tsv`, the rows of the spaces it gave a home
have left `named-values-baseline.tsv`, no file of its modules includes `WorldRuntime.h` or `WorldState.h`,
`check_layers.py` prints no cycle (made or declared) that contains one of its modules, the behavior golden is
identical, and the reference gives the same port numbers.

## Before AR4.1

### Task 1: the game's module boundaries (ADR 0029)

PR `docs(AR4): the game's module boundaries`, branch `docs/ar4-module-boundaries`. Skill: `engineering:architecture`.
It is the Crywolf plan's Task 7, widened to what AR4 must decide before its first move.

- [ ] **Step 1: Write `docs/adr/0029-game-module-boundaries.md`** with the seven questions of "Decisions" below, each
  with its options, their cost measured at 4cbf432b, and Emanuel's answer. Its row goes in `docs/adr/README.md`.
- [ ] **Step 2: Apply what needs no code:** the rows of `Client/modules.tsv` for `game/Events/Crywolf` and each
  `game/MapScripts/<Map>` (Decision 4), and the done criterion above if Decision 5 holds.
- [ ] **Step 3: Snapshot `metricas.tsv`**, to compare when AR4 ends.
- [ ] **Step 4: Check** `check_layers.py --clean-root engine` (`0 new`) and its tests, then push.

## AR4.1: `Kernel`, `Assets`, `Terrain`, `Camera`, `Sound`, `Player`, `Actors`

189 files and 33,515 lines to move: 144 in `Scenes/`, 38 in `World/`, 3 in `Assets/`, 2 in `GameLogic/` and 2 in
`engine/Models`. 232 baseline rows: 71 of the state aggregate in `Kernel`, 41 upward, 115 mutual and 5 one-way; 150
of them are `Actors`'. 105 named-values rows (52 `map`, 39 `player-action`).

### Tasks 2 to 4: the move, the namespaces and the comments

Task 2 runs `move.py --modules game/Kernel,game/Assets,game/Terrain,game/Camera,game/Player,game/Actors` after the
recipe's Step 0. In Task 3, `game/Kernel`'s five files from AR3 are already in `Client::Kernel`. Task 4 as the
recipe.

### Task 5: the `Kernel` ports and the module states' owner

PR `refactor(AR4.1): the kernel's ports and the module states' owner`. Skills: `clean-architecture`, `cpp-pro`.

**Files:**
- Create in `Client/src/game/Kernel/`: `IPacketSender.h`, `ISoundRequests.h` and `IOnScreenMessages.h` (the spec's three
  ports, with CODING_RULES' `I` for interfaces), with their tests in `Client/tests/kernel/`.
- Create `Client/src/game/Session/ModuleStates.h`.
- Modify `game/Session/Route.h` (the parts) and `App/GameClient.{h,cpp}` (the owner).

**Interfaces (produces):**

```cpp
namespace Client::Kernel
{
class IPacketSender { public: virtual ~IPacketSender() = default; virtual void Send(Protocol::Outgoing packet) const = 0; };
// As WorldRuntime::RequestSound takes it today; Task 7 gives the id its type.
class ISoundRequests { public: virtual ~ISoundRequests() = default;
    virtual void Request(std::int32_t id, std::optional<glm::vec3> origin, bool loop) = 0; };
// 0x555190(name, text, type).
class IOnScreenMessages { public: virtual ~IOnScreenMessages() = default;
    virtual Core::Expected<void, std::string> Add(std::string_view name, std::string_view text, std::int32_t type) = 0; };
}
namespace Client::Session { struct ModuleStates; } // one member per module, by value, as each design moves it
```

`Session` implements `IPacketSender`, `Sound` implements `ISoundRequests` (Task 7) and `Hud` implements
`IOnScreenMessages`. `App::GameClient` owns the `ModuleStates` beside the `WorldState`, and the `WorldEntryResources`
the session routes with points at it, so `Route` hands a handler the member it names (AR3 Decision 1A).

- [ ] **Step 1: Write the failing tests.** A fake of each port records its calls. `Route` hands a handler that names
  `const Kernel::IPacketSender&` the session's sender. The test that `Route` hands a member of `ModuleStates` comes
  with its first member (Task 6).
- [ ] **Step 2: Implement the ports and the owner.** `Scenes::GamePacketSender` and `Scenes::ActionSender`, two
  `std::function` aliases today, give way to `Kernel::IPacketSender` one module at a time, as each design reaches them;
  `UI::MessageSink` and `MessageSinkOf` give way to `IOnScreenMessages` the same way.
- [ ] **Step 3: Check** as the recipe.

The aggregate's rows into this milestone (`WorldMap.h` and `LoadedWorld.h` stop including `Terrain` and `Assets`, 9
rows) go with Task 6, which gives the terrain its state.

### Task 6: `Assets`, `Terrain` and `Camera`

PR `refactor(AR4.1): one record per model slot and the terrain's state`. Skills: `clean-code`, `refactoring`.

- Item vocabulary Task 11 as that plan writes it: `LoadedModel` with `MeshTraits` computed at load, one table type for
  `GameModels`, `WorldModels` and `CreatePreview`, `MakeSkeleton` by value, and the four link bones beside
  `PlayerHeadBone`. Gate: `test_golden_game_models`, the golden, and the equality test of the two root-height
  functions over every model id.
- `ModelId` (named-values Task 8) as `Core::Id<ModelIdTag, std::uint16_t>` in `Assets/ModelIds.h`, with `0x20a` once
  as `PlayerSkeletonModel`; item vocabulary Task 12 in the moved files.
- `CellSize` in `Terrain`. `terrainPolygonState` goes to `Camera`'s state and `groundMarks` to `Terrain`'s.
- `Terrain`'s 4 upward rows and `Assets`' one.
- The aggregate's rows into this milestone: `WorldMap.h` and `LoadedWorld.h` stop including `Terrain` and `Assets` (9
  rows), and what a loaded world holds of the terrain and the models goes to their states. The other rows of the
  aggregate leave with the modules they point at.

### Task 7: `Sound`

PR `refactor(AR4.1): the sound requests through the kernel`.

- `Sound/SoundIds.h` and `Audio::SoundId` (named-values Task 8): the 13 shared sounds, `0x19` once, and the three WAVs
  the events loop as music (`0x8b`, `0x99`, `0x9e`) named once. `ISoundRequests::Request` takes the `Audio::SoundId`.
- `Sound` implements `Kernel::ISoundRequests`. The roughly 200 calls to `WorldRuntime::RequestSound` (76 files) stay
  in their functions and call the port, so the order of the requests and the `rand()` that picks some of them do not
  move.
- `modelSounds`, `lastSoundId` and `audio` leave `WorldRuntime` for `Sound`'s state.

### Task 8: `Player` and the viewport

PR `refactor(AR4.1): the viewport's side effects as an ordered list`.

- The viewport appliers stop calling monsters, chat, shops and pets (`ViewportPlayers.cpp` alone reaches 14 modules).
  What each original handler calls after it creates the actor becomes an ordered extension point with fixed slots,
  assembled in `app/` in the original's order (spec, "Ordered extension points").
- `Actors/In/` is born with the viewport handlers. From here on, the `muret-packet` skill's section 2 and `AGENTS.md`
  rule 4 say that the handler goes in `<Module>/In/`.
- `viewportPackets` and the three `reportedCell…` fields go to `Actors`' state.
- `PlayerBones` and `PlayerActions` beside the player catalog (named-values Task 8: 39 `player-action` rows).

### Task 9: the character step and the character drawing

Two PRs: `refactor(AR4.1): the character step by actor class`, then `refactor(AR4.1): the drawing reuses its
buffers`. Skills: `refactoring` (the fidelity cautions), `cpp-pro`.

- The character step (`0x508f00`): what it calls per actor class (monsters, pets, fauna, effects, joints) becomes a
  fixed table of steps per class, assembled in `app/` in the original's order. No run-time registration.
  `CharacterStep.{h,cpp}` have 25 rows today.
- The character drawing and the skinning reuse their buffers instead of the ~2,090 temporary `std::vector` per frame
  (ADR 0026). Test: a big model, then a small one, against a fresh buffer.
- The rest of `Actors`' mutual rows (`Effects` 20, `Monsters` 12, `Fauna` 11, `Chat` 9, `Joints` 9, `Pets` 7) by the
  recipe, and the Devias church test of the music plan if that plan has landed.

## AR4.2: `Monsters`, `Pets`, `Movement`, `Skills`, `Combat`, `Hero`

126 files and 32,662 lines to move (116 in `Scenes/`, 10 in `GameLogic/`). 189 baseline rows (161 mutual, 28 upward),
121 named-values rows. `Hero`, `Monsters`, `Movement` and `Pets` are in today's declared cycle of 19 modules.

### Tasks 10 to 12: the move, the namespaces and the comments

As the recipe.

### Task 13: the click and the pathfinder

PR `refactor(AR4.2): the click's owners as an ordered list`.

- The mouse's arbitration becomes its own state in `game/Kernel`: `movement.pendingClick`, `ui.mouseOnUi`
  (`0x775857c`) and the `GameUiState` fields `MoveHero` (`0x59b37a`, `0x59b853`) reads (AR3.2 Task 8; its Step 1
  lists them).
- `GameMovement`'s click stops including `Events` and `Chat`: the owners it asks before walking (the Crywolf camera
  tour, its contract, the events' ticket window, the chat) become an ordered extension point.
- `0x4eac00` ported once, with the Crywolf altar goal inside it (14 call sites); the includes from `Actors`, `Hero`
  and `Skills` go. `0x57ce80` and `0x4ce000` ported once.
- The raven of `WorldTransitionSupport` goes to `Pets`.

### Task 14: `Monsters` and `Pets`

PR `refactor(AR4.2): the monsters' and the pets' state`.

- `phoenixBones`, `namedLineTime`, `namedLineFirst` and `modelPatches` go to `Monsters`' state, and `pets`, `petTab`
  and `petTooltipQuery` to `Pets`'.
- Each map's monsters stay in `game/Monsters`, in files named for the map (Decision 3).

### Task 15: `Skills`, `Combat` and `Hero`

PR `refactor(AR4.2): combat and the hero in one direction`.

- `Combat` and `Hero` stop including each other. Whether `Hero` (789 lines) merges with `Player` is answered in this
  PR with the measured includes, for Emanuel's review.
- `Tables/SkillRecord.h` and `Tables/SkillId.h` (named-values Task 8: 27 `skill` rows). A monster's own bones stay
  with it.

## AR4.3: `Particles`, `Joints`, `Effects`, `WorldObjects`, `Fauna`, `Weather`, `MapScripts`

198 files and 43,853 lines to move (119 in `Scenes/`, 79 in `World/`). 210 baseline rows (179 mutual), 53
named-values rows.

### Tasks 16 to 18: the move, the namespaces and the comments

As the recipe. Task 16's Step 0 sends Kalima's ten files to `game/MapScripts/Kalima` (a map without an event, by the
Crywolf plan's rule) and each map's objects to its `game/MapScripts/<Map>`.

### Task 19: one way to spawn an effect

PR `refactor(AR4.3): the effect spawner`.

- The adapters of `0x53fe70`, `0x530860` and `0x518cf0` (four each) become one `EffectSpawner` port.
- `ParticleSpawn` replaces the 15-parameter signatures (109 calls).

### Task 20: `Joints` and `Effects` in one direction

PR `refactor(AR4.3): joints and effects in one direction`. They include each other today (13 and 6 includes): one
direction or one module, with the measured includes in the PR for Emanuel's review (spec, criterion 6).

### Task 21: the per-map chains

PR `refactor(AR4.3): the map scripts`.

- The chains `0x606660`, `0x604ab0`, `0x602ed0` and `0x6002e0` stay written sequences of guarded calls in the
  original's order, one per chain, and a test per chain pins its order of maps.
- `WorldObjects` uses `MapScripts`, never the reverse; the types a script takes (`WorldObject`, the object pass) move
  below both (Decision 1).
- A `constexpr` table by map id holds per-map data only, tested over every id from 0 to 255.
- A `<Map>.h` per map script for its ids, and the shared texture slots as `Render::TextureSlot` in `game/Assets`
  (`0x5ac` once).

### Tasks 22 to 24: Crywolf's map, the weather and the fauna

- Task 22, `refactor(AR4.3): Crywolf's map script`. The object hooks stop taking `GameObjectPass`, and
  `StepCrywolfObject` joins them. The altar meshes leave `CharacterDrawing.cpp`, and `CreateArrivalSparks` leaves
  `CameraTour.cpp` for `Effects`. `frameCount` (`0x723c0c`) and `glowRotation` (`0x723c10`) leave `crywolf` for
  `game/MapScripts/Crywolf`, once a check shows that no block copy crosses `0x723c04..0x723c13`.
- Task 23, `refactor(AR4.3): the weather's state`. `crywolf.weather` (`0x5763358`) and `leafWeather` go to `Weather`.
- Task 24, `refactor(AR4.3): the fauna's record`. The record `Birds.cpp` writes by offset gets its named fields (AR1
  Task 5).

## AR4.4: the item, NPC, social and chat modules

`Items`, `Tooltips`, `Npc`, `Quests`, `ChaosMix`, `Warehouse`, `PetTrainer`, `Party`, `Social`, `Messenger`,
`PersonalShop`, `Chat` and `Guild`. 163 files and 46,976 lines to move (135 in `Scenes/`, 28 in `UI/`). 231 baseline
rows (195 mutual), 230 named-values rows (156 of them `text`).

### Tasks 25 to 27: the move, the namespaces and the comments

As the recipe.

### Task 28: `Items` and `Tooltips`

PR `refactor(AR4.4): the inventory's services as ports`.

- `InventoryServices` (10 callbacks; 37 files name it) becomes `Kernel` ports and narrow parameters.
- `cursorItem`, `cursorItemCount`, `cursorGrid`, `dialogItem`, `dialogItemCell`, `itemMoveTarget`,
  `hoveredItemCopy` and `floor` go to `Items`' state, the ones contiguous in the original together.
- Item vocabulary Task 12 in the moved files. The six decisions the item abstraction analysis left open are asked
  before this PR's code.

### Task 29: `Npc`, `Quests`, `ChaosMix`, `Warehouse`, `PetTrainer`

PR `refactor(AR4.4): the NPC windows' states`. `npcWindows` and `quest` leave `WorldRuntime`. `BD 09` stays in
`ChaosMix`. The field nobody reads (Crywolf plan, "Found on the way") is a port fix with its own yes, outside this PR.

### Task 30: `Social`, `Party`, `Messenger`, `PersonalShop`

PR `refactor(AR4.4): the social state in three`. `social` leaves `WorldRuntime` and `SocialState` splits into trade,
party and duel, except what is contiguous. `LetterPortrait` builds the actor through `Actors.h`, not its internals.

### Task 31: `Chat` and `Guild`

PR `refactor(AR4.4): the chat's and the guild's state`.

- `chat` and `guild` leave `WorldRuntime`.
- The chat rooms' three `WorldEvent` alternatives (`ChatRoomPacket`, `ChatRoomClosed`, `ChatRoomPingDue`) become
  `Chat/In/` handlers, and the target of `App/IChatRoomLinks.h` becomes `game/Chat`.
- `<Module>Texts.h` for the lines two files of a module print, `Tables/TextIndex.h` for the lines several modules
  print, and the 145 lines passed by their address become indices (named-values Task 8).

## AR4.5: `Ui`, `Hud`, `Events`, `CastleSiege`, `FrontEnd`, and the session

219 files and 47,811 lines to move (111 in `Scenes/`, 82 in `UI/`, 26 in `World/`), and the 74 files of `game/Scenes`
and `app` (15,567 lines, Decision 7). 287 baseline rows (247 mutual), 124 named-values rows.

### Tasks 32 to 34: the move, the namespaces and the comments

Task 32 runs `move.py --modules game/Ui,game/Hud,game/Events,game/CastleSiege,game/FrontEnd,game/Scenes --root app`.
Its Step 0 retargets the six `App/` headers the game still includes (`SessionState.h`, `LoopTiming.h`,
`FaithfulPacer.h`, `ServerAddress.h`, `PacingMode.h`, `ILoopClient.h`; Task 31 took `IChatRoomLinks.h`) to the
modules that own what they hold. After this move, `Client/src` holds only `engine/`, `game/` and `app/`.

### Task 35: the panels

PR `refactor(AR4.5): one panel per module`.

- `PanelRegistry` with one `IPanel` per module and the exclusion rules as a table. The HUD stops drawing each
  module's windows by hand.
- `GamePanels` is deleted, with the ~85 `std::function` it rebuilds every frame (ADR 0026).
- The kit's metrics and palette in `game/Ui`, and `Render::PackedColor` (named-values Task 8). The two `BoxPixels`
  helpers become one.

### Task 36: the events

PR `refactor(AR4.5): one folder per event`.

- `EventPackets.cpp` (1,634 lines) splits between `game/Events` (Devil Square, Blood Castle and Chaos Castle together,
  with `EventMapObjectFrame`) and `game/Events/Crywolf`, each event's handlers in its `In/`.
- Crywolf: `stage` (`0x723c05`), `state` (`0x723c04`) and the interface leave `crywolf` for the event. HUD children 21
  and 26 stop taking `GameHudContext`, the UI step leaves `GameUi.cpp`, and the camera tour takes its creations and
  the draw tail as arguments. `sandOverlay` (`0x576353c`) goes to `Hud`.
- `events`, `eventObject`, `chaosCastleArea` and `chaosCastleTrapArmed` go to `Events`' state, and each event's ids to
  its `<Event>.h`.

### Task 37: `CastleSiege` and `Hud`

PR `refactor(AR4.5): the castle's and the HUD's state`. `castleSiege`, `hud`, `ui`, `panels`, `dialogs`, `dialogBox`
and `oldWindows` leave `WorldRuntime`. Item vocabulary Task 12 in the moved files.

### Task 38: what is left of `WorldRuntime`

PR `refactor(AR4.5): the last fields of the runtime`. Each field still in `WorldRuntime` goes to its owner: the ones
most modules share (`map` in 19 modules, `ownRecordDepth` in 24) to `Kernel::GameContext`, the rest to the module
`globals.tsv` names. Then `WorldRuntime.h` is deleted, and `GameContext::Runtime()` with it. `WorldState.h` keeps only
the scenes' facade, and its target becomes `game/Session`, which no module includes anymore.

### Task 39: the session split

PR `refactor(AR4.5): the connection apart from the routing`. Skills: `clean-architecture`, `refactoring` (Extract
Class, Move Function).

**Files:**
- Create `game/Session/Connection.{h,cpp}`: keepalive, serial, keys, and the transport of the login and the server
  change.
- Create `game/FrontEnd/In/` and `Out/`: the login, the server list, the character list, creation, deletion and
  selection, F3 03 and F3 10.
- Modify `ServerSession.{h,cpp}`: the routing and the send queue only.

- [ ] **Step 1: Pin** the flows that have no test of their own yet: the order of packets of a login with an auto
  login, of a map server change and of a return to the server list.
- [ ] **Step 2: Extract `Connection`**, with the session's tests green.
- [ ] **Step 3: Move the 15 flow members** of `ServerSession` to `FrontEnd/In/` handlers routed by `Route`, F3 03 and
  F3 10 with them. The 4 transport members (`OnOpened`, `OnClosed`, `OnPacket`, `OnUndecryptable`) stay in the
  connection. `byte6c8e4d` goes to the session's state.
- [ ] **Step 4: Check** `tests/app`, `tests/session` and `tests/protocol`, then push.

### Task 40: what carried every packet goes

PR `refactor(AR4.5): drop the session's event variant`.

- [ ] **Step 1: Split `tests/app/test_server_session.cpp`** per handler, next to each handler's module tests, with the
  same input bytes and the same assertions.
- [ ] **Step 2: Delete** `WorldEvent.h`, `CharacterEvent.h`, `ISessionEventSink.h`, `SessionEventApplier.{h,cpp}` and
  `/bigobj` on `ClientLib`. Check that the MSVC Debug job passes without `/bigobj`.
- [ ] **Step 3: Update the `muret-packet` skill's test recipe**, which builds a `BareWorld` and sets `routedWorld` on
  the sink that goes, then run the skill's scenario again.
- [ ] **Step 4: Check** that `grep -rn "WorldEvent\|ISessionEventSink\|SessionEventApplier" Client/src Client/tests`
  prints nothing, then push.

## After AR4.5

### Task 41: the `Game` library

PR `refactor(AR4): the game as one library`. Skill: `cpp-pro`; AR1 Task 8 is the precedent.

- [ ] **Step 1:** `Game` (`Game.dll`, `libGame.so`) from `Client/src/game/**`, linking `Engine`; `ClientLib` keeps
  `app/`. `GAME_API` from `generate_export_header`, placed by AR1 Task 8's script (the symbols one binary defines and
  another asks for), with hidden visibility and `-Wl,--no-undefined`.
- [ ] **Step 2:** a test whose includes reach the game links `Game` and the game's test support, not `ClientLib`.
- [ ] **Step 3:** `MU_MONOLITHIC=ON` builds `Game` static too. ADR 0028's consequences get the `Game` line.
- [ ] **Step 4: Check** all four CI configurations, then push.

## AR4 is done when

- `Client/src` holds `engine/`, `game/` and `app/` and nothing else, and every game file declares its folder's
  namespace.
- `layers-baseline.tsv` has no row of a game module, and `check_layers.py` prints no cycle that contains one.
- `WorldRuntime.h`, `WorldEvent.h`, `CharacterEvent.h`, `ISessionEventSink.h` and `SessionEventApplier.{h,cpp}` do
  not exist.
- Every routed row's handler is in its module's `In/`, and `ServerSession.h` has no `Send…` or `Request…` of a
  module's intent.
- `Game` builds as its own library on the four CI configurations.
- `metricas.tsv` gives the same port numbers as Task 1's snapshot, except what the port closed in between.

## Decisions

Recommended and written into the tasks unless Emanuel says otherwise. Task 1's ADR records the answers.

1. **Who may use whom** (Crywolf Task 7, question 1).
   - **A: the hosts** (`WorldObjects`, `Monsters`, `Hud`, `Movement`, `Npc`, `Weather`) **use the event and the map
     script, never the reverse**, and the types a script takes move below both. This plan does that.
   - B: each chain's calls are handed down from `game/Scenes`, so the hosts never know the scripts; one function per
     chain, assembled above.
2. **How the state leaves `WorldRuntime`.**
   - **A: module by module.** Each design moves its module's state to `Session::ModuleStates` and its files stop
     including `WorldRuntime.h` and `WorldState.h`. The aggregate's 71 rows in `Kernel` leave with the modules they
     point at, and Task 38 deletes `WorldRuntime.h`. This plan does that.
   - B: retarget `WorldRuntime.h` and `WorldState.h` to `game/Session` now. `Kernel` is clean in AR4.1, but 346
     includes from layer 9 become baseline rows of their modules, in one rewrite of the baseline.
3. **The monsters of a map** stay in `game/Monsters`, in files named for the map (recommended, Crywolf plan), or go
   with the map script.
4. **One `modules.tsv` row per event and per map script** (`game/Events/Crywolf`, `game/MapScripts/<Map>`), so a
   violation names its event or map and each leaves the baseline alone (recommended), or folders of one module as
   today.
5. **"In no declared cycle" joins the done criterion** of a milestone (recommended). None of AR4.1's modules is in
   today's declared cycle; four of AR4.2's are.
6. **The game's library:** one `Game`, built in Task 41 (recommended, ADR 0028 "starting with one"), or a few.
7. **`game/Scenes` and `app` move with AR4.5** (recommended), or in AR5.

## Self-review

- **Coverage:** the master plan's five milestones and every knot of its table; named-values Task 8's homes; item
  vocabulary Tasks 11 and 12; the Crywolf pieces of AR4.2 to AR4.5 and its Task 7; AR3.2 Task 6 Step 2, Task 7 Steps
  2 and 3, and Task 8; AR2.2 Task 12's second half and AR2.3 Task 18; ADR 0026's two allocation sites; ADR 0028's
  `Game`.
- **What stays out:** the port fixes of the Crywolf plan's "Found on the way"; AR5's scene scripts and `main.cpp`
  phases; AR6's `MapId` enum and deduplication; the item draw plan's tasks, which keep their own plan; the music plan,
  which runs on its own.
- **Names used the same way in every task:** `Session::ModuleStates`, `Kernel::IPacketSender`,
  `Kernel::ISoundRequests`, `Kernel::IOnScreenMessages`, `EffectSpawner`, `ParticleSpawn`, `PanelRegistry`, `IPanel`,
  `Connection`.
- **Estimates:** the files, lines and rows of each milestone were counted at 4cbf432b from the map and the baselines.
  The master plan's counts stay where this plan did not measure again: the ~2,090 vectors, the ~85 `std::function`,
  the 109 `ParticleSpawn` calls and the effect adapters.
- **Detail:** AR4.1 is written to the step. AR4.2 to AR4.5 are written to the task, with what each must deliver; each
  milestone's Step 0 measures again against that day's `main`, as the master plan asks of every epic.
