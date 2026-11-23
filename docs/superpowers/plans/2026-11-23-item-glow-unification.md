# One item draw: findings and plan for the item glow unification

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** every item the client draws (worn parts, what hangs from a character, floor items, UI items, effect items)
goes through one implementation of the original's item draw `0x60b3d0`, one of its part wrapper `0x60cf70` and one of
the hanging routine `0x509340`, so the level glow, the excellent and ancient passes and the options' glow detail behave
the same everywhere, as in `main.exe`.

**Architecture:** the original has a single routine for the level glow; MuRet ported it twice (a first one for the
login in CL6, a full one for the game from CL7 on) and worn armor still runs on the first. The two do not give the same
result, so by the refactor's rule the difference is a port bug: first a test pins every difference, then port fixes
move the player parts to the full port, and only then pure refactor PRs delete the first one and fold the scepter case
into the weapon sprite table it belongs to.

**Tech Stack:** C++20, GoogleTest, the recording `ModelBatch`, the behavior golden (`Client/tests/characterization`),
the selection GL trace golden (`Client/tests/scenes/test_golden_character_drawing.cpp`).

**Spec:** the evidence in this document (there is no separate spec), the refactor design
[`2026-11-20-client-architecture-design.md`](../specs/2026-11-20-client-architecture-design.md) and the master plan
[`2026-11-20-client-architecture-refactor.md`](2026-11-20-client-architecture-refactor.md), "Epic AR6" and "The two
lanes".

Measured on `refactor/client` at 642ebe5 (2026-11-23) against the decompilation of `main.exe` c32bbd5f in the research
repo (`client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/`). Nothing here was built or run: every MuRet claim
comes from reading the code and every claim about the original from its pseudocode and ASM. Task 1 turns the list of
differences into a test.

## The answer in short

- **Yes, it is worth unifying, and the original backs it:** the level glow is one routine, `0x60b3d0`, with exactly two
  call sites. Every item reaches it with the same table: the level picks the passes and the color factor, the item
  picks the color, the options' detail trims it.
- **`ScepterGlow` is not a level glow.** It is case `0x620` of a 26-model switch at the end of `0x509340` that asks for
  sprites on the bones of some weapons, whatever their level. MuRet has the other 25 cases inline in
  `GameHanging.cpp` and this one apart in `World/ScepterGlow`, because it was ported first for the login.
- **MuRet has two ports of the same routines** and the player's worn armor, in the game, the login and the selection,
  still runs on the first, which lacks five things the original does (below). This is visible in the game today.
- **Recommendation:** unify on the full port, in the order pin, fix, delete. Not as one module called `ItemGlow` that
  swallows everything, but as one owner per original function: the glow table keeps its name inside it.

## What the original does

### One routine draws an item with its glow: `0x60b3d0`

`0x60b3d0(object, model, light, alpha, level << 3, excellent, ancient, select, mode)` has two call sites:

| Call site | In | Who gets there |
| --- | --- | --- |
| `0x60d44b` | `0x60cf70`, the part draw | 13 call sites in 7 functions: `0x50ba30` ×6 (the shadow of a player `0x50bcec` and of any other actor `0x50beb6`, the skeleton bodies `0x50d309` and `0x50d356`, the Chaos Castle Angel `0x50d3c9`, the six worn parts `0x50d4e0`), the effect items `0x52d260`, `0x52d400`, `0x52d580` and `0x52d620`, the UI item `0x5e0720` and the floor item `0x608a00` ×2 |
| `0x509a8a` | `0x509340`, what hangs | 8 call sites, all in `0x50ba30`: weapons, shields, wings, the pet, the quiver |

What it does, in order:

1. The level is `(arg >> 3) & 15`. A per-model remap changes it (`0x60b4aa..0x60b6d5`: bolts and arrows `2n + 1`, and
   a fixed 0, 8 or 9 or a formula for some models of the `0x1a`, `0x1c`, `0x1e` and `0x22` families). Then the options'
   detail (`0x6c9dcc`, 0 to 4, 4 in `.data`) trims it: below 4, the level is at most `2 · detail + 5`.
