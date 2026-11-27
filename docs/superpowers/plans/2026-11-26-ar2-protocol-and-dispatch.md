# AR2, protocol and dispatch: detailed plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** adding a packet touches three places: its layout, its handler and one table row. Every packet the client
reads goes through one reader. Every head code of `0x4e2b10` is a row of a table: a handler, a code MuRet has not
ported yet, or a code the client only writes. No byte of behavior changes.

**Architecture:** AR2.1 gives the engine a packet layout kit (`PacketCode`, `Field`, `PacketLayout`, a reader that
keeps the first error, `ReadList`). It also gives the game one opcode list per server, and moves `game/Protocol` to
`Client/src/game/Protocol/<Module>/`. Every reader is then rewritten on the kit. AR2.2 moves `game/Session` and
replaces the 129 `Dispatcher::On` registrations with two `constexpr` tables, one row per `case` of the original
dispatcher, routed by `Route<&Fn>`. It also splits `ServerSession` into the connection and the routing. AR2.3 turns the
rest of the handlers into free functions in `game/<Module>/In/` and the send functions into actions in `Out/`, and
deletes the event variant that carries every packet to `Scenes/` today.

**Tech Stack:** C++20 (NTTP enums, `constexpr` tables, concepts), CMake, GoogleTest, Python 3 (standard library,
`unittest`), GitHub Actions.

**Spec:** [`docs/superpowers/specs/2026-11-20-client-architecture-design.md`](../specs/2026-11-20-client-architecture-design.md),
the epic in the master plan
([`2026-11-20-client-architecture-refactor.md`](2026-11-20-client-architecture-refactor.md), "Epic AR2"), and Tasks 5
and 6 of the [named-values plan](2026-11-23-named-values.md), which run inside AR2.1 and AR2.2.

Measured on `main` at be4b8452 (2026-11-26), after AR1.4.

## Global Constraints

- No PR changes behavior. The behavior golden (`tests/characterization`) and every current test and golden must stay
  identical on Linux and Windows, in Debug and `math-release`. A difference is a refactor bug, not a golden to
  regenerate (`superpowers:systematic-debugging`).
- These do not change:
  - The order of the original's calls, of `rand()` calls, and of the five copies into the packet ring.
  - The bytes the client writes.
  - The first-time-only log line for an unknown code.
- Titles and branches:
  - Design PRs: `refactor(AR2.m): …` on `refactor/ar2.m-<topic>`.
  - Moves: `refactor(AR2.m): move <module> to Client/src/game`.
  - Never `CLn` in a scope.
  - CI runs `check_pure_move.py` on every `type(ARn.m)` PR, so a design PR never renames a file. Before pushing, check
    that `git diff -M50% --name-status` shows no `R` line.
- Everything in English. A comment only when strictly necessary, on one line; keep the `// 0x…` citations. A cut keeps
  `not ported` or `MuRet does not …` on the line that cites the address (the reference reads one line at a time).
- New code calls `RoundedMath.h` / `RoundedTransforms.h`, never `std::sin` or glm trig (`check_rounded_math.py`).
- New scripts: standard library only, `python3 -I`, tests with `unittest`.
- PRs target `main` and may be stacked. Nothing merges without Emanuel's yes (2026-11-26).
- Priority: fidelity, then `docs/CODING_RULES.md`, then `clean-code`, `clean-architecture` and `refactoring`.
- The cloud container builds the client only with the stub recipe and no SDL; GitHub CI is the build and the final word.

## Review Focus

- **A packet longer than what the original reads, or shorter in a field it never reads.** Some readers today stop
  before the end, and the original ignores the rest. A layout's `minimumSize` must be what the original's reader reads,
  never the struct's full size. Each reader task adds, per layout, a test with a packet of exactly `minimumSize` bytes
  and one with one byte less.
- **`F3` and `F4` subcodes.** They sit at `pkt[3]` in a C1 and at `pkt[4]` in a C2 (04, "El dispatcher"). An `F1` that
  arrives as a C2 reads `pkt[4]` in MuRet; the original reads `pkt[3]`. That is a documented divergence and stays.
  Task 1 pins both offsets in `PacketCode`'s tests.
- **`0x00` is the greeting on the ConnectServer and the chat on the GameServer.** Two lists and two tables. Task 11
  tests that a ConnectServer code never routes into the GameServer table.
