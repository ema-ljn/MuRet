# Item vocabulary: one name and one home for each item fact

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** what the client knows about items (the `Item.bmd` row, the in-memory item, the item ids and group ranges, the
two id spaces, the model load lists, the model slot, the object state of `0x607b60`) gets one name and one home each,
without changing what the client computes.

**Architecture:** the analysis of 2026-11-23 found that MuRet stores items at the original's level (one model per
slot, attributes from the `Item.bmd` row, the group's shared "topology" as code by range) and that what it lacks is
vocabulary: offsets, literals and private constants re-declared file by file where `main.exe` had one struct and one
table. The plan names those facts in small refactor PRs, each behind a test that pins today's behavior, places the new
headers in the modules of the refactor, and leaves every comparison where the original has it. Two port differences
found while measuring are fixed first, in the port lane.

**Tech Stack:** C++20, GoogleTest, `ctest`, the behavior golden (`Client/tests/characterization`), the architecture
tools (`check_layers.py`, `move.py`).

**Spec:** the analysis "Análisis de la abstracción de ítems del cliente" (Spanish,
https://claude.ai/artifact/KTfBi1HkP1S9e7VWaTykCP) and its three reports (`identity.md`, `models.md`, `drawing.md`,
with the scripts that reproduce each number, in the project files under `cliente/abstraccion-items/`); the refactor
design [`2026-11-20-client-architecture-design.md`](../specs/2026-11-20-client-architecture-design.md); the master plan
[`2026-11-20-client-architecture-refactor.md`](2026-11-20-client-architecture-refactor.md); the types rule in
[`CODING_RULES.md`](../../CODING_RULES.md) §7 "Types and aliases"; the item draw plan
[`2026-11-23-item-glow-unification.md`](2026-11-23-item-glow-unification.md) (PR #428).

Measured on `refactor/client` at e1004fe (2026-11-23) against `main.exe` c32bbd5f in the research repo
(`client-research/mainexe-vmxp-c32bbd5f/decompiled/functions/`).

## What changes, row by row

| Fact | Original | MuRet today | After this plan | Tasks |
| --- | --- | --- | --- | --- |
| The `Item.bmd` row | one struct of 0x54 bytes | a raw span: 105 reads in 25 files through 48 private constants and 35 literals; +0x1e has five names in seven files | `Tables::ItemRecord`, one accessor per field, the offsets in one place | 2, 3 |
| The in-memory item (`0x54bf40`) | named fields | `World/ItemState.h` names them by offset (`byte0a`, `word0c`, `word12`) | the same fields with their meaning, the offset in the comment | 4 |
| Item ids and group ranges | a literal in each comparison | 364 private constants for 81 ids (`0x1c15`: 22 definitions, 10 names, 2 meanings), five of them misnamed | one taxonomy header (groups, codes, models, the most repeated ids); every comparison stays where it is | 1, 9, 12 |
| Type and model | two spaces of one integer | the same, with nothing to tell them apart | codes and models in two namespaces, a model written as `ModelOf(code)`; `ModelId` arrives in AR4 as the types rule says | 9, 12 |
| The load lists | 37 loops and 198 single calls | 562 literal rows in `Data/Models/PlayerModels.cpp` | the same 562 rows in the same order, written as about 245 loops and calls | 5, 6 |
| A model slot | one record of 0xe0 bytes | six containers; `RoleOf` parses the texture script per mesh per draw | one `LoadedModel` per slot, mesh traits computed at load, bone keys kept once | 11 |
| The object state (`0x607b60`) | one function | four files; the floor copy misses four scales | one function, the floor scales fixed first | 7, 13 |
| Draw and glow (`0x60b3d0`) | one function | two ports | the item draw plan, Tasks 4 to 8, unchanged; this plan adds two notes to its Task 7 | 14 |

The answer to the question that started this (do we repeat meshes or bones per item?) is no, and nothing here changes
how models are stored: weapons carry their own mesh and one-bone skeleton because the data does, and armor is skinned
with the wearer's bones as in the original. The only repeated bones are the animation keys kept twice per slot (Task
11).

## What stays as it is

- **Each site's own list.** When two copies of a rule differ in the original (`0x59cf6f` accepts the bows
  `0x800..0x806` and `0x811`; `0x59d878` also `0x814` and `0x815`), they differ in MuRet. A named predicate replaces a
  copy only where the ASM of that site matches it.
- **The representation the original fixes** (CODING_RULES §7, rule 6): ranges compared as signed 16-bit values
  (`CMP CX,0x1012` / `JL`), the signed or unsigned reading of the stored id where the port has it today, the width of
  each field.
- **The order of the texture calls**, which numbers the texture slots, and the single `rand()` before the first file.
- **The x87 operation order, the float constants as bit patterns, the number and order of `rand()` calls** (`0x607b60`:
  `0x220` one, `0xc19` one, `0xc17` two, `0xe20` two).
- **The two maps from item type to display model** (`0x608350` and `0x5e1480`): they differ on purpose.
- **No shared mesh or skeleton per item family**: the data has none and the original has none.

## Decisions for Emanuel

1. **Where the item taxonomy lives (Task 9).** Recommended: `game/Tables`, as `Tables/ItemCode.h` next to
   `ItemRecord`. The item code is the key of `Item.bmd` (16 sections of 512, `ItemTable.h:20-22`) and the index of
   `ItemRecordAt`; `Tables` is at layer 6, so every module that compares ids can include it (`game/Items`, at layer 9,
   cannot be included by `Actors` or `Player`); and `FirstItemModel`, `ItemModelsPerGroup` and `ItemModel()`
   (`PlayerModels.h:23-29`), which decision 2 also sends to `game/Tables`, move into the same header instead of being
   defined twice. The alternative is a module of its own, `game/ItemKinds` at layer 6: it reads well, but it cannot
   include `Tables` (same layer), and a new module is a new row in every list of the 58 modules (CODING_RULES §5, the
   design spec, ADR 0023, the `muret-module` skill) and in AR1.2's move list.
2. **Where MU's model catalog lives (Task 10), before AR1.2 moves the engine.** The map sends `PlayerModels`,
   `ModelLoadCalls`, `ModelTextureCalls` and `PlayerActions` to `engine/Data` and `Attachment` to `engine/Models`. Which
   file `main.exe` loads into which slot, and which bone of `Player.bmd` is the right hand, are MU content, as the
   packet layouts and the sound catalog are, and CODING_RULES §7 rule 5 says the engine never names a game id
   (`Attachment.h:37,46` names `0x1c13` and `0x1c30`). Recommended: retarget the four to `game/Tables` (layer 6,
   because `Scenes/WorldState.cpp` in `game/Kernel` includes `PlayerModels.h`) and `Attachment` to `game/Actors`. The
   only engine file that includes the catalog is `Render/Models/Skeleton.cpp:5-6`, for `MakePlayerSkeleton`, so that
   function leaves the engine first; `Attachment` has no engine user (`CharacterDrawing.cpp` and `GameHanging.cpp`
   use it). The alternative is to leave them in `engine/` for AR1.2 and move them again in AR4.1: about 75 include
   lines and a namespace rewritten twice.