2. Per-model early draws that return (`0x1e2d`, `0x1a27..0x1a2a`, `0x1c21`…) and per-model effects and light stores
   (`0x60be05..0x60c5c3`).
3. The object's state flags `+0x78` (`0x60c5d1..0x60c740`): with `0x4000`, one `0x4040` pass with slot `0x620` instead
   of the body; with `1`, the color (0.3, 1, 0.5), or (0.3, 1, 1) with `2` too; without `1`, with `0x1000` the effect
   `0x628` and the body in (0, 0, 1), and with `2` or `0x20` the color (0.3, 0.5, 1).
4. The level table (`0x60c740..0x60cb3b`). The base pass is always `0x609640`, which has its own 51 per-model cases:

   | Level | Model color | Passes after the base | Needs detail |
   | --- | --- | --- | --- |
   | 0 to 2, 14, 15 | light | none | |
   | 3, 4 | light × pulse × (1, 0.6, 0.6) | none | |
   | 5, 6 | light × pulse × (0.5, 0.7, 1) | none | |
   | 7, 8 | light × 0.8 | `0x44` | ≥ 1 |
   | 9, 10 | light × 0.9 | `0x44`, `0x48` | ≥ 2 |
   | 11, 12 | light × 0.9 | `0x240`, `0x48`, `0x44` | ≥ 3 |
   | 13 | light × 0.9 | `0x1040`, `0x48`, `0x44` | 4 |

   The pulse is the global `0x7d36ba8`, `sin(T · 0.004) · 0.15 + 0.6`. The glow passes go through `0x60b130` and
   `0x60b300`, which take the pass color from the item (`0x608e00`, `0x6093d0`, `0x609560`).
5. With detail above 0 and without the flags `0x20` and `0x4000`: an excellent item (`excellent & 0x3f`, except
   `0x1a12..0x1a18` and `0x1c30`) adds the `0x42` pass and returns; otherwise an ancient one (`ancient % 4` is 1 or 2)
   adds the `0x840` pass with a pulse as alpha and, while the pulse is at its floor, draws `rand() % 100` into the
   model's `+0xc0`.

So the intensity does change only with the level, as it looks in the game. What changes per item is the color of the
passes, the base pass of some models and a few models with a look of their own.

### The part draw `0x60cf70` and the worn parts loop

`0x60cf70` is the wrapper every item but the hanging ones uses: it leaves with alpha at 0.01 or less, draws the two
outline passes (`0x60cd60`) when the owner is under the mouse, sets the light (`0x5ffce0`), transforms the meshes
(`0x4ebc80`) and calls `0x60b3d0`. The worn parts loop (`0x50d3dc..0x50d4f5`, from the boots to the head) is the same
code for players and for every other actor: it writes the class skin into the model's `+0x98`, the class 4 helm writes
the object's `+0x68`, and it calls `0x60cf70` with the part's level, excellent and ancient bytes.

### What hangs: `0x509340`, and where the scepter is

`0x509340` poses the item on its bone, calls `0x60b3d0` (`0x509a8a`) and then runs a switch on the model that asks for
sprites, joints or particles on its bones, with one `rand() % 30 + 70` before it. The switch has 26 models:
`0x225..0x229`, `0x231`, `0x619..0x620`, `0x81c`, `0xa16`, `0xa17`, `0xa18`, `0xa23`, `0xa25`, `0xa26`, `0xa27`,
`0xc1c`, `0xc1d`, `0xc1e` and `0xe21`. The scepter is `0x620`. None of the cases reads the level.

## What MuRet does today

### Every path that draws an item

"First" is the CL6 port (`DrawModel` and `DrawPasses`, `CharacterDrawing.cpp:534..639`); "full" is
`DrawItemFromMeshes` (`CharacterDrawing.cpp:1729`) with `DrawEarlyItemGlow`, `PrepareItemGlowEffects` and
`DrawSpecialItemMeshes`.