- **The five copies into the packet ring** (`0x0B`, `0x12`, `0x1C`, `0x24` and `0x2A`). They happen before the handler,
  even for a packet that cannot be read or decrypted. When they become a table column (Task 11), a test sends a
  malformed `0x24` and checks that it is still copied.
- **The handlers gated by the XOR-remap flag** (`0x19`, `0x29`, `0x1E`, `0x73`, `0x30`, `0x36` and `0x1C`). They keep
  today's behavior exactly. A refactor does not resolve P-C33 (08), the open contradiction for `0x1C`. Task 11 keeps
  each one's current test and adds the flag as a column, with no new behavior.

---

## How this plan places files

- **A file changes folder only in a move PR**, made by `move.py` from its row in
  `Client/docs/research/coverage/module-map.tsv`. In a design PR, a file whose module changes gets its `target` cell
  changed, not a `git mv`.
- **A new file is born in the folder it will keep.** For example, `engine/Network/Field.h`,
  `game/Session/GameServerOpcodes.cpp` and `game/Guild/In/GuildList.cpp`. The new file takes that folder's namespace
  (`Client::<path>`, CODING_RULES "Namespaces") and gets its map row in the same PR (`confidence` `high`, `why` ending
  in `(AR2.m)`).
- **`game/Protocol` is organized by module.** Each layout and writer lives in `game/Protocol/<Module>/`, named after
  the module of `Client/modules.tsv` whose state its handler touches. Task 3 fixes the mapping. The
  ConnectServer's packets go to `game/Protocol/Session/`.

## Deviations from the master plan, and why

| master plan | this plan | why |
| --- | --- | --- |
| 11 copies of `FieldsOf` | 13 (`Skill`, `Trade`, `NpcWindows`, `FloorItems`, `Viewport`, `Quest`, `Combat`, `Party`, `Inventory`, `Messages`, `Duel`, `Pet/PetOrders`, `Friends`) | measured at be4b8452; two were added after the master plan |
| the types review puts `Opcode`, `Subcode` and `PacketCode` in `Protocol/PacketCode.h` | `engine/Network/PacketCode.h` | `PacketLayout` lives in the engine (Emanuel, 2026-11-23) and holds a `PacketCode`. The engine cannot include the game |
| game modules move in AR4 | `game/Protocol` moves in AR2.1 (Task 4) and `game/Session` in AR2.2 (Task 10) | AR2 rewrites every file of both modules: moving them first puts the rewrite at its final path, and `git blame` skips only the move |
| a comments PR after each move | the reader and handler PRs translate the comments of what they rewrite | they rewrite almost every line; a comments PR would translate text the next PR deletes |
| `Route<&Fn>` deduces the parts of the `GameContext` (AR2.2) and AR3.1 can start with AR2.1 | AR3.1's `GameContext` lands before Task 12 | a handler can only receive state that something owns; today it reaches `Scenes/` only through `ISessionEventSink` |

## AR2.1 (design, move): layouts and the opcode list

### Task 1: the engine's packet layout kit

PR `refactor(AR2.1): read packets through a layout`, branch `refactor/ar2.1-layouts`. Skills: `cpp-pro`,
`clean-code`, `superpowers:test-driven-development`.

**Files:**
- Create in `Client/src/engine/Network/` (module `engine/Network`):
  - `PacketCode.h`
  - `Field.h`
  - `PacketLayout.h`
  - `LayoutReader.{h,cpp}`
  - `ReadList.h`
- Modify:
  - `engine/Network/ProtocolError.h`: add `ProtocolResult`.
  - `engine/Network/PacketFields.{h,cpp}`: `FieldsAfterSubcode` stays for the 19 files that call it, until Tasks
    6 to 9 move them to `LayoutReader`; the last of them deletes it.
  - The seven readers of `Protocol/Connection/`, as the first users.
- Test:
  - `Client/tests/network/test_packet_code.cpp`
  - `test_layout_reader.cpp`
  - `test_read_list.cpp`

**Interfaces (produces):**