Already decided elsewhere, so not asked again: no strong `ItemType` (types review A5, both readings of the stored id are
behavior) and `ModelId` as `Core::Id<ModelIdTag, std::uint16_t>` in AR4 (types review F2, CODING_RULES §7 rule 5);
custom items are out of this plan (the analysis proposes a separate appended list after AR7, if Emanuel wants them);
each task is its own PR and starts after Emanuel's yes.

## Global Constraints

- Fidelity first: a change is right when it does what `main.exe` does at the cited address. Every new block cites its
  `0x…`.
- A refactor PR (every task but 7 and 8) changes no golden and no test expectation. The tests it touches change only
  identifiers.
- A port PR (Tasks 7 and 8) rewrites only the behavior goldens its change explains and lists them, with the reason, as
  `Client/tests/characterization/README.md` asks.
- The representation is the one the port has today (CODING_RULES §7, rule 6). A conversion stays at the place where
  the cast is today; a new helper never moves a sign extension or a truncation.
- No file is renamed outside `move.py`. A new file is born in the folder it keeps inside its root (`Tables/`,
  `Assets/`), with that folder's namespace, its row in `Client/docs/research/coverage/module-map.tsv`
  (`confidence` `high`, `why` ending in `(item vocabulary)`) and the folder's glob in `Client/modules.tsv`
  `folders_today`. `check_layers.py` prints `0 new`, or the PR says which row it adds and why.
- Everything in English; a comment only if strictly necessary, one line, keeping the `0x…` citations. New code calls
  `RoundedMath.h`, never `std::sin` or glm trig.
- PRs target `refactor/client`, one task per PR; Emanuel says yes before the next one starts. No task runs while an AR
  move PR touches its files. Tasks 7 and 8 wait for #428, #429 and #430 to merge, because they touch the same files.
- GitHub CI is the build and the final word: the cloud container builds only the client without SDL and GL.
- `ctest -R` matches GoogleTest names (`Suite.Case`, from `gtest_discover_tests`), never the executable's name: each
  filter below names suites. "No tests were found" exits 0 and is a failure of the step, not a pass.

## Review Focus

- **An empty slot reaching a predicate.** The stored id `0xffff` is `-1` where the port reads it signed and `65535`
  where it reads it unsigned; a new name must not change which. Task 9 pins `SignedItemCode(0xffff) == -1` and changes
  no read of the stored id; Task 15 runs each group predicate over all 65,536 ids against the comparison of each site
  that adopts it.
- **Half-open against closed bounds.** The three-argument `InRange` means `[first, last]` in 18 files and
  `[first, end)` in 2. Task 1 renames the two half-open ones; Task 15 tests each group predicate at `first - 1`,
  `first`, `end - 1` and `end`.
- **The sign and byte order of the row.** `+0x40` is a signed 32-bit price and the words are little endian. Task 2
  pins a negative price and the low byte first.
- **The order of the load lists.** A fold that swaps two rows loads the same files in another order. Task 5 pins the
  562 rows in order before Task 6 touches them.
- **A port fix that reaches too far.** The floor scale changes for four models only and the Heart's level for one
  model only. Task 7 compares the floor scale with the shared function for every item type, and Task 8 draws the Heart
  at all 16 levels.

---

## Order

| When | Tasks | Lane |
| --- | --- | --- |
| Now, independent of AR1 | 1, 2, 3, 4, 5, 6 | refactor, scope `refactor(items)` |
| After #428, #429 and #430 merge | 7, 8 | port, scope `fix(items)` |
| After decision 2, before AR1.2's move (AR1 plan, Task 7) | 10 | design, scope `refactor(AR1.1)` |
| After decision 1 and Task 10 | 9 | refactor |
| AR4.1 and AR4.4 design PRs | 11, 12 | refactor, inside each milestone |
| AR6, after the item draw plan's Task 7 | 13, 14, 15 | refactor |

## Tasks

### Task 1: misnamed constants and range helpers get the names of what they hold

PR `refactor(items): name item constants and range helpers after what they hold`, branch
`refactor/items-honest-names`. Skill: `clean-code`.

**Files:**
- Modify: `Client/src/Scenes/HeroSkillsKnight.cpp:49-50,104,110`, `Client/src/Scenes/ItemTooltipOptions.cpp:52,57-61,319-323`,
  `Client/src/Scenes/HeroSkillCasts.cpp:65-66,259` and their uses, `Client/src/Scenes/ChaosMix.cpp:61,361,363`,
  `Client/src/Scenes/Skill55faa0.cpp:35` and `Client/src/Scenes/GameMovement.cpp:368` and their uses

**Interfaces:**
- Produces: the names below. Nothing outside these files uses them.

| File | Today | Becomes | Why (evidence) |
| --- | --- | --- | --- |
| `HeroSkillsKnight.cpp` | `FirstShield = 0xc12`, `ShieldsEnd = 0xe12`, the lambda `shield`, "un escudo" in the comment of `TheHandsAllow` | `FirstStaffModel`, `StaffModelsEnd`, `staff`, "a staff" | group 5 in model space, `0x581931..0x58194e` |
| `ItemTooltipOptions.cpp` | `FirstShield = 0xa00`, `ShieldsEnd = 0xc00` | `FirstStaff`, `StaffsEnd` | group 5 in type space, `0x5b7ae8..0x5b7af4` |
| `ItemTooltipOptions.cpp` | `WithBlocking{0x1f, 0x17, 0x15}`, `BlockingText = 79` | `MagicSwords`, `MagicDamageText` | swords 0:31, 0:23, 0:21 (`0x5b7af6..0x5b7b06`); text 79 is "Aumenta daño magico en %d%%", printed with `byte16`, the magic power |
| `ItemTooltipOptions.cpp` | `FirstHelm = 0x1400`, `HelmText = 93` | `FirstGloves`, `GlovesText` | group 10, `0x5b7a65` |
| `HeroSkillCasts.cpp` | `ThirdMount = 0x1c16`, `DarkHorse = 0x1c37`, "con el caballo" at `:259` | `DarkHorseModel`, `FenrirModel`, "with Fenrir" | `ModelOf` of 13:4 Dark Horse and 13:37 Horn of Doom (Fenrir); `:259` is `0x55bda6` `CMP [ESI+0x318],0x1c37` |
| `ChaosMix.cpp` | `FirstArmor = 0xc00` | `WeaponsEnd` | the code splits groups 0 to 5 from the rest |
| `Skill55faa0.cpp`, `GameMovement.cpp` | `InRange(value, first, end)`, half-open | `InHalfOpenRange` | the other 18 `InRange` are closed |