| What is drawn | Original | MuRet | Port | Detail it uses |
| --- | --- | --- | --- | --- |
| Worn parts of a player, game | `0x50d4e0` | `GameCharacters.cpp:463`, `DrawCharacter(Parts)` | first | constant 4 |
| Worn parts of a player, login and selection | `0x50d4e0` | `LoginScene.cpp:1763`, `CharacterSelectScene.cpp:1667`, `DrawCharacter(Body)` | first | constant 4 |
| Worn parts of any other actor | `0x50d4e0` | `MonsterDrawing.cpp:1046`, `DrawThePart` | full | constant 4 |
| Skeleton body (`0xd1..0xd3`) | `0x50d309`, `0x50d356` | `CharacterDrawing.cpp:1795`, `DrawSkeletonBody` | first for the outline, full for the body | constant 4 |
| Chaos Castle Angel | `0x50d3c9` | `CharacterDrawing.cpp:1097`, `DrawChaosCastleBody` | first for the outline, `0x609640` called directly | none |
| What hangs, the three scenes | `0x509a8a` | `GameHanging.cpp:731`, `HangItem` | full | constant 4 |
| What hangs, without a world or a skeleton | `0x509a8a` | `CharacterDrawing.cpp:802`, `DrawHangingFrom` (`CharacterDrawPass::All`) | first | constant 4 |
| Floor items | `0x608a00` | `GameFloorItemDrawing.cpp:109` | full | the options |
| UI items | `0x5e0720` | `UiItemDrawing.cpp:762` | full | constant 4 |
| Effect items | `0x52d260`… | `GameDrawTail.cpp:566` | full | constant 4 |

### What is ported twice

| Original | Kept | Duplicate |
| --- | --- | --- |
| `0x60b3d0`, the item draw | `DrawItemFromMeshes` | `DrawPasses` and the glow half of `DrawModel` |
| `0x60cf70`, the part draw | `DrawThePart` (`MonsterDrawing.cpp:1046`), the most complete | the other half of `DrawModel`, and the blocks written by hand in `DrawSkeletonBody` and `DrawChaosCastleBody` |
| `0x50d3dc..0x50d4f5`, the parts loop | the loop of `DrawTheParts` (`MonsterDrawing.cpp:1121`), which cuts on a class other than 0 | `PartsInOrder`, `LightOf`, `DrawParts` (`CharacterDrawing.cpp:703..773`) |
| `0x60cd60`, the outline of a part | `DrawObjectOutline` with `PartOutlinePassesOf` | `DrawOutline` with `OutlinePassesOf` (`CharacterDrawing.cpp:94`, `485`) |
| `0x509340`, what hangs | `HangItem` and `ItemSprites` (`GameHanging.cpp`) | `DrawHangingFrom`, `DrawHanging`, `DrawQuiver`, `DrawHands` (`CharacterDrawing.cpp:802`, `870`, `902`, `981`), which know only the scepter case |
| `0x4ecc30`, the UV of a shine pass | `ItemShineTexCoords` (`GameItemGlowEffects.cpp:226`, the clock as the original truncates it) | `ShineTexCoords` (`Render/Models/Shine.cpp:40`, the clock in whole milliseconds) |
| `0x607b60` and the light stores | `NativeItemObjectState` plus `StateLight` | `HangingObjectState`, `TypeLightOf`, `ModelLightOf` (`ObjectState.cpp:157..214`) |
| the level remap `0x60b4aa..` | `ItemLevel` (`CharacterDrawing.cpp:1645`) | `QuiverGlowLevel` (`ItemGlow.h:43`) |
| the detail trim | `DrawItemFromMeshes` (`CharacterDrawing.cpp:1741`) | `ItemGlowOf` (`ItemGlow.cpp:19`) |

`World/ScepterGlow` is the scepter case kept apart from its 25 siblings, with a class of its own,
`ScepterGlowCarry`, that no production code sets: `DrawnCharacter::scepter` is only assigned in
`tests/scenes/test_character_drawing.cpp:1902`.

### Where the two ports differ

On the worn parts of a player, which run on the first port, MuRet does not do what the original does:

| # | The original | The first port |
| --- | --- | --- |
| 1 | trims the level with the options' detail and turns excellent and ancient off at 0 (P-C91, already recorded) | always detail 4 |
| 2 | levels 3 to 6 pulse red and blue with `0x7d36ba8` | the plain light, no pulse (`ItemGlow.h` records it under P-C18) |
| 3 | the base pass is `0x609640`: chrome under the armor, pants, gloves and boots `0x1221`, `0x1226`, `0x1229` and their families (12 models), the look of the `0x101b` family in mode `0x102` | the plain base pass for every model (P-C14 e) |
| 4 | an ancient part draws `rand() % 100` from the CRT while its pulse is at the floor | no `rand()`: the CRT stream differs from the original's |
| 5 | the shine UVs use the clock as `0x4ecc30` truncates it, in x87 precision | whole milliseconds and `float` math |

In both ports: the state flags block of step 3 (`0x60c5d1..0x60c740`) is not ported. No source cites that range, no
source has those tints, and the reference counts 379 of the 6,442 bytes of `0x60b3d0` as missing. A poisoned or frozen
character does not change color.

What the full port lacks to draw a player's parts, so it is added before the switch: the class skin (`+0x98` of the
model; `RetainedObjectMeshDraws` draws with skin 0), the class 4 helm (`0x50d43d..0x50d4a4`, today a cut at
`MonsterDrawing.cpp:1135`), and the selection's light and pose, which arrive in `DrawnCharacter` and not in
`ActorState`.

## Recommendation

### The target: one owner per original function

| Original | Owner | Fed by |
| --- | --- | --- |
| `0x60b3d0` | `DrawItemFromMeshes`, with the detail and the pulse read from `ItemDrawContext` (filled in one place, `FillItemDrawContexts`), not passed by each caller | `ItemGlowOf` (the level table), `ItemLevel`, `DrawEarlyItemGlow`, `PrepareItemGlowEffects`, `DrawSpecialItemMeshes`, `PassColors.h` |
| `0x60cf70` | `DrawItemPart`, new, taken out of `DrawThePart` | every caller of the part draw |
| `0x50d3dc..0x50d4f5` | `DrawWornParts`, new, taken out of `DrawTheParts` | players and the rest |
| `0x509340` | `HangItem` and `ItemSprites` with its 26 cases, the scepter among them | the three scenes |
| `0x4ecc30` UVs | `ItemShineTexCoords` | every shine pass |

The name: `0x60b3d0` draws the whole item, base included, so the unit is the item draw and `ItemGlow` stays as the name
of its level table. The item draw gets a file pair of its own (`ItemDrawing.h`, `ItemDrawing.cpp`) cut from the 2,563
lines of `CharacterDrawing.cpp`, so `UiItemDrawing`, `GameFloorItemDrawing`, `GameDrawTail` and `MonsterDrawing` stop
including the character drawing to draw an item.

### What it gives and what it costs

- One place to fix. The state flags block, P-C91 and anything a trace shows later are ported once, not per path.
- About 600 production lines go: the first port in `CharacterDrawing.cpp` (about 400), `ShineTexCoords`,
  `HangingObjectState` with its two helpers, `QuiverGlowLevel` and `ScepterGlowCarry`.
- The cost is in the tests. `test_character_drawing.cpp` has 88 tests on the first port and three more test files call
  `DrawCharacter`; the ones that pin a behavior verified in a trace move to the unified path, none is dropped without
  an equivalent.
- The risk is the selection. The first port is the one verified against the original's GL trace
  (`test_golden_character_drawing.cpp`: `Male17` at level 13, `PantMale02`, `Sword17` at level 13, `Helper02`, with the
  outline). That test is the gate of the switch: it must pass on the unified path without touching its golden.

### Where it fits in the AR plan

AR6 says the functions ported twice "end up as one, if both give the same result (otherwise the difference is a port
bug: it opens separately)". Here they do not give the same result, so the work splits:

1. **Port lane, Tasks 1 to 4.** A test that pins the differences, then port work. Tasks 2 and 4 change behavior on
   purpose: each rewrites the behavior goldens it changes and says so in its description, as
   `tests/characterization/README.md` asks of a port PR.
2. **Refactor, Tasks 5 to 7.** Pure deletions and one move of code, golden identical. By the letter they are AR6. I
   recommend running them right after Task 4 and before AR4.1, which moves `Actors` with the character drawing:
   otherwise AR4.1 moves, splits and translates about 600 lines that are about to be deleted, and two live paths drift
   meanwhile.
