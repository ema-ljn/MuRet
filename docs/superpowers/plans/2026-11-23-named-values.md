# Named values: one home per space, brought in by the refactor

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** every number whose meaning is fixed outside the file that uses it (an opcode, an item code, a map, a sound,
a `Text.bmd` line, a record offset, a shared float) has one home, found by its kind, without changing what the client
computes; the rest stays where it is used.

**Architecture:** no common header. Each space gets one home in the module that owns its numbering (the wire in
`game/Protocol`, table keys in `game/Tables`, maps in `game/Maps`, sounds in `game/Sound`, models in `game/Assets`,
the x87 numbers in `engine/Math`), the opcodes as one list per server, as TrinityCore and cMaNGOS keep theirs. A file
keeps the name its value has *there* (a role) and takes the home's name as its initializer. A rule in CODING_RULES says
it, a ratchet check in CI keeps it from getting worse, and each epic of the refactor moves the spaces it touches.

**Tech Stack:** C++20, Python 3 standard library (the architecture tools), GoogleTest, the behavior golden.

**Spec:** the three reports of 2026-11-23 in the project files under `cliente/vocabulario-constantes/`: `census.md`
(the inventory, with `scripts/census/`), `placement.md` (homes, epics, the check, risks; `scripts/placement/`),
`prior-art.md` (ten projects, the C++ Core Guidelines, Fowler, the libraries checked with Context7) and
`opcodes-trinitycore.md`; the refactor's design
[`2026-11-20-client-architecture-design.md`](../specs/2026-11-20-client-architecture-design.md) and master plan
[`2026-11-20-client-architecture-refactor.md`](2026-11-20-client-architecture-refactor.md); the AR1 plan
[`2026-11-22-ar1-engine-apart.md`](2026-11-22-ar1-engine-apart.md); CODING_RULES §3, §7 "Types and aliases" and §14;
the item plan [`2026-11-23-item-vocabulary.md`](2026-11-23-item-vocabulary.md) (#431), which is this plan applied to
items and stays the owner of items.

Measured on `refactor/client` at e1004fe (2026-11-23).

## What the census found

`Client/src` has 13,669 named scalar constants; 12,262 (90%) are private to one `.cpp` or function. Only 13 of 144
enums carry values: MuRet names its numbers with `constexpr`. The pattern the owner saw in `FirstShield = 0xc12` holds
for every space: the value is named again in each file that needs it, under another name each time, and where a home
exists most files do not use it.

| Kind | Today (census, placement) | Home | Type | Lands in |
| --- | --- | --- | --- | --- |
| Opcodes and subcodes | 269 definitions, 309 of 346 protocol values in `Protocol/**` headers; 22 opcode values defined twice or more (`0xf3`: 6 definitions, 3 names) | **one list per server** in `game/Protocol` | `std::uint8_t` now; `Opcode`, `Subcode` (types review F5) in AR2.1 | Task 5, Task 6 |
| Packet offsets and sizes | 78 offsets, 76 private to a `Protocol/*.cpp`; sizes beside opcodes (`Protocol/Character/Opcodes.h:25-29`) | the packet's layout, `game/Protocol/<Module>/` | `Field<T, Offset, Endian>` | AR2.1 (planned) |
| Item codes, models, group bounds | 455 definitions for 135 values; `0x1c15` 22 times | `Tables/ItemCode.h` | item plan | item plan Tasks 1, 9 |
| `Item.bmd` and other table rows | 155 offsets read outside `Data/`; only `GateRecord` is typed | `game/Tables`, one record view per table | accessor per field | item plan Tasks 2, 3; Task 8 (`SkillRecord`) |
| Maps | 314 definitions for 25 values (`LastChaosCastleMap` 25 times) | `Maps/MapId.h` | `std::int32_t` constants, as the copies they replace; opaque enum in AR6 | AR1 Task 5 (ranges), Task 5d (single maps), Task 5e (the fauna's loose literals) |
| Character class | 77 definitions for 7 values, no header | `game/Kernel` | `enum class CharacterClass : std::uint8_t`, `BaseClassOf` | Task 7 |
| Sounds | 158 definitions for 100 values; `0x19` 23 times under 12 names | `game/Sound` | `Audio::SoundId` (`Core::Id`) | Task 8 |
| Non-item models, texture slots | 149 model definitions (`0x20a` 11 times); `0x5ac` 41 times under 29 names | `game/Assets` | `ModelId`, `Render::TextureSlot` | Task 8 |
| Skills, player actions, bones | 75, 181, 74 definitions | `Tables/SkillId.h`; `PlayerActions.h`, `PlayerBones` beside the player catalog | the stored primitive | Task 8 |
| `Text.bmd` lines | 811 definitions for 667 values, 70% used by one file; 145 lines passed by their address (`0x772ff5c`) | the file; `<Module>Texts.h`; `Tables/TextIndex.h` when modules share one | `std::size_t` | Task 8 |
| Floats as bit patterns | 1,046 definitions for 225 values; `0x3dcccccd` 117 times under 56 names | numeric names (`Tenth`, `Half`, `FullTurn`) in `engine/Math`; role names stay | the type the expression has today | Task 4, AR6 |
| Offset-named fields | 209 fields (`flags78`: 185 uses in 54 files) | the owner's state struct, named, offset in a comment | field | AR3.2, AR4 (planned in spirit) |
| UI geometry, colors | 1,849 definitions; 19 geometry pairs in 3+ files | the file; palette in `game/Ui` | primitive; `PackedColor` | AR4.5 |
| Effect type and subtype | 395 subtype, 348 effect type definitions | the kernel's file | `int` (types review A3) | stays |
| Addresses of the original | 2,675 address literals, 307 names carrying one | comments; `globals.tsv` | never a constant | AR3.1 (planned) |

Every number above is reproducible with the named script of `census.md` or `placement.md`.

## What the rest of the world does

Ten projects read (devilutionX, OpenRCT2, OpenTTD, OpenMW, OpenXcom, Arx Libertatis, Doom, ScummVM, MuMain, OpenMU)
and two server cores (TrinityCore, cMaNGOS). Only MuMain's `_define.h` + `_enum.h` (5,886 lines) keeps everything in
one place; the others keep **one header per id space, in the module that owns it** (devilutionX `tables/itemdat.h`,
OpenTTD's 69 `*_type.h`, OpenRCT2 `SpriteIds.h`). The C++ Core Guidelines say both halves: ES.45 "avoid magic
constants" and, of a constant one file uses, "better still, don't expose" it. A constants header is the textbook case
of coincidental cohesion and rebuilds everything that includes it.

**Opcodes, as TrinityCore and cMaNGOS:** both keep every opcode in one enum in `Opcodes.h` and one table with a row per
opcode (name, when it is accepted, how it is processed, handler), unported codes as rows with `Handle_NULL`, and
TrinityCore deduces the packet class from the handler's signature. AR2.2 already has the table, `Unported{va}` and
`Route<&Fn>`; what MuRet does differently is the numbers: per feature today, per module in the master plan. MU's head
codes carry subcodes of several modules (`F3`: character list, level-up points, PK level, inventory), so only one list
per server gives each code one home (`opcodes-trinitycore.md`).

**Not taken, and why** (`prior-art.md`): magic_enum (it reflects enumerators; MuRet's open ids have none, and its
range is [-128, 127]); Kaitai Struct (generated readers do not keep the original's read order or failures, and need a
runtime); XML plus code generation like OpenMU (it pays with four consumers; MuRet has one, and the master plan already
decided "no code generator for now"); data moved to TSV (a second source of truth beside `Item.bmd`); clang-tidy's
`readability-magic-numbers` (it flags unnamed literals, not a value named 22 times); `std::to_underlying` (C++23).

## Decisions

Emanuel chose both recommendations on 2026-11-23.

1. **Opcodes: one list per server, as TrinityCore (chosen), or per module, as the master plan says today.** One
   list: `Protocol/Opcodes.h` for the GameServer (head codes and their subcodes) and `Protocol/Connection/Opcodes.h`
   for the ConnectServer, numbers only, included by the layouts and the table and never by a handler. Per module keeps
   `0xf3` defined in four headers.
2. **The check starts now (chosen) or with AR4.1.** Now: opcodes and subcodes fail outside `game/Protocol` from
   day one, every other space is reported (`watch`) with today's ~1,100 duplicates in the baseline, and each space
   starts failing the day its home lands, in the PR that creates it. With AR4.1: no tool until then; the port can add
   opcodes outside `Protocol/` and duplicates meanwhile, and they enter the first baseline as debt.

Recommended and written into the tasks unless Emanuel says otherwise: the three engine files that compare a game value
take it as data in AR1.1 (Task 3); `Math/FloatConstants.h` and `Math/Angles.h` are a task of their own after AR1 Task 5
(Task 4); `CharacterClass` lives in `game/Kernel`, as the types review placed it; text indices stay `std::size_t`;
`game/Ui` adds `game/Sound` to its `may_use` for the widget kit's click sounds (one-way, as the `muret-module` skill
allows).

## Global Constraints

- Fidelity first. Naming a value changes one token: the comparison, its operator, its operand order, its cast and its
  `// 0x…` citation stay.
- **Replace initializers, not types.** A local constant that takes a home's name keeps its name and its declared type:
  `constexpr std::int32_t FirstGloves = Tables::FirstCodeOf(Tables::ItemGroup::Gloves);`. A warning that appears is
  fixed by the local's type, never by a new cast at the comparison.
- A constant is shared by value; a predicate only where the ASM of each site matches it. Two definitions with the same
  value and different meanings (`FirstEventMap` 18 and 24) are not merged.
- Values move only in design PRs (`refactor(ARn.m)` that is not a move, `refactor(items)`). A move PR
  (`check_pure_move.py`) and a comments PR (`check_comments_only.py`) never touch a value; a data citation travels with
  its definition.
- A new home is born in the folder it keeps inside its root, with that folder's namespace, its row in
  `Client/docs/research/coverage/module-map.tsv` and its glob in `Client/modules.tsv`; `check_layers.py` prints `0 new`.
- Home values are `inline constexpr` or enumerators; nothing with a dynamic initializer (CODING_RULES §5, the ADR for
  shared libraries).
- Tests keep the original's literals in their expectations; a home has a test that writes its values.
- Everything in English; a comment only if strictly necessary, keeping `0x…` citations. `ctest -R` names GoogleTest
  suites (`Suite.`). GitHub CI is the build and the final word.
- One task per PR, each after Emanuel's yes.

## Review Focus

- **A role swept into an identity.** `FirstEventMap` is 18 in some files and 24 in others; a script that replaces by
  name alone renumbers half of them. Task 2's registry matches on (space, value) and its replacements match name, type
  and value; Task 2 has a test with two roles of one name and two values.
- **One number in two spaces.** `0x1c15` is Rena's code and the Horn of Dinorant's model. The check keys on (space,
  value); Task 2 tests that the two definitions do not collide.
- **Small numbers that repeat by nature.** Subcode `0x01` exists under many opcodes. Subcodes are `home-only` and have
  no duplicate rule; Task 2 tests that two subcodes `0x01` in `Protocol/` pass.
- **A float that changes type.** `Tenth` is a `float` 28 times and a `double` widened from that `float` 78 times. Task
  4 gives each representation its own name and replaces a definition only when its declared type and its initializer
  give the home's value bit for bit in that type; the behavior golden runs in Debug and Release math on both systems.
- **A port PR that adds a duplicate while the refactor is half done.** The baseline only shrinks; Task 2 tests that a
  new duplicate fails and that a fixed row left in the baseline fails too.

---

## Order

| When | Task | PR |
| --- | --- | --- |
| Now | 1 the rule and the plan amendments | `docs(rules)` |
| Now, after Task 1 | 2 `check_named_values.py` | `ci(AR1.1)` |
| AR1.1, before AR1.2's move | 3 the engine stops naming game values | `refactor(AR1.1)` |
| AR1.1, after AR1 Task 5 | 4 `Math/FloatConstants.h`, `Math/Angles.h` | `refactor(AR1.1)` |
| AR2.1 | 5 one opcode list per server | `refactor(AR2.1)` |
| AR2.2 | 6 the table's name column and coverage | `refactor(AR2.2)` |
| AR3.1 | 7 `CharacterClass` | `refactor(AR3.1)` |
| AR4.1 to AR4.5 | 8 the homes of each milestone | each milestone's design PR |
| AR6, AR7 | 9 the sweeps and the check without a baseline | design |

The item plan's tasks run in their own order and register `Tables/ItemCode.h` with the check when its Task 9 lands.

## Tasks

### Task 1: the rule, and the plans that carry it

PR `docs(rules): give each space of named values one home`, branch `docs/rules-named-values`.

**Files:**
- Modify: `docs/CODING_RULES.md` (a subsection "Named values" after "Types and aliases" in §7; §3 "Nada hardcodeado"
  points to it)
- Modify: `docs/superpowers/plans/2026-11-20-client-architecture-refactor.md`: AR2.1 (the codes in one list per server;
  sizes apart from codes), AR2.2 (the name column, the coverage test), AR3.1 (`CharacterClass`),
  each AR4 milestone (its homes, Task 8's table), AR6 (the float sweep), AR7 (the check without a baseline)
- Modify: `docs/superpowers/plans/2026-11-22-ar1-engine-apart.md` (Tasks 3 and 4 of this plan as AR1.1 tasks 5b and 5c;
  "AR1 is done when" adds: no engine file defines a game value)
- Modify: `.claude/skills/muret-module/SKILL.md` (a line: a value of a shared space goes to its home, with a link to
  the rule)

The rule, as it goes into §7:

> ### Named values (2026-11-23)
>
> A value whose numbering is fixed outside the file that uses it (by the wire, a `Data\` file, a load order or an
> enumeration of the original) belongs to a space.
>
> 1. **One home per space, never one home for all.** Opcodes and subcodes in one list per server in `game/Protocol`;
>    packet layouts in `game/Protocol/<Module>/`; table rows and their keys (item codes, skills, shared text lines) in
>    `game/Tables`; maps in `game/Maps`; sounds in `game/Sound`; models and textures in `game/Assets`; the numbers the
>    x87 code shares in `engine/Math`. No `Constants.h`, `Offsets.h` or `Ids.h` across spaces.
>    `Client/tools/architecture/named-values.tsv` is the list.
> 2. **Identity once, role anywhere.** The home names what the value is (`Models::HornOfDinorant`). A file names what
>    it means there (`HeightOffsetPet`) with its own type and the home's name as its initializer, never the literal
>    again. One file may keep an identity private; the second file that needs it moves it home. A closed set, an
>    opcode and a group bound are born in their home.
> 3. **Two spaces, two names.** A number that means two things has a name in each space, and going from one to the
>    other is a named function (`ModelOf`).
> 4. **The site keeps its shape.** The comparison, its operator, its cast and its citation stay; the local name keeps
>    the type the literal had there. A predicate replaces a comparison only where the original's code at that address
>    matches it.
> 5. **A layout carries its offsets** (§14). An offset constant outside its layout is a missing accessor.
> 6. **What stays local:** one window's geometry and colors, one effect's parameters (also as float bit patterns), one
>    model's bones and actions, a sentinel beside its record. An address of the original is a comment, and from AR3.1
>    a row of `globals.tsv`.
> 7. **The engine names no game value**: it receives it as a parameter or in a table the game fills.
>
> `check_named_values.py` checks 1, 2 and 7. Existing code adopts these rules in the refactor's design PRs, never in a
> move or comments PR.

- [ ] **Step 1:** Write the subsection and the plan amendments above.
- [ ] **Step 2: Check** every file and line the amendments cite exists (`git grep` each), and the master plan's
  "Global Constraints" still read as before.
- [ ] **Step 3: Commit** `docs(rules): give each space of named values one home`; push; CI green (docs only).

### Task 2: `check_named_values.py`, a ratchet like `check_layers.py`

PR `ci(AR1.1): check that each named value has one home`, branch `refactor/ar1.1-named-values-check`. Skills:
`superpowers:test-driven-development`.

**Files:**
- Create: `Client/tools/architecture/check_named_values.py`, `Client/tools/architecture/test_check_named_values.py`
  (made-up trees with `scratch_repo.py`), `Client/tools/architecture/named-values.tsv`,
  `Client/tools/architecture/named-values-baseline.tsv`
- Modify: `.github/workflows/ci.yml` (a step in the `includes` job after "Client layers", `:194-200`)

**Interfaces:**
- Consumes: the comment and string stripping of `check_rounded_math.py`; the map and module reading of
  `check_layers.py` (import, do not copy).
- Produces:
  - CLI: `check_named_values.py --src Client/src --modules Client/modules.tsv --map
    Client/docs/research/coverage/module-map.tsv --spaces …/named-values.tsv --baseline …/named-values-baseline.tsv
    [--write-baseline]`; exit 0 with `N named values: N in the baseline, 0 new`, exit 1 listing each new row as
    `<file>:<line>: <space> <value> (<name>): <why>`.
  - `named-values.tsv`, columns `space`, `home`, `match`, `policy`, `note`. `match` is `name=<regex>` until the type
    exists, then `type=<regex>`, with an optional `values=<lo>..<hi>`. `policy` is `home-only` (a definition by
    literal outside the home fails), `single-definition` (a (space, value) defined by literal in two files fails, or
    anywhere once the home defines it) or `watch` (reported, never fails).
  - First rows: `opcode` and `subcode` (`game/Protocol`, `home-only`: the home exists today). The spaces whose home
    does not exist yet start as `watch`, so a port PR is never asked to use a header nobody has written: `map` (until
    `Maps/MapId.h`, AR1 Task 5), `item-code` and `item-model` (until the item plan's Task 9), `number` (until Task 4),
    `character-class` (until Task 7), `sound`, `model`, `texture`, `text` (`values=0..9999`, so a color named
    `WhiteText` is out), `skill`, `player-action` (until their AR4 milestone, Task 8). The PR that creates a home
    switches its row to `single-definition` or `home-only` and lowers the baseline in the same diff.
  - `named-values-baseline.tsv`, columns `space`, `file`, `name`, `value`: today's violations, about 1,100 rows
    (placement section 3.1). A row that no longer happens fails, so the file only shrinks.
  - A `watch` space prints its new rows as a report and exits 0.
  - An alias (an initializer that names another constant) never fails. A definition of any game space in a file whose
    module is under `engine/` fails with or without a baseline row once Task 3 lands (`--clean-root engine`, as
    `check_layers.py`).

- [ ] **Step 1: Write the failing tests:** `test_second_literal_definition_fails`,
  `test_alias_of_the_home_passes`, `test_home_only_outside_home_fails`,
  `test_same_number_in_two_spaces_passes` (`0x1c15` as a code and a model), `test_two_roles_one_name_two_values_pass`
  (`FirstEventMap` 18 and 24 in two files of the `map` space: two values, no duplicate),
  `test_subcodes_repeat_inside_protocol` (two `…Subcode = 0x01` in `Protocol/` pass),
  `test_values_bound_drops_a_color`, `test_comments_and_strings_are_ignored`, `test_stale_baseline_row_fails`,
  `test_write_baseline_round_trip`, `test_engine_file_defining_a_game_value_fails`,
  `test_watch_space_reports_and_passes`.
- [ ] **Step 2: Run** `python3 -I -m unittest discover -s Client/tools/architecture -p 'test_check_named_values.py' -v`;
  expected: every test fails on the missing module.
- [ ] **Step 3: Implement** the check; run the tests; expected: PASS.
- [ ] **Step 4: Write the baseline** with `--write-baseline` on the tree, read it (no row of `Protocol/` for opcodes;
  the `0x19` sounds, the maps 18, 23 and 30, the classes 2 and 4 present), and run the check without the flag:
  `… 0 new`.
- [ ] **Step 5:** Add the CI step; push; CI green. Commit `ci(AR1.1): check that each named value has one home`.

### Task 3: the engine stops naming game values (AR1.1)

PR `refactor(AR1.1): pass the game values the engine compared as data`, branch `refactor/ar1.1-engine-game-values`.
Before AR1.2's move.

Four engine sites compare a game value; the comparison stays in the original's order, and the value or the decision
arrives from the game:

| Site | Today | After |
| --- | --- | --- |
| `Render/Models/Cloth.cpp:60,517` | `LoginMap = 0x37`; `else if (map == LoginMap)` chooses the wind (`0x4551f4`) | the cloth update takes the map it compares as a field the game fills, or the login-map flag; the `rand()` call stays where it is |
| `Render/Models/Skinning.cpp:157-163` | `ShadowSpreadOf(map)` with `CastleSiegeMap = 0x1e` | `ShadowSpreadOf` moves to the game (`game/Actors`, beside its caller); the engine takes the spread |
| `Render/Models/Animation.cpp:23-24,92-95` | `FastWingAction = 0x1e`, `0x1f` in `WingKeysPerOriginalFrame(action)` (`0x50ba30`) | the function moves to the game beside the wings; the engine takes the keys per frame |
| `Audio/Music.cpp`: `WorldTracks`, `StopWorld`, `StepWorld` | the 22 track names of MU and the choice of track by map, with the map ids as bare literals (the check does not see them: they have no name) | PR 439 left this row open (the check sees neither strings nor bare literals). The [music plan](2026-11-25-music-home.md), Tasks 3 to 5, does it: the list becomes `Music::Track` in `game/Music` (layer 8), not `game/Maps` (layer 6 holds no behavior), and the two functions move there as `Music::StepTheMapMusic` and `Music::StopTheWorldMusic`, in the same order, over the engine's `Audio::MusicPlayer`. Crywolf's part left first: [Crywolf plan](2026-11-24-crywolf-homes.md), Task 2 (PR 447) |

The two FoxAnti opcodes (`Network/FoxAntiObfuscation.cpp:51,101`) are already AR1 Task 4's ("its MU values come
injected"). Each moved function keeps its tests; a test compares old and new over every map id (0..255) and action
(0..255). Done when `check_named_values.py --clean-root engine` lists only what other tasks own: the FoxAnti
opcodes (`Network/FoxAntiObfuscation.cpp:51,101`, AR1 Task 4) and `PlayerSkeletonModel`, `PlayerShadowModel` and
`FirstItemModel` in `Data/Models/PlayerModels.h` (the item plan's Task 10). The PR that removes the last of them adds
`--clean-root engine` to CI's "Client named values" step (#439 left it out for that reason).

### Task 4: the numbers the x87 code shares (AR1.1, after AR1 Task 5)

PR `refactor(AR1.1): give the shared float numbers one home`. Create `Client/src/Math/FloatConstants.h` and
`Client/src/Math/Angles.h` (`engine/Math`, namespace `Client::Math`), with numeric names only (`Tenth`, `Half`,
`Hundredth`, `FullTurn`, the degree-to-radian factors), **one name per representation**: the plain name is the number
in the type the original keeps it in (`Tenth`, a `float` from its bit pattern; `FullTurn`, the integer 360) and a
suffix names another type (`TenthAsDouble`, the `double` widened from that `float`; `FullTurnAsFloat`,
`FullTurnAsDouble`). The two "degrees to radians" that differ in the last bit stay two names: `DegreesToRadians`
(`0x3c8efa35`, the `float` nearest to π/180) and `DegreesToRadiansRoundedUp` (`0x3c8efa36`, the next one).

A script replaces a private definition only when its declared type and its initializer give the home's value bit for
bit in that type, whatever the spelling (`0.1f`, `std::bit_cast<float>(0x3dcccccdu)`, a file's own `Bits` helper);
the compiler checks every replaced shape with a `static_assert`, and the home's test repeats the decimal spellings. A
copy with the home's name and type becomes a using-declaration in the same place; any other keeps its name and its
type and takes the home's name as its initializer. Role names (`Fade`, `Growth`: 884 of 985) are not touched, nor a
numeric name with another value (`FullTurn` 720, `Third` for 0.3). Gate: the behavior golden in Debug and Release math
on Linux and Windows; the reference gives the same numbers. The registry's `number` space becomes `home-only`; what
stays in its baseline (the negatives, the original's π and the numeric names with another meaning) is AR6's sweep.

### Task 5: one opcode list per server (AR2.1)

Inside AR2.1's design PR. Create `Client/src/Protocol/Opcodes.h` (`game/Protocol`): every GameServer head code once,
named after the research's `04-protocolo.md` section that describes it, and its subcodes grouped under it; the
ConnectServer keeps `Protocol/Connection/Opcodes.h`. Numbers only: the sizes in `Protocol/Character/Opcodes.h:25-29`
go to their layouts (today that header rebuilds 294 of 684 `.cpp` through one include for `EquipmentSize`). The 22
values defined twice disappear; the FoxAnti layer and the XorRemap lab tables take their codes from it. Representation
as the types review F5 sets it (`Opcode`, `Subcode`, `PacketCode`, stored as one byte). A test lists every head code of
`0x4e2b10`'s switch and checks each is in the list once.

The binary bounds the list: `0x4e2b10`'s switch has 141 head codes and 138 nested cases (the research repo's
`dispatcher-0x4E2B10.md`), plus the codes the client only writes, each one byte, so it closes when the port does.
TrinityCore's `Opcodes.h` has 2,694 lines. Its cost is who includes it, the layouts and the table only, never its
length. If one head code's subcodes ever make the file hard to read, split it inside the same home:
`Protocol/Opcodes/<HeadCode>.h`, included by `Protocol/Opcodes.h`; the registry's home becomes that folder, and the
table's `static_assert` and the coverage test do not change.

### Task 6: the table's name and coverage (AR2.2)

Inside AR2.2's design PR, the TrinityCore pieces the master plan does not list yet: a `name` column
(`std::string_view`) for the line the `Dispatcher` already writes for an unknown code and for a packet log; every code
of `Protocol/Opcodes.h` in exactly one row, as a handler, an `Unported{va}` or a `SentOnly` row for codes the client
only writes (TrinityCore's `Handle_NULL` and `Handle_ServerSide`); a `when` column only if `0x4e2b10` checks the scene
before the handler (to verify there first). A test fails on a code with no row.

### Task 7: the character class (AR3.1)

`enum class CharacterClass : std::uint8_t { DarkWizard, DarkKnight, Elf, MagicGladiator, DarkLord }` and
`[[nodiscard]] constexpr CharacterClass BaseClassOf(std::uint8_t packed) noexcept` (the `& 7` of the original) in
`game/Kernel`, as the types review F1 places it. The 77 private definitions (`DarkLordClass` 12 times, `ElfClass` 17)
take it; where the original compares the unmasked register the raw integer stays.

### Task 8: the homes of each AR4 milestone

Each milestone's design PR creates the homes of the spaces its modules own, moves the values a second file needs, and
lowers the check's baseline for its files. It lists the literals it leaves and why.

| Milestone | Homes |
| --- | --- |
| AR4.1 `Kernel`, `Assets`, `Sound`, `Player`, `Actors` | `Sound/SoundIds.h` and `Audio::SoundId` (the 13 shared sounds; `0x19` once; the three WAVs the events loop as music, `0x8b`, `0x99` and `0x9e`, named once: [music plan](2026-11-25-music-home.md), "Music played as a sound"); `Assets/ModelIds.h` and `ModelId` (non-item models; `0x20a` once, as `PlayerSkeletonModel`); `PlayerBones` and `PlayerActions` beside the player catalog; `CellSize` in `Terrain` |
| AR4.2 `Monsters`, `Pets`, `Movement`, `Skills`, `Combat`, `Hero` | `Tables/SkillRecord.h` (the item plan's Task 2 for `Skill.bmd`: ≤10 offsets in 6 files) and `Tables/SkillId.h`; a monster's own bones stay with it |
| AR4.3 `Particles`, `Joints`, `Effects`, `WorldObjects`, `MapScripts` | the shared texture slots as `Render::TextureSlot` in `game/Assets` (`0x5ac` once); a `<Map>.h` per map script for its ids; effect type and subtype stay |
| AR4.4 `Items`, `Tooltips`, `Npc`, `Guild`… | `<Module>Texts.h` for lines two files of a module print; `Tables/TextIndex.h` (`Texts::`) for lines several modules print; the 145 lines passed by address become indices (`0x76bd1dc + n·300` → `n`) |
| AR4.5 `Ui`, `Hud`, `Events`, `CastleSiege`, `FrontEnd` | the kit's metrics and palette in `game/Ui`, `Render::PackedColor`; `<Event>.h` ids |

### Task 9: the sweeps (AR6) and the end of the baseline (AR7)

AR6: `MapId` becomes the opaque enum with the same names (planned); group predicates by ASM (item plan Task 15); the
sweep of numeric float constants left after Task 4; the registry switches its `match` from name suffixes to types.
AR7: `check_named_values.py` runs without a baseline for the `home-only` spaces, as `check_layers.py` does.

## Self-review

- Every kind of the census has a home and a task, or a reason to stay local (UI geometry, effect parameters, effect
  type and subtype, one model's bones, addresses as comments).
- The opcode decision follows the two projects Emanuel named (TrinityCore, cMaNGOS) and states what MuRet does not take
  from them.
- Every Review Focus line has its test in Task 2 or its gate in Task 4.
- Items are not planned twice: the item plan owns them, and this plan only registers its homes and asks it to keep
  declared types (done in #431).
- Tasks 5 to 9 are outlines on purpose: each lands inside a milestone's design PR, which gets its steps when it opens,
  as the master plan does.