```cpp
namespace Client::Network
{
enum class Opcode : std::uint8_t {};
enum class Subcode : std::uint8_t {};
struct PacketCode { PacketType type; Opcode opcode; std::optional<Subcode> subcode; };
[[nodiscard]] constexpr std::size_t SubcodeOffset(PacketType type); // 3 in a C1/C3, 4 in a C2/C4
template <typename T> using ProtocolResult = Core::Expected<T, ProtocolError>;
// T is std::uint8_t, std::uint16_t, std::uint32_t or std::array<std::byte, N>. At counts from pkt[0], as 04 cites it.
template <FieldType T, std::size_t At, std::endian Order = std::endian::little> struct Field;
// reader: the original reader's address.
struct PacketLayout { PacketCode code; std::size_t minimumSize; std::uint32_t reader; };
// Open(packet, layout) -> ProtocolResult<LayoutReader>; Read<Field>() -> T; Bytes(offset, count) -> a view;
// Finish(value) -> ProtocolResult<V>.
class LayoutReader;
// Open, then read(reader), then Finish: what a reader calls.
template <typename Read> auto ReadPacket(const Packet& packet, const PacketLayout& layout, Read read)
    -> ProtocolResult<std::invoke_result_t<Read&, LayoutReader&>>;
// The records start at `first` and measure Size; readOne gets each as std::span<const std::byte, Size>.
template <std::size_t Size, typename ReadOne>
auto ReadList(LayoutReader& reader, std::size_t first, std::size_t count, ReadOne readOne) -> std::vector<Record>;
}
```

`LayoutReader::Read` and `Bytes` past the end record `TooShort` once and return `T{}` or an empty view; `ReadList`
records it when `count` records do not fit, before reading any. `Finish` returns that first error, or the value.
A reader is one `ReadPacket` call and tests no field.

- [ ] **Step 1: Write the failing tests:**
  - `PacketCode`: `SubcodeOffset(C1) == 3` and `SubcodeOffset(C2) == 4`.
  - `LayoutReader`:
    - A wrong type, opcode or subcode gives `WrongPacket`.
    - A packet one byte shorter than `minimumSize` gives `TooShort`.
    - Reading past the end, then reading more, still gives the first error.
    - A big-endian `u16` at `+4` reads `0x1234` from `12 34`.
  - `ReadList`: a count larger than the bytes give `TooShort`.
- [ ] **Step 2: Implement the kit.**
  - Templates header-only.
  - `LayoutReader`'s out-of-line members `ENGINE_API`.
  - `ProtocolResult` next to `ProtocolError`.
- [ ] **Step 3: Rewrite the readers of `Protocol/Connection/`** (`Hello`, `Keepalive`, `Login`, `Logout`, `MainCheck`,
  `MapServerMove` and `ServerList`) on `PacketLayout` constants. Their tests in `tests/protocol/` stay as they are.
- [ ] **Step 4: Check.** Run `check_layers.py --clean-root engine` (expect `0 new`), then the tool tests, then push and
  let CI build.

### Task 2: one opcode list per server (named-values Task 5)

PR `refactor(AR2.1): one opcode list per server`, branch `refactor/ar2.1-opcodes`. Skills: `cpp-pro`, `muret-module`.

**Files:**
- Create:
  - `Client/src/Protocol/Opcodes.h` (module `game/Protocol`): every GameServer head code once, named after its
    `04-protocolo.md` section, with its subcodes under it.
  - `Client/docs/research/coverage/dispatcher-cases.tsv`: the head codes and nested cases of `0x4e2b10`, generated from
    the research repo's `dispatcher-0x4E2B10.md`. Columns: `level`, `code`, `parent`, `handler`.
- Modify:
  - The 31 files that define the 175 `*Opcode` and 85 `*Subcode` constants. Each becomes a name of the list, or a role
    initialized from it (CODING_RULES §7).
  - `Protocol/Connection/Opcodes.h`: the ConnectServer list, typed.
  - `Protocol/Character/Opcodes.h`: `NameSize`, `EquipmentSize` and `EntrySize` go to their layouts. 38 files include
    it today.
  - `App/LabXorRemapTables.h`: takes its codes from the lists.
  - `Protocol/Dispatcher.{h,cpp}`: `On(Network::Opcode, …)` and `On(Network::Opcode, Network::Subcode, …)`.
  - `Client/tools/architecture/named-values.tsv`: the `opcode` space's home.
- Test: `Client/tests/protocol/test_opcodes.cpp`.