3. **Port lane, Task 8.** The state flags block, ported once in the unified routine.

It should not start while a PR of AR1.1 touches `CharacterDrawing.h` (the engine types leave that header in AR1.1), and
by the two lanes' rule no task runs the day its files are being moved.

### Decisions for Emanuel

1. **The order above, or strictly AR6.** Decided (Emanuel, 2026-11-23): the port fixes now, the deletions before
   AR4.1. Task 1 starts when #424 merges: it moves `IModelLibrary` out of `CharacterDrawing.h` into
   `Render/Models/IModelLibrary.h`, so the signatures below name `Render::Models::IModelLibrary` from then on.
2. **Where the port fixes land.** Recommended: `refactor/client`, because the behavior golden and its harness only
   exist there; `main` has had no port PR since #403.
3. **The name.** Recommended: `ItemDrawing` for the unit and `ItemGlow` for its level table. `ItemGlow` for the whole
   unit also works; it only says less than what the routine does.
4. **Three engine rows in the module map.** `Render/Models/ItemGlow`, `PassColors` and `ObjectState` hold MU knowledge
   (the level table, item model ids) and `module-map.tsv` sends them to `engine/Models` with medium confidence.
   Recommended: retarget them to `game/Actors/Drawing`, with the rest of the item draw, before AR1.2 moves
   `Render/Models/**`. `Shine.h`, the enum of the passes, stays: `0x4ecc30` is the mesh renderer.

## Global Constraints

- Fidelity first: a change is right when it does what `main.exe` does at the cited address. Every new block cites its
  `0x…`.
- The selection GL trace golden never changes in this plan. If the unified path breaks it, the path is wrong.
- A port PR (Tasks 2, 4 and 8, and Task 3 if a golden moves) rewrites only the behavior goldens its change explains and
  lists them in its description. A refactor PR (Tasks 5 to 7) changes no golden and no test expectation.
- The order of the original's calls and of `rand()` calls follows the original; between two original frames nothing
  draws from the CRT or writes a store (`ItemDrawContext::originalFrame`).
- No file is renamed outside `move.py`; a task that deletes a file checks `git diff -M50% --name-status` shows no `R`
  line. New files add their row to `Client/docs/research/coverage/module-map.tsv` in the same PR.
- Everything in English; a comment only if strictly necessary, one line, keeping the `// 0x…` citations. New code calls
  `RoundedMath.h`, never `std::sin` or glm trig.
- PRs target `refactor/client`. One task per PR, and each PR is one step: Emanuel says yes before the next one starts.
- GitHub CI is the build and the final word: the cloud container builds only the client without SDL and GL.

## Review Focus

- A class whose skin is not 0 (every class but the first): the skin and hair meshes take slot `0x12e + skin` and
  `0x12f + skin`, with `skin = 2 · (class & 7) + (class >> 3)`. Task 3 pins classes 1 to 4 and a second class.
- The class 4 helm: the light it writes into `+0x68` stays for the parts drawn after it and for the boots of the next
  frame. Task 3 pins two consecutive frames.
- A draw between two original frames: an ancient part at the floor of its pulse draws one `rand()` per original frame,
  not per draw. Task 4 pins the CRT state after two draws in one original frame.
- The glow lowered in the options while wearing a level above the trim: parts, hanging, UI, floor and effect items dim
  alike, and excellent and ancient go off at 0. Task 2 pins each caller.
- A part whose model the library does not have: nothing drawn and the picking box still published, as with the zeroed
  record of `0x60dbe0`. Task 3 pins it for a player.

---

## Tasks

Tasks 1 to 3 are specified here. Tasks 4 to 8 are milestones: the list Task 1 produces decides their steps, and each
gets its step list in its own PR description before it starts.

### Task 1: pin where the two item draws differ

No production code changes. Title: `test(AR6): pin where the two item draws differ`.

**Files:**
- Create: `Client/tests/scenes/test_item_draw_equivalence.cpp` (`muret_add_module_tests()` picks it up)

**Interfaces:**
- Consumes: `DrawCharacter(batch, models, character, clock, CharacterDrawPass::Parts)`; `SkinObjectMeshes(models,
  object, bones)`; `DrawItemFromMeshes(batch, models, object, clock, meshes, level, excellent, ancient, detail,
  context)`; the recording `ModelBatch`.