- [ ] **Step 1: Rename** each identifier in its file, and the comment words that name the wrong kind. Nothing else
  changes on those lines.
- [ ] **Step 2: Check the diff is names only:** `git diff -U0 | grep '^[-+][^-+]'` shows, per pair of lines, the same
  text with one identifier or comment word changed.
- [ ] **Step 3: Run the tests of those files:**
  `ctest --preset linux-x64-debug -R '^(HeroSkillsKnight(Helpers)?Test|ItemTooltipsTest|PersonalShopPriceTest|HeroSkillCastsTest|ChaosMix(KindForMode)?Test|MixQuoteTest|MixRoomTest|GameMovementTest|HeroArrivalSkillTest)\.'`
  (`HeroArrivalSkillTest` is the one that covers `Skill55faa0.cpp`). Expected: PASS, no test file changed. Push; CI green with the golden unchanged.
- [ ] **Step 4: Commit** `refactor(items): name item constants and range helpers after what they hold`.

### Task 2: `ItemRecord`, the `Item.bmd` row with names

PR `refactor(items): read the Item.bmd row through named accessors`, branch `refactor/items-item-record`. Skills:
`cpp-coding-standards`, `superpowers:test-driven-development`.

**Files:**
- Create: `Client/src/Tables/ItemRecord.h`, `Client/src/Tables/ItemRecord.cpp` (module `game/Tables`, namespace
  `Client::Tables`); their rows in `Client/docs/research/coverage/module-map.tsv`; `Tables/**` in the `folders_today`
  of `game/Tables` in `Client/modules.tsv`
- Create: `Client/tests/data/test_item_record.cpp`
- Modify: `Client/tests/data/test_golden_item_table.cpp`, `Client/tests/support/ItemFiles.h` (its three offset
  constants become `ItemRecordLayout` names; add `ItemTableWithRecord`)
- Modify: `Client/src/World/ItemState.cpp` (its 35 literal offsets and the private `Byte` and `Word` readers at
  `:27-35` go)

**Interfaces:**
- Consumes: `Data::Tables::ItemTable`, `ItemRecordSize`, `ItemCount` (`Data/Tables/ItemTable.h`).
- Produces, in `Tables/ItemRecord.h`:
  - `namespace ItemRecordLayout` with `inline constexpr std::size_t` offsets: `Name = 0x00` (`NameSize = 30`),
    `TwoHanded = 0x1e`, `Level = 0x20`, `EquipmentSlot = 0x22`, `Skill = 0x23`, `Width = 0x24`, `Height = 0x25`,
    `DamageMin = 0x26`, `DamageMax = 0x27`, `DefenseRate = 0x28`, `Defense = 0x29`, `MagicDefense = 0x2a`,
    `AttackSpeed = 0x2b`, `WalkSpeed = 0x2c`, `Durability = 0x2d`, `StaffDurability = 0x2e`, `MagicPower = 0x2f`,
    `RequiredStrength = 0x30`, `RequiredAgility = 0x32`, `RequiredEnergy = 0x34`, `RequiredVitality = 0x36`,
    `RequiredLeadership = 0x38`, `RequiredLevel = 0x3a`, `PriceValue = 0x3c`, `FixedPrice = 0x40`, `SetStatKind = 0x44`,
    `Classes = 0x45` (5 bytes), `Resistances = 0x4a` (7 bytes), and
    `static_assert(Resistances + 7 <= Data::Tables::ItemRecordSize)`.
  - `enum class ItemClassColumn : std::uint8_t { DarkWizard, DarkKnight, Elf, MagicGladiator, DarkLord };` (the
    order of +0x45..+0x49, `0x5af590`).
  - `class ItemRecord`: `explicit ItemRecord(std::span<const std::byte, Data::Tables::ItemRecordSize> bytes) noexcept;`
    and `[[nodiscard]] … const noexcept` accessors, each at the width of its field: `NameBytes()` →
    `std::span<const std::byte, 30>`; `TwoHanded()`, `EquipmentSlot()`, `Skill()`, `Width()`, `Height()`,
    `DamageMin()`, `DamageMax()`, `DefenseRate()`, `Defense()`, `MagicDefense()`, `AttackSpeed()`, `WalkSpeed()`,
    `Durability()`, `StaffDurability()`, `MagicPower()`, `PriceValue()`, `SetStatKind()` → `std::uint8_t`; `Level()`,
    `RequiredStrength()`, `RequiredAgility()`, `RequiredEnergy()`, `RequiredVitality()`, `RequiredLeadership()`,
    `RequiredLevel()` → `std::uint16_t` (little endian); `FixedPrice()` → `std::int32_t` (little endian, signed, as
    `0x54d0f0` reads it: `CDQ`, `JL`); `ClassRequirement(ItemClassColumn)` → `std::uint8_t`; `Resistances()` →
    `std::array<std::uint8_t, 7>`.
  - `[[nodiscard]] std::optional<ItemRecord> ItemRecordAt(const Data::Tables::ItemTable& table, std::size_t type) noexcept;`
    and the `const ItemTable&&` overload deleted, as `RecordTable::Record` does: the record points into the table.

- [ ] **Step 1: Write the failing tests** in `test_item_record.cpp`, with a table built by
  `ItemTableWithRecord(std::size_t type, const std::array<std::byte, 0x54>& plain)` (support, encrypts as
  `ItemTableOf` does):