**Interfaces:**
- Consumes: `Network::Opcode`, `Network::Subcode` (Task 1).
- Produces: `Client::Protocol::Opcodes`, with `inline constexpr Network::Opcode CharacterOpcode{0xf3};` and, under each
  head code, `namespace Character { inline constexpr Network::Subcode InventoryList{0x10}; }`.

- [ ] **Step 1: Generate `dispatcher-cases.tsv`** with a script in `Client/docs/research/scripts/tools/`, with its
  `unittest`. The script reads the research's three maps: 245 opcode slots (140 distinct handlers, 104 to the
  default), the 19-entry level-2 map and the 211-entry level-3 map.
- [ ] **Step 2: Write the failing test.** Each `level 1` code of the TSV that has a handler appears exactly once in
  `Protocol/Opcodes.h`. The list holds no code the TSV lacks, except the ones the client only writes, which are marked
  as such.
- [ ] **Step 3: Write the list and replace the constants.** The 19 values defined in more than one file go. The sizes
  go to the layouts that read them. Run `check_named_values.py --clean-root engine`: the baseline only shrinks.
- [ ] **Step 4: Check.** `grep -rn "constexpr std::uint8_t \w*Opcode" Client/src` prints only the lists. Then push.

### Task 3: the protocol's folders, by module

PR `refactor(AR2.1): give each packet its module`, branch `refactor/ar2.1-protocol-modules`. Skills: `muret-module`.
It changes data only: the map and `modules.tsv`.

**Files:** Modify `module-map.tsv` (the 94 `game/Protocol` rows) and `Client/modules.tsv` (`game/Session`'s
`folders_today`).

- [ ] **Step 1: Set each row's `target` to `game/Protocol/<Module>`.** Use the module whose state its handler touches.
  Rows by module:

  | module | files today |
  | --- | --- |
  | `Session` | `Connection/*`, `BuildConstants.h` |
  | `FrontEnd` | `Character/CharacterCreate`, `CharacterDelete`, `CharacterList`, `CharacterName`, `CharacterSelect`, `CharacterSelectResult.h` |
  | `Player` | `Character/LevelUpPoint`, `PkLevel`, `OptionData` |
  | `Items` | `Inventory`, `FloorItems`, `Character/InventoryList` |
  | `Chat` | `Chat/ChatSend`, `Messages` |
  | `Movement` | `Movement`, `World` |
  | `Actors` | `Viewport` |
  | `Combat` | `Combat` |
  | `Skills` | `Skill` |
  | `Pets` | `Pet/*` |
  | `Social` | `Trade`, `Duel` |
  | `Party` | `Party` |
  | `PersonalShop` | `PersonalShop` |
  | `Npc` | `NpcWindows`, `DialogAnswers` |
  | `ChaosMix` | `JewelMix` |
  | `Quests` | `Quest` |
  | `Messenger` | `Friends`, `FriendChat`, `Letters` |
  | `Guild` | `Guild` |
  | `Events` | `Events` |
  | `CastleSiege` | `CastleSiege`, `CastleBattle`, `SiegeControls` |
  | root of `game/Protocol` | `Outgoing`, `Opcodes.h` |
  | `game/Session` (not Protocol) | `Dispatcher` |

  A file whose packets belong to two modules (`World`: teleport and regen, but also weather) stays whole in this
  task. Its reader PR splits it, creating the new file in the other module's folder.
- [ ] **Step 2: Check.** Run `check_layers.py` on the new targets (expect `0 new`) and `python3 -I -m unittest
  discover -s Client/tools/architecture -p 'test_*.py'`.

### Task 4: the move

PR `refactor(AR2.1): move the protocol to Client/src/game`, branch `refactor/ar2.1-protocol-move`. Skill:
`muret-move`.

- [ ] **Step 1: Run `move.py --modules game/Protocol`.** Do by hand only what it prints.
- [ ] **Step 2: Run the checks:**
  - `check_pure_move.py --base <base> --head HEAD`
  - `check_layers.py`
  - `git diff -M50% --diff-filter=M` (only include lines and paths)

  Then push.
- [ ] **Step 3: After the squash, record it** with `move.py --record <hash>` (in the next PR of the stack).

### Task 5: the namespaces follow the folders

PR `refactor(AR2.1): name the protocol's namespaces after their folders`, branch `refactor/ar2.1-protocol-namespaces`.