- Produces: `struct ItemDrawCase { std::uint16_t model; std::uint8_t level, excellent, ancient, characterClass, detail;
  bool outlined; float alpha; }` and `std::optional<std::string> FirstDifference(const ItemDrawCase&)`, which draws one
  worn part through each port over the same pose and returns the first command that differs (pass index, mesh, then
  blend, slot, color or UV), or nothing.

- [ ] **Step 1: Write the test table.** One parameterized test over: levels 0 to 15; plain, excellent, ancient;
  models `Male17` armor, one of `0x1221`, `0x1226`, `0x1229`, and `0x121b`; classes 0 and 1; detail 4 and 0; outlined
  and not; alpha 1 and 0.5. Each row states its expectation: `Same`, or `Differs` with the number of the difference in
  "Where the two ports differ".
- [ ] **Step 2: Run it on CI** and correct the expectations to what the two ports really do. A difference outside the
  five numbered ones is a finding: add it to this document's table in the same PR.
- [ ] **Step 3: Commit.** The test passes: it documents today's differences, and Task 4 turns every `Differs` row
  into `Same` or deletes the row with the first port.

### Task 2: the options' glow detail reaches every item draw (P-C91)

Port fix. Title: `fix(client): apply the glow detail option to every item draw`.

**Files:**
- Modify: `Client/src/Scenes/CharacterDrawing.h` (`ItemDrawContext`, `DrawItemFromMeshes`), `CharacterDrawing.cpp:1729`,
  `ItemDrawResources.{h,cpp}`, and the six callers: `UiItemDrawing.cpp:762`, `MonsterDrawing.cpp:1103`,
  `GameHanging.cpp:731`, `GameFloorItemDrawing.cpp:109`, `GameDrawTail.cpp:566`, `CharacterDrawing.cpp:1862`
- Modify: the five places that build the context: `GameSceneTail.cpp:89`, `LoginScene.cpp:1910`,
  `CharacterSelectScene.cpp:1712` (through `FillItemDrawContexts`), `GameSceneUi.cpp:453` and `GameScene.cpp:1224` (by
  hand)
- Modify: `Client/docs/research/08-pendientes.md` (P-C91: what is left is the worn parts, closed by Task 4)
- Test: `Client/tests/scenes/test_game_item_glow_effects.cpp`, and one case in `test_ui_item_drawing.cpp`,
  `test_monster_drawing.cpp`, `test_game_hanging.cpp`, `test_game_floor_item_drawing.cpp` and `test_game_draw_tail.cpp`

**Interfaces:**
- Produces: `std::uint8_t ItemDrawContext::glowDetail = Render::Models::DefaultGlowDetail;` (`0x6c9dcc`) and the same
  field in `ItemDrawResources`, which `FillItemDrawContexts` copies. Each builder sets it from the `ui.glowDetail` of
  its `WorldRuntime` (the option is a global: it stays between scenes). `DrawItemFromMeshes` loses its
  `detail` parameter. `FloorItemDrawContext::glowDetail` goes.

- [ ] **Step 1: Write the failing tests.** `DrawItemFromMeshes` with a level 13 excellent item and
  `context.glowDetail` 0, 1, 2, 3 and 4 draws the passes of levels 5, 7, 9, 11 and 13, and the `0x42` pass only above
  0. One test per caller sets the detail to 1 in its resources and expects a level 13 item that is not excellent to
  draw the base and one `0x44` pass.
- [ ] **Step 2: Run them and see them fail** (five callers pass 4).
- [ ] **Step 3: Move the detail into the context** and delete the parameter.
- [ ] **Step 4: Run the tests and the behavior golden.** The golden stays identical: every scene runs with detail 4.
- [ ] **Step 5: Commit.**

### Task 3: the part draw and the parts loop for every actor

Port work: it removes the cut at `0x50d431`. Players are not switched yet, so no golden should move. Title:
`feat(client): draw the worn parts of any class with the part draw`.