```cpp
TEST(ItemRecordTest, ReadsEveryFieldAtItsOffset) // plain[i] = i for every i
{
    // …record = ItemRecordAt(table, 0x222).value()
    EXPECT_EQ(record.TwoHanded(), 0x1e);
    EXPECT_EQ(record.Level(), 0x2120);
    EXPECT_EQ(record.EquipmentSlot(), 0x22);
    EXPECT_EQ(record.Width(), 0x24);
    EXPECT_EQ(record.Height(), 0x25);
    EXPECT_EQ(record.AttackSpeed(), 0x2b);
    EXPECT_EQ(record.StaffDurability(), 0x2e);
    EXPECT_EQ(record.RequiredStrength(), 0x3130);
    EXPECT_EQ(record.RequiredLeadership(), 0x3938);
    EXPECT_EQ(record.RequiredLevel(), 0x3b3a);
    EXPECT_EQ(record.FixedPrice(), 0x43424140);
    EXPECT_EQ(record.ClassRequirement(ItemClassColumn::DarkLord), 0x49);
    EXPECT_EQ(record.Resistances(), (std::array<std::uint8_t, 7>{0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50}));
    // and one EXPECT per remaining accessor, each equal to its offset
}
TEST(ItemRecordTest, TheFixedPriceKeepsItsSign) // FF FF FF FF at +0x40 → -1
TEST(ItemRecordTest, ATypeOutsideTheTableHasNoRecord) // ItemCount → nullopt, ItemCount - 1 → a record
```

  And in `test_golden_item_table.cpp`, `TEST_F(GoldenItemTable, SwordOfDestructionAndBoneBladeDifferOnlyInTheirNumbers)`
  on the real file: types 16 and 22 give `TwoHanded` 0 and 0, `Level` 82 and 147, `Skill` 22 and 22, `Width` 1 and 1,
  `Height` 4 and 4, `DamageMin` 82 and 122, `DamageMax` 90 and 135, `AttackSpeed` 35 and 40, `Durability` 84 and 95,
  `RequiredStrength` 160 and 100, `RequiredAgility` 60 and 35, `ClassRequirement(DarkKnight)` 1 and 2,
  `ClassRequirement(MagicGladiator)` 1 and 0.
- [ ] **Step 2: Run them to see them fail:** `cmake --build --preset linux-x64-debug --target test_item_record` fails
  with `Tables/ItemRecord.h` not found.
- [ ] **Step 3: Implement `ItemRecord` and `ItemRecordAt`.** Words and the price are read byte by byte, low byte
  first, never by reinterpreting the bytes.
- [ ] **Step 4: Run** `ctest --preset linux-x64-debug -R '^(ItemRecordTest|GoldenItemTable|MemoryItemTable)\.'`. Expected:
  PASS.
- [ ] **Step 5: Adopt it in `World/ItemState.cpp` and `tests/support/ItemFiles.h`:** `CopyMetadata` and the
  requirement code read `ItemRecord` accessors; the support helper writes through `ItemRecordLayout`. Run
  `ctest --preset linux-x64-debug -R '^(ItemStateTest|ItemStateDeathTest|GoldenItemState)\.'` (expected: PASS, unchanged) and `check_layers.py` (expected:
  `0 new`; `game/Items` may use `game/Tables`).
- [ ] **Step 6: Commit** `refactor(items): read the Item.bmd row through named accessors`; push; CI green with the
  golden unchanged.

### Task 3: every other reader of the row goes through `ItemRecord`

PR `refactor(items): read Item.bmd rows only through ItemRecord`, branch `refactor/items-item-record-readers`.

**Files:**
- Modify the readers of the census (identity report, section 3.2): `Scenes/ActorActions.cpp`, `ActorCombat.cpp`,
  `ActorWalk.cpp`, `ItemPrice.cpp`, `ItemTooltipStats.cpp`, `UiItemDrawing.cpp`, `ItemHelpLevels.cpp`,
  `InventoryMoves.cpp`, `OwnItemPlacement.cpp`, `GameInventoryHud.cpp`, `ItemTooltips.cpp`, `OwnStatHelpers.cpp`,
  `HudItems.cpp`, `ItemTooltipRequirements.cpp`, `ItemNames.cpp`, `FloorItemNames.cpp`, `GameCharacterHud.cpp`,
  `GridItemUse.cpp`, `OwnDerivedStats.cpp`, `ItemTooltipOptions.cpp`, `ItemTooltipParts.cpp`, `GameHud.cpp`,
  `GameItemHelp.cpp`, `NpcWindowSteps.cpp`, `PetTrainer.cpp`, `ItemTooltipName.cpp` (`:351`, through
  `ItemRecordName(frame.record)`), and `UI/ChatCommands.cpp`; and the headers that carry the raw row:
  `ItemTooltipParts.h:29,35` (`frame.record`, `RecordByte`), `HudItems.h:67,72`, `ItemHelpLevels.h:62`

**Interfaces:**
- Consumes: Task 2's `ItemRecordAt` and accessors.
- Produces: no new name. The three file-local `ItemRecordOf` (`GameHud.cpp:221`, `OwnItemPlacement.cpp:107`,
  `InventoryMoves.cpp:291`) and the private byte and word readers (`ItemPrice.cpp:259-275`, `ItemHelpLevels.cpp:56-65`,
  `ItemTooltipOptions.cpp:182`, `ItemTooltipParts.cpp:46`) go. Each site keeps its own "missing row" message and its
  own reading of the byte (`!= 0`, `== 0`, added to a text index). `frame.record` and the header parameters become
  `ItemRecord`; `RecordByte` (`ItemTooltipParts.h`, used by the options, requirements and stats tooltips) goes with them. A site whose meaning differs from the field's name
  keeps a local name for it: `const auto prize = record->Durability();` in `ItemNames.cpp`.

- [ ] **Step 1: Replace** each `items…->Record(type)` and its offset constant with `ItemRecordAt` and the accessor.
- [ ] **Step 2: Check nothing reads the row by offset any more:**
  `grep -rnE 'items(->|\.)Record\(' Client/src` prints nothing, and
  `grep -rnwE 'TwoHandedAt|TwoHandedByte|TwoHands|WeaponMarkOffset|ItemLevelAt|ItemLevelWord|ItemEquipSlotOffset|ItemWidthOffset|ItemHeightOffset|HeightByte|DurabilityAt|StaffDurabilityAt|FixedPriceAt|PotionValueAt|ItemStatKindOffset|SetStatByte|FirstClassByte|MagicGladiatorByte|ItemDarkLordOffset|FirstResistanceByte|RequiredLevelOffset|BaseDurabilityOffset|EventTicketPrizeOffset|AgilityWord|AttackSpeedByte|DefenseByte|DefenseRateByte|DurabilityByte|EnergyWord|ItemNamedByte|LeadershipWord|MagicByte|MagicDurabilityByte|MaxDamageByte|MinDamageByte|RequiredLevelWord|StaffDurabilityByte|StrengthWord|VitalityWord|WalkSpeedByte|RecordByte' Client/src`
  prints nothing, and the constants `Width` and `Height` that held `0x24` and `0x25` are gone (`grep -rnE 'Width = 0x24|Height = 0x25' Client/src`).