- [ ] **Step 1: Rename each moved file's namespace** to `Client::Protocol::<Module>`, for example
  `Protocol::Inventory` and `Protocol::FloorItems` → `Protocol::Items`. Use the libclang method of the AR1 namespaces
  (references, using-declarations, `PrintTo` helpers by hand).
- [ ] **Step 2: Fix the collisions.** Two old namespaces that become one can declare the same name. Rename the later
  one after the packet it reads (the compiler lists each collision).
- [ ] **Step 3: Check.** `grep -rn "namespace Client::Protocol::\(Inventory\|FloorItems\|Messages\|World\|Viewport\)"
  Client/src` prints nothing, then push.

### Tasks 6 to 9: every reader on the kit

One PR per group, each `refactor(AR2.1): read the <group> packets through layouts`, branch
`refactor/ar2.1-layouts-<group>`. Skills: `clean-code`, `refactoring` (Substitute Algorithm only where the bytes read
are the same), `superpowers:test-driven-development`.

| task | group | files today (lines with header) |
| --- | --- | --- |
| 6 | front end, player, chat, movement, combat | `Character/*` (760), `Chat/ChatSend` (77), `Messages` (195), `Movement` (256), `World` (176), `Combat` (161), `OptionData` (102) |
| 7 | items, trade, NPCs | `Inventory` (309), `FloorItems` (334), `Trade` (313), `Duel` (180), `Party` (240), `PersonalShop` (474), `NpcWindows` (450), `JewelMix` (43), `DialogAnswers` (45) |
| 8 | the world around the hero | `Viewport` (582), `Skill` (471), `Pet/*` (214), `Quest` (197), `Friends` (249), `FriendChat` (407), `Letters` (292) |
| 9 | guild, events, the castle | `Guild` (796), `Events` (809), `CastleSiege` (985), `CastleBattle` (590), `SiegeControls` (48) |

For each file:

- [ ] **Step 1: Pin first.** If a reader has no test for the short packet and the exact packet, add both, against
  today's code, and see them pass.
- [ ] **Step 2: Rewrite it.** Give each packet a `PacketLayout` constant with the original reader's address and the
  `minimumSize` it reads. Its fields become `Field` reads. Remove the local `FieldsOf`, the `Fields` class
  (`Events.cpp`, `CastleSiege.cpp`) and each `if (!fields.HasValue())`. The three list reads of `CastleSiege` become
  `ReadList`. Writers keep their bytes and take their codes from `Protocol/Opcodes.h`. In Task 6, the five `Name` and
  two `CharacterName` aliases become one `NameBytes` (`std::array<std::byte, NameSize>`) beside the character name's
  layout (types review).
- [ ] **Step 3: Translate and trim the comments** of what the PR rewrote (CODING_RULES §9).
- [ ] **Step 4: Check.** Run the group's tests and `tests/protocol`, then push. CI runs the golden.

After Task 9: `grep -rn "FieldsOf\|class Fields\|fields.HasValue()" Client/src/game/Protocol` prints nothing.

## AR2.2 (move, design): the opcode table and routing

### Task 10: move the session

PR `refactor(AR2.2): move the session to Client/src/game`, branch `refactor/ar2.2-session-move`. Skill: `muret-move`.
It moves `App/ServerSession.{h,cpp}`, `App/WorldEvent.h`, `App/CharacterEvent.h`, `App/ISessionEventSink.h`,
`Scenes/SessionEventApplier.{h,cpp}` and `Protocol/Dispatcher.{h,cpp}` (map target `game/Session`).

- [ ] **Step 1: Run `move.py --modules game/Session`**, the same checks as Task 4, and push. Record it after the
  squash.

### Task 11: the two tables (named-values Task 6)

PR `refactor(AR2.2): route packets through the opcode tables`, branch `refactor/ar2.2-opcode-tables`. Skills:
`cpp-pro`, `clean-architecture`.

**Files:**
- Create in `Client/src/game/Session/`:
  - `OpcodeTable.h`: the row type.
  - `GameServerOpcodes.cpp`
  - `ConnectServerOpcodes.cpp`
- Modify:
  - `Dispatcher.{h,cpp}`: it reads a table instead of `On()`.
  - `ServerSession.cpp`: its 129 `On()` registrations become rows.