**Files:**
- Create: `Client/src/Scenes/ItemDrawing.h`, `Client/src/Scenes/ItemDrawing.cpp` (target `game/Actors/Drawing`)
- Modify: `Client/src/Scenes/MonsterDrawing.cpp:1046..1145` (`DrawThePart` and `DrawTheParts` call the new functions),
  `Client/src/Scenes/CharacterDrawing.{h,cpp}` (`DrawnObject::skin`, used by `RetainedObjectMeshDraws`)
- Test: `Client/tests/scenes/test_item_drawing.cpp`

**Interfaces:**
- Produces, in `ItemDrawing.h`:

```cpp
// What 0x60cf70 reads from the object that owns the part, and the two fields it writes.
struct ItemPartOwner
{
    std::uint16_t type = 0;                                 // +2: 0xc5 outlines only its first mesh
    Render::Models::ObjectTransform transform;              // +0xc, +0x10, +0x1c
    std::span<const Render::Models::BoneTransform> bones;   // +0x120
    glm::vec3 light{1.0f};                                  // +0x3b4
    float alpha = 1.0f;                                     // +0x174
    std::int16_t hiddenMesh = Render::Models::NoHiddenMesh; // +0x58, the outline only
    std::int16_t blendMesh = Render::Models::NoBlendMesh;   // +0x64
    float* blendMeshLight = nullptr;                        // +0x68: read, and written by the class 4 helm
    glm::vec3* storedLight = nullptr;                       // the light the 0x121b case writes (0x60ad23)
    glm::vec2 uv{};                                         // +0x6c, +0x70
    std::uint32_t stateFlags = 0;                           // +0x78: no outline with 0x4000
    bool vertexLight = false;                               // +0xe8
    std::span<const ObjectOutlinePass> outline;             // empty when the mode is not 1
    float boneScale = 1.0f;                                 // 0x6c881c by the mode
};

struct ItemPart
{
    std::uint16_t model = 0;
    std::uint8_t level = 0;
    std::uint8_t excellent = 0;
    std::uint8_t ancient = 0;
    std::uint8_t skin = 0; // model + 0x98
};

// 0x60cf70
[[nodiscard]] Core::Expected<void, std::string> DrawItemPart(Render::ModelBatch& batch, const IModelLibrary& models,
                                                             const ItemPartOwner& owner, const ItemPart& part,
                                                             Core::Duration frameClock, const ItemDrawContext& context,
                                                             const std::function<void(const glm::vec3&)>& onPartBox);

// +0x1f8 + k * 0x20: the head, the helm, the armor, the pants, the gloves and the boots. Without a model, no part.
using WornParts = std::array<std::optional<ItemPart>, 6>;

// 0x50d3dc..0x50d4f5: from the boots to the head, the class skin in each model and the class 4 helm.
[[nodiscard]] Core::Expected<void, std::string> DrawWornParts(Render::ModelBatch& batch, const IModelLibrary& models,
                                                              const ItemPartOwner& owner, const WornParts& parts,
                                                              std::uint8_t characterClass, Core::Duration frameClock,
                                                              const ItemDrawContext& context,
                                                              const std::function<void(const glm::vec3&)>& onPartBox);
```

- `DrawThePart` and `DrawTheParts` keep their signatures. `DrawTheParts` keeps its guards (`+0x3e3`, Chaos Castle, the
  model outside the zeroed records) and loses the cut on `actor.characterClass != 0`.

- [ ] **Step 1: Write the failing tests.** For `DrawItemPart`: alpha 0.01 draws nothing; a missing model draws nothing
  and calls `onPartBox`; with `outline` set, two outline passes come before the base, and none with `stateFlags`
  `0x4000`. For `DrawWornParts`: the order is boots, gloves, pants, armor, helm, head; classes 1 to 4 and `0x09` (a
  second class) put the skin mesh on slot `0x12e + SkinIndexOf(class)`; the class 4 helm leaves `*blendMeshLight`
  written for the head of this frame and the boots of the next one.
- [ ] **Step 2: Run them and see them fail.**
- [ ] **Step 3: Cut `DrawThePart`'s body into `DrawItemPart` and `DrawTheParts`' loop into `DrawWornParts`**, with the
  helm block (`0x50d43d..0x50d4a4`) ported from the ASM. `DrawThePart` and `DrawTheParts` fill an `ItemPartOwner` from
  their `ActorState`. Add the skin to `DrawnObject`.