- [ ] **Step 3: Run** `check_layers.py` (expected: `0 new`; every reader is in a `game` module at layer 7 or above)
  and push: CI runs the scene tests of all 26 files. Expected: green, the golden unchanged.
- [ ] **Step 4: Commit** `refactor(items): read Item.bmd rows only through ItemRecord`.

### Task 4: the in-memory item names its fields

PR `refactor(items): name the fields of the in-memory item after what 0x54bf40 puts in them`, branch
`refactor/items-item-state-names`.

**Files:**
- Modify: `Client/src/World/ItemState.h:20-56`, `World/ItemState.cpp`, and every file the compiler names in Step 2

**Interfaces:**
- Produces, in `World::ItemState`, each with its offset as the only comment (`// +0x0a`):

| Today | Becomes | Item offset | Source |
| --- | --- | --- | --- |
| `byte0a` | `twoHanded` | +0x0a | row +0x1e |
| `word0c`, `word0e` | `damageMin`, `damageMax` | +0x0c, +0x0e | row +0x26, +0x27 |
| `byte10` | `defenseRate` | +0x10 | row +0x28 |
| `word12` | `defense` | +0x12 | row +0x29 |
| `word14` | `magicDefense` | +0x14 | row +0x2a |
| `byte16` | `magicPower` | +0x16 | row +0x2f |
| `byte17` | `attackSpeed` | +0x17 | row +0x2b |
| `word18` | `walkSpeed` | +0x18 | row +0x2c |
| `word1e`, `word20`, `word22`, `word24`, `word26` | `requiredStrength`, `requiredAgility`, `requiredEnergy`, `requiredVitality`, `requiredLeadership` | +0x1e..+0x26 | row +0x30..+0x38 |
| `word28` | `requiredLevel` | +0x28 | row +0x3a |
| `itemClass` | `part` | +0x08 | `0x54cf08..0x54d053` |

  `id`, `scalar1Low`, `flags1b`, `flags1c` keep their names: their other bits are not verified. The comment "Nombres
  neutros…" goes, and `Classify` (`ItemState.cpp:603`), which fills `part`, becomes `PartOf`.

- [ ] **Step 1: Rename the members in `ItemState.h`.** Not by text replacement: `word0c`, `word0e` and `word12` are
  also members of the pet record in `Scenes/WorldRuntime.h:300-303` and `word12` of `Data/Tables/QuestTable.h:80`.
- [ ] **Step 2: Build** `ClientLib` and the test targets; fix each error at the line the compiler names.
- [ ] **Step 3: Check** `grep -rnwE 'byte0a|byte10|byte16|byte17|word14|word18|word1e|word20|word22|word24|word26|word28|itemClass' Client/src Client/tests`
  prints nothing, and the remaining `word0c`, `word0e`, `word12` are the pet record's and the quest table's.
- [ ] **Step 4: Run** `ctest --preset linux-x64-debug -R '^(ItemStateTest|ItemStateDeathTest|GoldenItemState)\.'` and push. Expected: PASS; CI green, the
  golden unchanged.
- [ ] **Step 5: Commit** `refactor(items): name the fields of the in-memory item after what 0x54bf40 puts in them`.

### Task 5: pin the three load lists

PR `test(items): pin the order of the three model load lists`, branch `refactor/items-pin-load-lists`.

**Files:**
- Modify: `Client/tests/data/test_player_models.cpp`

**Interfaces:**
- Produces: `TEST(PlayerModelsTest, TheThreeLoadListsKeepEveryRowInOrder)`, the safety net of Task 6.

- [ ] **Step 1: Write the test.** FNV-1a 64 (offset `0xcbf29ce484222325`, prime `0x100000001b3`) over each row in
  order: `model` as two bytes little endian, the folder's bytes, one `0`, the name's bytes, one `0`, `number` as four
  bytes little endian (`-1` is `FF FF FF FF`). Expected, computed at e1004fe from the literal rows:

```cpp
EXPECT_EQ(PlayerModelFiles().size(), 213u);
EXPECT_EQ(LoadListHash(PlayerModelFiles()), 0x3794d8bc0d507deaULL);
EXPECT_EQ(ItemModelFiles().size(), 243u);
EXPECT_EQ(LoadListHash(ItemModelFiles()), 0xab5f697a4af3ec7cULL);
EXPECT_EQ(SkillModelFiles().size(), 106u);
EXPECT_EQ(LoadListHash(SkillModelFiles()), 0x377accd992f48473ULL);
```

- [ ] **Step 2: Run it on the unchanged lists:** `ctest --preset linux-x64-debug -R '^PlayerModelsTest\.'`. Expected:
  PASS. A failure here means the test's encoding differs from the one above, not the lists.
- [ ] **Step 3: Check it bites:** swap two rows of `ItemFiles` locally; the test fails; undo.
- [ ] **Step 4: Commit** `test(items): pin the order of the three model load lists`.

### Task 6: the load lists say their loops

PR `refactor(items): write the model load lists as the loops of 0x60dbe0, 0x60ff50 and 0x615700`, branch
`refactor/items-load-loops`. Skills: `refactoring`, `cpp-coding-standards`.

**Files:**
- Modify: `Client/src/Data/Models/PlayerModels.cpp:14-593`, `Client/src/Data/Models/ModelLoadCalls.{h,cpp}`,
  `Client/tests/data/test_model_load_calls.cpp`

**Interfaces:**
- Produces, file-local in `PlayerModels.cpp`, a `constexpr` builder in the style of `CallList`
  (`ModelTextureCalls.cpp:27`):