- Test:
  - `Client/tests/session/test_opcode_tables.cpp`
  - the current `tests/app/test_server_session*.cpp`, unchanged

**Interfaces (produces):**

```cpp
namespace Client::Session
{
struct Unported { std::uint32_t address; };   // a case of 0x4e2b10 MuRet does not handle yet
struct SentOnly {};                            // a code the client only writes
using RowHandler = void (*)(ServerSession& session, const Network::Packet& packet); // constexpr, unlike std::function
struct OpcodeRow
{
    Network::PacketCode code;
    std::uint32_t address;                     // the case's handler in 0x4e2b10 or its sub-dispatchers
    std::string_view name;                     // for the unknown-code line and a packet log
    bool copiesToRing = false;                 // 0x0B, 0x12, 0x1C, 0x24, 0x2A: copied before the handler
    bool needsUninterceptedFlag = false;       // 0x19, 0x29, 0x1E, 0x73, 0x30, 0x36, 0x1C (P-C33 stays open)
    std::variant<RowHandler, Unported, SentOnly> action;
};
[[nodiscard]] std::span<const OpcodeRow> GameServerOpcodes();
[[nodiscard]] std::span<const OpcodeRow> ConnectServerOpcodes();
}
```

A `when` column is added only if `0x4e2b10` checks the scene before the handler, verified there first.

- [ ] **Step 1: Write the failing tests:**
  - No two rows share a `PacketCode`. A `static_assert` checks this too.
  - Every code of `Protocol/Opcodes.h` is in exactly one row.
  - Every case of `dispatcher-cases.tsv` is a row.
  - A ConnectServer `0x00` never reaches the GameServer table.
  - A malformed `0x24` is still copied to the ring.
- [ ] **Step 2: Write the rows** from today's registrations, in the original's case order. A row calls today's member
  handler through a captureless lambda (`[](ServerSession& s, const Network::Packet& p) { s.OnWalk(p); }`). The five
  `CopyToPacketRing` calls and the flag checks move from the handlers to the columns. The handlers stay where they
  are.
- [ ] **Step 3: Check.** Run `tests/app`, `tests/session` and `tests/protocol`, then push. CI runs the golden.

### Task 12: `Route<&Fn>` and the session split

PR `refactor(AR2.2): split the session and route the first modules`, branch `refactor/ar2.2-route`. Skills:
`clean-architecture`, `refactoring` (Extract Class, Move Function). It needs AR3.1's `Kernel::GameContext`.

**Files:**
- Create in `game/Session/`:
  - `Route.h`
  - `Connection.{h,cpp}`: keepalive, serial, keys, and the login and server-change flow.
- Create in `game/<Module>/In/`: one handler per packet of four modules: login (`game/FrontEnd`), selection
  (`game/FrontEnd`), movement (`game/Movement`) and the viewport (`game/Actors`).
- Modify: `ServerSession.{h,cpp}`, which keeps the routing.

**Interfaces:**
- Consumes: `Kernel::GameContext` (AR3.1) and `OpcodeRow` (Task 11).
- Produces: `template <auto Fn> void Route(ServerSession& session, const Network::Packet& packet)`, so `&Route<&Fn>`
  is a `RowHandler`. It deduces from `Fn`'s signature the layout to read (its first parameter's `Layout` member) and the
  parts of `session.Context()` (the `Kernel::GameContext`) to pass. A handler is
  `void OnWalk(const Protocol::Movement::Walk&, Movement::MovementState&, Actors::ActorPool&)`, and receives neither the
  session nor the packet.

- [ ] **Step 1: Write a failing `Route` test** with a fake context and a handler that records its arguments.
- [ ] **Step 2: Extract `Connection`.** `ServerSession` shrinks to the routing and the send queue.
- [ ] **Step 3: Rewrite the four modules' handlers as `In/` functions** and make their rows `Route<&Fn>`. Their
  `WorldEvent` alternatives and their `SessionEventApplier` cases go.
- [ ] **Step 4: Check** the four modules' tests and `tests/app`, then push.

### Task 13: the `muret-packet` skill

PR `docs(AR2.2): the muret-packet skill`, branch `docs/ar2.2-muret-packet`. Skill: `superpowers:writing-skills`.