- [ ] **Step 4: Run the tests, `test_monster_drawing` and the behavior golden**, all identical.
- [ ] **Step 5: Commit.**

### Task 4: the worn parts of a player on the unified path

Port fix, the one that changes what is seen. `GameCharacters.cpp:463`, `LoginScene.cpp:1763` and
`CharacterSelectScene.cpp:1667` draw the parts with `DrawWornParts` over an `ItemPartOwner` filled from the
`DrawnCharacter` and its `ActorState`. Closes differences 1 to 5 and P-C91. Gate: `test_golden_character_drawing`
passes untouched, and every `Differs` row of Task 1 is `Same`. Its tests also pin that two draws inside one original
frame leave the CRT state and the `+0xc0` selections as one draw does. The behavior goldens that change are rewritten
and listed with the reason for each.

### Task 5: delete the first port

Refactor, golden identical. Goes: `DrawModel`, `DrawPasses`, `PartsInOrder`, `LightOf`, `DrawParts`,
`DrawHangingFrom`, `DrawHanging`, `DrawQuiver`, `DrawHands`, `DrawOutline` and its `OutlinePassesOf`,
`CharacterDrawPass::All` and `Hanging` with the two fallback branches (`LoginScene.cpp:1772`,
`CharacterSelectScene.cpp:1677`), `ShineTexCoords`, `HangingObjectState`, `TypeLightOf`, `ModelLightOf`,
`QuiverGlowLevel`, `ItemGlowOf`'s own trim, `ScepterGlowCarry` and `DrawnCharacter::scepter`. `DrawSkeletonBody` and
`DrawChaosCastleBody` draw their outline with `DrawObjectOutline`. The tests of the first port move to the unified
path one by one; a test with no equivalent is listed in the PR for Emanuel to decide.

### Task 6: the scepter joins the weapon sprite table

Refactor, golden identical. The `0x620` case of `World/ScepterGlow.cpp` goes next to its 25 siblings in `ItemSprites`
(`GameHanging.cpp:432`); `tests/world/test_scepter_glow.cpp` and its 300 emulated cases keep passing against it.
`World/ScepterGlow.{h,cpp}` are deleted.

### Task 7: the item draw in its own files, and the remaining callers of the part draw

Refactor, golden identical. `DrawItemFromMeshes` and its helpers (`CharacterDrawing.cpp:1418..1793`) move into
`ItemDrawing.cpp`. `DrawSkeletonBody`, `DrawChaosCastleBody`, the UI item, the floor item and the effect items call
`DrawItemPart` where their hand-written block is the same three steps (the alpha gate `0x60cf99`, `0x5ffce0`,
`0x4ebc80`); a caller whose block differs stays and the PR says why, with the address.

### Task 8: the state flags block

Port. A new pending in `08-pendientes.md` for `0x60c5d1..0x60c740`, then its port inside `DrawItemFromMeshes`: the
`0x4040` shell with slot `0x620`, the three tints and the `0x1000` case with effect `0x628`, and the excellent and
ancient passes off with `0x20` or `0x4000`.

## Self-review

- **Evidence coverage:** each row of "What is ported twice" has a task that removes its duplicate (Tasks 2, 4, 5, 6
  and 7); each numbered difference is closed by Task 2 or Task 4; the block missing in both ports is Task 8.
- **Not verified:** the list of differences comes from reading both ports, not from running them: Task 1 exists to
  replace it with a test. The 0.6 and 0.7 factors of levels 3 to 6 are the ones the full port already uses; the
  constants `0x6b8a20` and `0x6b8a5c` were not read from `.rdata` again here.
- **Consistent names:** `DrawItemFromMeshes`, `DrawItemPart`, `DrawWornParts`, `ItemPartOwner`, `ItemPart`,
  `WornParts`, `ItemDrawContext::glowDetail` and `ItemDrawing.{h,cpp}` are used the same way throughout.
- **Out of scope:** the other things called glow in the client (street lights, monster and skull effects, joints, the
  cloth flares) are not item glow and do not go through `0x60b3d0`.