```cpp
struct PartName { std::uint16_t firstModel; std::string_view name; };
template <std::size_t Size> struct FileList
{
    std::array<ModelFile, Size> files{};
    std::size_t count = 0;
    constexpr void Add(std::uint16_t model, std::string_view folder, std::string_view name, int number);
    // One loop of the original: `turns` calls, model and number one higher each turn.
    constexpr void AddFamily(std::uint16_t firstModel, std::string_view folder, std::string_view name, int firstNumber, int turns);
    // A loop that loads one file per part in each turn, part by part (the body loops of 0x60dbe0).
    constexpr void AddEach(std::initializer_list<PartName> parts, std::string_view folder, int firstNumber, int turns);
};
```

  `PlayerFiles`, `ItemFiles` and `SkillFiles` are namespace-scope `constexpr` variables built by `constexpr`
  functions, as `CallList` is; their counts 213, 243 and 106 are `static_assert`ed on those variables, as
  `ModelTextureCalls.cpp:134-137` does (inside the builder the list is not a constant expression). `firstModel + turn`
  is written with `static_cast<std::uint16_t>`. One entry per loop or single call, with its call VA as the trailing
  comment: the rows that share a VA comment today are one loop. Five loops branch inside their body and are written as
  consecutive entries that share the loop's VA, in the original's order, never with a skip parameter:
  `0x60ef49..0x60f072` (numbers 18 to 21: at 20 the five `*MaleTest` files, no helm at 21, `t_PantMale` at 19),
  `0x60f07d..0x60f105` (22 to 25, no helm at 24), `0x6113f7..0x611423` (`EventBloodCastle` 0 to 3: models `0x1c22`
  to `0x1c24`, then `0x2245`), `0x61169f..0x6116c8` (`Gem` 1 to 13 without 9) and `0x6116fb..0x61171a`
  (`SkillScroll.bmd` with number `-1` into four models, `0x1a27` to `0x1a2a`). `PlayerModelFiles()`, `ItemModelFiles()` and
  `SkillModelFiles()` keep their signatures. The folder strings are defined once, in `PlayerModels.cpp`.
  `SiegeModelFiles()` goes: it repeats 15 rows of `SkillFiles` (rows 41, 42, 55, 56, 58, 59 and 88 to 96,
  `0x6161ea..0x616854`) and only two tests of `test_model_load_calls.cpp` read it
  (`SiegeModelPathsAndNumbersFollowTheNativeBmdCalls`, `TheFinalSkillQueriesKeepTheFirstTextureFolderThroughTheNativeNameCache`);
  they check the same slots in `SkillModelFiles()`. `SiegeBeforeSkillTextureCalls` stays: `GameModels.cpp:155` uses it.

- [ ] **Step 1: Fold each list**, one loop at a time, running Task 5's test after each list.
- [ ] **Step 2: Run** `ctest --preset linux-x64-debug -R '^(PlayerModelsTest|ModelLoadCallsTest|ModelTextureCallsTest)\.'`.
  Expected: PASS, with Task 5's hashes unchanged. `ModelTextureCalls.cpp` is not touched.
- [ ] **Step 3: Push.** CI green, `test_golden_game_models` and the behavior golden unchanged.
- [ ] **Step 4: Commit** `refactor(items): write the model load lists as the loops of 0x60dbe0, 0x60ff50 and 0x615700`.

### Task 7: floor items take the four scales of `0x607b60` (port)

PR `fix(items): give floor items the scales 0x607b60 gives four models`, branch `fix/items-floor-scales`. Starts after
#430 merges.

**Files:**
- Modify: `Client/src/Render/Models/ObjectState.{h,cpp}`, `Client/src/Scenes/GameDrawTail.cpp:459-470` (its
  `EffectItemScale` goes), `Client/src/Scenes/FloorItemPackets.cpp:46-54,146-153`
- Test: `Client/tests/render/test_object_state.cpp`, `Client/tests/scenes/test_floor_items_scene.cpp`

**Interfaces:**
- Produces, in `Render/Models/ObjectState.h`: `[[nodiscard]] float ItemObjectScale(std::int16_t model) noexcept;`
  (`0x607beb..0x607bfd` and `0x607f14..0x607f52`), the body of today's `EffectItemScale`. `InitializeTheObject` and
  `InitializeEffectItemPrefix` call it; `ItemScale`, `SmallItemScale`, `FirstSmallModel` and `EndSmallModel` leave
  `FloorItemPackets.cpp`.

- [ ] **Step 1: Write the failing tests.** `TEST(ObjectStateTest, TheItemScaleOf0x607b60)`: `0x1a15` → 0.5f, `0x1e27` →
  0.5f, `0x1e13` → 1.0f, `0x2012` → 0.7f, `0x812` → 0.7f, `0xe11` → 0.7f, `0xe12` → 0.8f, `0x222` → 0.8f, `0x811` →
  0.8f. In the floor scene test, `ADroppedSmallHealingPotionKeepsScaleOne`: a created floor item of 14:1 has `scale`
  1.0f, and for every item type in `[0, 0x2000)` the created item's scale equals `ItemObjectScale` of the created
  item's model (after `0x608350`'s remaps).
- [ ] **Step 2: Run them to see them fail:** the floor test fails for `0x1a15`, `0x1e13`, `0x1e27` and `0x2012` with
  0.8.
- [ ] **Step 3: Implement** `ItemObjectScale` and point both callers at it.
- [ ] **Step 4: Run** `ctest --preset linux-x64-debug -R '^(ObjectStateTest|FloorItemsSceneTest|FloorItemNameTest|GameDrawTailTest)\.'`.
  Expected: PASS.
- [ ] **Step 5: Push.** If a behavior golden changes (a scene that drops one of the four), rewrite it and list it in
  the description with this reason.
- [ ] **Step 6: Commit** `fix(items): give floor items the scales 0x607b60 gives four models`.

### Task 8: the Heart glows at level 8 (port)

PR `fix(items): draw the Heart with the level 8 that 0x60b3d0 gives it`, branch `fix/items-heart-level`. Starts after
#430 merges.