- [ ] **Step 1: Write `.claude/skills/muret-packet/SKILL.md`**, with the login packet as the example:
  1. The layout in `game/Protocol/<Module>/`.
  2. The handler in `game/<Module>/In/`.
  3. The row in the table.
  4. The tests: exact packet, short packet, table coverage.

  Add its line to `AGENTS.md`.

## AR2.3 (design): every handler in its module

### Tasks 14 to 17: the handlers and the actions, by group

One PR per group, the same groups as Tasks 6 to 9 minus what Task 12 did. Each is `refactor(AR2.3): move the <group>
handlers to their modules`, branch `refactor/ar2.3-handlers-<group>`. Skills: `clean-code`, `refactoring`.

- [ ] **Step 1: Rewrite each handler** as a 40 to 80-line function in `game/<Module>/In/`, routed by `Route<&Fn>`. Its
  `WorldEvent` alternative and its `SessionEventApplier` case go.
- [ ] **Step 2: Make each send function an action** in `game/<Module>/Out/`. There are 40 public `Send…` and
  `Request…` functions in `ServerSession.h` today. Each action validates against the module's state and writes its
  packet through a `Kernel` port.
- [ ] **Step 3: Split `tests/app/test_server_session.cpp`** (5,136 lines) per handler, with the same input bytes and
  the same assertions.
- [ ] **Step 4: Check** the group's tests, then push. CI runs the golden.

### Task 18: what carried every packet goes

PR `refactor(AR2.3): drop the session's event variant`, branch `refactor/ar2.3-no-events`.

- [ ] **Step 1: Delete** `WorldEvent.h`, `CharacterEvent.h`, `ISessionEventSink.h`, `SessionEventApplier.{h,cpp}`
  and `/bigobj` on `ClientLib` (`Client/CMakeLists.txt`). Then check that the MSVC Debug job passes without
  `/bigobj`.
- [ ] **Step 2: Check.** `grep -rn "WorldEvent\|ISessionEventSink\|SessionEventApplier" Client/src Client/tests`
  prints nothing, then push.

## AR2 is done when

- A new packet touches its layout, its handler and one table row, and nothing else. The `muret-packet` skill's example
  proves it.
- `dispatcher-cases.tsv` and both tables agree, and `test_opcode_tables` fails on a code with no row.
- `game/Protocol` has no `FieldsOf`, no `Fields` class and no hand-written `TooShort` per field.
- The behavior golden is identical, and `check_layers.py --clean-root engine` and `check_named_values.py --clean-root
  engine` print `0 new`.
- The last PR's description answers the master plan's question: does the repetition left in the layouts justify a code
  generator (CODING_RULES §10)?

## Where AR2 ended (2026-11-27)

- Done: Tasks 1 to 11 on `main`; Task 12's first half (`Route`, #521) and Tasks 13 to 17 (#522 to #529), stacked.
- Moved to AR4 by Emanuel: Task 12's second half, the session split ("Dejarla para AR4"), and Task 18 ("La 18 con
  AR4, y su plan"). What the sink still carries is the output of the login, the selection and the server change, which
  stay in the session until `FrontEnd`'s design: Tasks 39 and 40 of the
  [AR4 plan](2026-11-27-ar4-scenes-into-modules.md).
- Steps 2 and 3 of Tasks 14 to 17 (the `In/` and `Out/` files and the test split) go with each module's design in
  AR4 (Emanuel, 2026-11-27, "Rutear a los aplicadores de hoy").
- **A code generator: no.** The protocol's 102 layouts are 293 `Field` aliases and one `PacketLayout` constant, and its
  133 readers are one `ReadPacket` call each: every offset is written once, next to its reader's address. A generator
  would add an input format and a build step to review for the same lines (CODING_RULES §10).

## Decisions

Emanuel took the three as this plan writes them (2026-11-26).

1. **The protocol's folders.**
   - By the module of `modules.tsv`, as the spec says (`Protocol/Items/`, with `Inventory` and `FloorItems` together).
     The namespaces follow (Task 5). This plan does that.
   - Or one folder per today's namespace (`Protocol/Inventory/`, `Protocol/FloorItems/`), with no namespace rename.
2. **AR3.1's `GameContext` before AR2.2's Task 12.** `Route<&Fn>` needs something that owns the state it passes.
3. **Moving `game/Protocol` and `game/Session` inside AR2**, not in AR4.