**Files:**
- Modify: `ItemLevel` (`Client/src/Scenes/CharacterDrawing.cpp:1652` today, or `ItemDrawing.cpp` if the item draw
  plan's Task 7 moved it first)
- Test: `Client/tests/scenes/test_game_floor_item_drawing.cpp`

**Interfaces:**
- Produces: `0x1e1e` joins the level-8 rule of `ItemLevel`, as `0x60b5ea` (`CMP ECX,0x1e1e`, then `0x60b56b`
  `MOV EBX,0x8`) has it.

- [ ] **Step 1: Write the failing test** `TEST(GameFloorItemDrawingTest, AHeartGlowsAtLevelEightWhateverItsLevel)`:
  with the models and context of `HelpersOverrideThePackedItemLevelBeforeChoosingTheirPasses`, the floor item
  `0x1e1e` with `value = level << 3` adds 6 vertices for every level 0 to 15 (levels 0 and 2 are remapped to `0x223a`
  and `0x223b` first, which already glow at 8).
- [ ] **Step 2: Run it to see it fail:** today the Heart draws 3 vertices at levels 1, 3 to 6, 14 and 15, 9 at levels 9
  and 10, and 12 at levels 11 to 13.
- [ ] **Step 3: Add `model == 0x1e1e`** to the level-8 condition.
- [ ] **Step 4: Run**
  `ctest --preset linux-x64-debug -R '(^GameFloorItemDrawingTest|^GoldenGameFloorItemDrawing|^UiItemDrawingTest|ItemDrawEquivalence)\.'`.
  Expected: PASS. Push; list any behavior golden that changes.
- [ ] **Step 5: Commit** `fix(items): draw the Heart with the level 8 that 0x60b3d0 gives it`.

### Task 9: the item taxonomy

PR `refactor(items): give item groups, codes and models one home`, branch `refactor/items-taxonomy`. Starts after
decision 1 and Task 10: it renames `LordsCape` in `Attachment.h`, an engine file until Task 10 retargets it. The paths
below are for the recommended `game/Tables`.

**Files:**
- Create: `Client/src/Tables/ItemCode.h` (module `game/Tables`, namespace `Client::Tables`; code in `Client::Data`
  writes `Client::Tables::` in full until AR1 Task 9) and its row in the module map; the note of `game/Tables` in
  `Client/modules.tsv` and the layer-6 row of CODING_RULES §5 say the tables module also names item groups and ids
- Create: `Client/tests/gamelogic/test_item_code.cpp`
- Modify: `Client/src/Data/Models/PlayerModels.h:23-29` (`FirstItemModel`, `ItemModelsPerGroup` and `ItemModel()`
  move to `ItemCode.h`, unchanged; their users include it), the files that define a private constant for one of the ids
  named below (identity report, section 3.4: about 110 definitions) and the eight definitions of `0x212` (section 3.5)

**Interfaces:**
- Produces, in `Client::Tables`:
  - `enum class ItemGroup : std::uint8_t { Swords, Axes, Maces, Spears, Bows, Staffs, Shields, Helms, Armors, Pants,
    Gloves, Boots, Wings, Helpers, Potions, Scrolls };`
  - From `PlayerModels.h`, unchanged: `FirstItemModel = 0x212`, `ItemModelsPerGroup = 512`,
    `ItemModel(std::uint8_t group, std::uint16_t index)` (`0x60ff50`).
  - `[[nodiscard]] constexpr std::int16_t ItemCode(ItemGroup group, std::int32_t index) noexcept;` (group · 512 +
    index), `[[nodiscard]] constexpr std::int16_t SignedItemCode(std::uint16_t id) noexcept;` (the `MOVSX` of the
    stored id), `[[nodiscard]] constexpr std::int16_t ModelOf(std::int16_t code) noexcept;` (code + `FirstItemModel`).
    Each return is a `static_cast<std::int16_t>` and the group an explicit cast, as `ItemModel` does: CI builds with
    `-Wconversion` and `/W4 /WX`.
  - `namespace Codes` with `inline constexpr std::int16_t` names from `Item.bmd`: `HornOfUnilia` (13:2),
    `HornOfDinorant` (13:3), `DarkHorse` (13:4), `DarkRaven` (13:5), `CapeOfLord` (13:30), `HornOfDoom` (13:37),
    `WingsOfDarkness` (12:6), `Heart` (14:12), `Zen` (14:15), `RemedyOfLove` (14:20), `Rena` (14:21),
    `JewelOfCreation` (14:22), `ScrollOfTheEmperor` (14:23); and `namespace Models` with the same names as
    `ModelOf(Codes::…)`. So `0x1c15` is `Codes::Rena` or `Models::HornOfDinorant`, never both under one spelling.
  - Model-space values stay `std::int16_t` until AR4 brings `ModelId`; that PR converts `Models`. Group predicates are
    not in this task: no site adopts one here (Task 15).

- [ ] **Step 1: Write the failing tests:** `ItemCodeTest.TheExampleSwords` (`ItemCode(ItemGroup::Swords, 16) == 16`,
  `ModelOf(16) == 0x222`, `ModelOf(22) == 0x228`); `ItemCodeTest.TheTwoSpacesOf0x1c15` (`Codes::Rena == 0x1c15`,
  `Models::HornOfDinorant == 0x1c15`, `Codes::HornOfDinorant == 0x1a03`); `ItemCodeTest.ModelOfAgreesWithItemModel`
  (for every group and index `0..511`, `ModelOf(ItemCode(g, i))` read as `std::uint16_t` equals `ItemModel(g, i)`);
  `ItemCodeTest.AnEmptySlotReadsAsMinusOne` (`SignedItemCode(0xffff) == -1`, `SignedItemCode(0x7fff) == 0x7fff`).
- [ ] **Step 2: Run them to see them fail**, then implement `ItemCode.h`; run
  `ctest --preset linux-x64-debug -R '^(ItemCodeTest|PlayerModelsTest)\.'`. Expected: PASS.
- [ ] **Step 3: Replace the private definitions** of the named ids and of `0x212` with the header's names, leaving
  each comparison where it is, with its shape and its cast. A definition whose value is the id but whose meaning at
  its site is something else (`HeightOffsetPet`, `Counted1c14`) becomes a local alias of the header's name, such as
  `constexpr auto HeightOffsetPet = Models::HornOfDinorant;`, and keeps its name.
- [ ] **Step 4: Check** `check_layers.py` prints `0 new` (every user is in a module above layer 6, or in
  `game/Tables`; `Protocol/Events.h` keeps its literal, since `Protocol` is at layer 6 too) and push. CI green, the
  golden unchanged.
- [ ] **Step 5: Commit** `refactor(items): give item groups, codes and models one home`.

### Task 10: MU's model catalog leaves the engine before AR1.2 (if decision 2 says so)

PR `refactor(AR1.1): keep MU's model catalog out of the engine`, branch `refactor/ar1.1-model-catalog`.

**Files:**
- Create: `Client/src/Assets/PlayerSkeleton.{h,cpp}` (module `game/Assets`, namespace `Client::Assets`) with
  `MakePlayerSkeleton`, moved out of `Render/Models/Skeleton.cpp:76-98`; `Assets/**` in the `folders_today` of
  `game/Assets` in `Client/modules.tsv`; their rows in the module map
- Create: `Client/tests/gamelogic/test_player_skeleton.cpp` with the four `MakePlayerSkeleton` tests of
  `tests/render/test_skeleton.cpp:132-184` as `PlayerSkeletonTest` (AR1 Task 8 links the engine test folders to the
  engine libraries only)
- Modify: `Render/Models/Skeleton.{h,cpp}` (no `PlayerModels.h` or `PlayerActions.h` include), `Scenes/GameModels.cpp:118`,
  `tests/render/test_skeleton.cpp`, `tests/scenes/test_character_step.cpp`, `tests/scenes/test_golden_character_drawing.cpp`
- Modify: ten rows of `Client/docs/research/coverage/module-map.tsv`: `Data/Models/PlayerModels.*`,
  `ModelLoadCalls.*`, `ModelTextureCalls.*`, `PlayerActions.*` to `game/Tables`, `Render/Models/Attachment.*` to
  `game/Actors`, each with `confidence` `high` and a `why` ending in `(item vocabulary)`

- [ ] **Step 1:** Move the function, its tests and its includes; the callers include the new header.
- [ ] **Step 2: Check** `check_layers.py --clean-root engine` prints `0 new` and `grep -rn 'PlayerModels.h\|PlayerActions.h' Client/src/Render`
  prints nothing.
- [ ] **Step 3: Run**
  `ctest --preset linux-x64-debug -R '^(SkeletonTest|PlayerSkeletonTest|GoldenModelFiles|CharacterStepTest|GoldenCharacterStep|GoldenCharacterDrawing)\.'`;
  push; CI green, the golden unchanged.
- [ ] **Step 4: Commit** `refactor(AR1.1): keep MU's model catalog out of the engine`. AR1.2's `move.py` then takes the
  four catalog files to `game/Tables`; `Attachment` stays in `src/Render/Models/` until AR4 moves `game/Actors`, so it
  also moves once. Other engine files still name item models (`ObjectState.cpp:73`, `PassColors`, `ItemGlow`,
  `Animation.h:66`): those are the item draw plan's decision 4 and AR4.

### Task 11: one record per model slot (AR4.1, `Assets` design PR)

Detailed when AR4.1 opens. What it decides now:

- `LoadedModel { Data::Models::Model model; Render::Models::Skeleton skeleton; std::vector<Render::TextureSlot> meshTextures; std::vector<MeshTraits> meshes; }`,
  with `MeshTraits` (the mesh's texture role and its shine mark) computed once at load, as `0x4f13d0` parses the
  texture script and `0x6117fd..0x611847` marks the five meshes. `RoleOf` (`Scenes/CharacterDrawing.cpp:198-215`) reads
  them instead of parsing per draw. `GameModels`, `WorldModels` and `CreatePreview` share one table type instead of
  three trios of maps. `selections` and `unlitMeshes` stay mutable state outside the asset.
- `MakeSkeleton` takes the model by value and moves the key arrays: the keys are kept once (today 7.04 MB against
  4.40 MB). Its only reader of `Model::bones` is `MakeSkeleton` itself.
- The Player link bones get one home next to `PlayerHeadBone`: `PlayerRightHandBone = 33`, `PlayerLeftHandBone = 42`,
  `PlayerWingsBone = 47`, `PlayerPetBone = 34` (`0x511290`), used by `AttachmentOf`, `ActorConstruction` and
  `GameHanging`.
- Gate: `test_golden_game_models`, the behavior golden, and an equality test of the two root-height functions over
  every model id before one replaces the other.

### Task 12: the names reach every module (AR4.1, AR4.4, AR4.5 design PRs)

Each milestone's design PR replaces, in the files it moves, the loose item literals with Task 9's names and adopts
`ModelId` where the types plan puts it, as `MapId` does for maps. A literal changes only when it is an item id at that
site; the PR lists the ones it leaves and why.

### Task 13: `0x607b60` as one function (AR6, after the item draw plan's Task 7)

`InitialObjectState`, `HangingRandomCount`, the random counts of `InitializeEffectItem` and Task 7's
`ItemObjectScale` become one function that returns scale, blend mesh, hidden mesh, light, UV and the number of
`rand()` calls; the floor, the hanging and the effect items take what they need from it. Gate: the emulated comparison
of `0x607b60` over 8,800 ids (`check_muret_rules.py`) and the behavior golden.

### Task 14: two notes for the item draw plan's Task 7

Draw and glow are the item draw plan's (#428): Tasks 4 to 8 there are unchanged. Two findings of the drawing report
belong to its Task 7, which moves the item draw into `ItemDrawing.cpp`:

- The item draw has no module of its own: actors, monsters, floor, inventory and effects call it, and today that is
  five `game/Items → game/Actors` rows in the layer baseline. Its decision 4 (retarget `ItemGlow`, `PassColors` and
  `ObjectState`) should name that home.
- `DrawItemMeshPass` is the generic mesh renderer (`0x4ecc30`/`0x4ef0c0`) under an item name: 11 files call it, among
  them effects and monsters. It should not move into `ItemDrawing.cpp` under that name.

### Task 15: the rest of the deduplication (AR6)

- `PassColors.cpp` in the original's form: the armor set index for groups 7 to 11 plus the single models, 69 rows
  instead of 209, with an equality test against today's tables for every id below `0x2400` before they go.
- Group predicates `IsCodeIn(ItemGroup, std::int32_t)` and `IsModelIn(ItemGroup, std::int32_t)` in `Tables/ItemCode.h`
  (half-open over the group, on a value the caller has already read signed) and named kind predicates (`IsBowModel`,
  `IsCrossbowModel`…), one per distinct shape; a site adopts one only when its ASM matches, and the PR cites it. Each
  is tested against the comparison of every site that adopts it, over all 65,536 ids and at its edges.
- One `LevelOf` (`(x >> 3) & 15`, 8 definitions and 31 inline copies) and one excellent mask (`& 0x3f`, 7 definitions),
  and the 18 closed three-argument `InRange` helpers named `InClosedRange`. The other four `InRange` overloads
  (`CastleSiegeBlessing.cpp:19` and `CastleWeaponEffects.cpp:55` take distances, `CharacterDrawing.cpp:432` and
  `GameSocialSteps.cpp:63` are half-open over a count and a rectangle) are not item ranges and keep their names.

## Self-review

- Every row of the table at the top has a task: the row (2, 3), the in-memory item (4), ids and ranges (1, 9, 12,
  15), type and model (9, 12), the load lists (5, 6, 10), the model slot (11), the object state (7, 13), the draw
  (14 and the item draw plan).
- Names are consistent across tasks: `ItemRecordAt`, `ItemRecordLayout`, `ItemObjectScale`, `ItemCode`,
  `SignedItemCode`, `ModelOf`, `ItemModel`, `Codes::`, `Models::`, `IsCodeIn`, `IsModelIn`, `LoadedModel`, `MeshTraits`.
- Each Review Focus line has its test in the task that owns the code: Tasks 9 and 15 (empty slot), Tasks 1 and 15
  (bounds), Task 2 (sign and byte order), Task 5 (order), Tasks 7 and 8 (reach).
- Checked against the ASM on 2026-11-23 by three reviewers (Tasks 1 to 4, Tasks 5 to 8, modules and C++); their
  corrections are in: suite-name `ctest` filters, the five branching load loops, the reader list of Task 3, the
  taxonomy in `game/Tables`, Task 9 after Task 10, and the price fields' names.
- Tasks 11 to 15 are outlines on purpose: their files move in AR1.2 and AR4, and each gets its steps when its milestone
  opens, as the master plan does with AR1.
