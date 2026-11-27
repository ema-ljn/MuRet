---
name: muret-packet
description: Use when the MuRet client has to read a new server packet, port an `Unported{}` row of the opcode tables, change how a row reads or handles its packet, or when a row's packet does not reach the world in a test (`routedAfter` empty, "no se puede leer", a session test with no effect).
---

# A packet the client reads

A packet touches four places: its layout and reader, its handler, its row in the opcode table, and its tests. Besides
them: the opcode's name, a module-map row for a new file, and the MuRet line of its section in
`Client/docs/research/04-protocolo.md`. Fidelity to `main.exe` and
[CODING_RULES](../../../docs/CODING_RULES.md) rank above this skill; where a file goes is the
[`muret-module`](../muret-module/SKILL.md) skill.

Before writing code, take from the research (`Client/docs/research/`, or the research repo, never invented): the case of
the dispatcher 0x4e2b10 that reaches the handler, the handler's address, the bytes it reads and their order, whether it
looks at the encryption flag, and what it writes when the packet is short.

## 1. The layout and the reader: `Client/src/game/Protocol/<Module>/`

The opcode has one name, in `Client/src/game/Protocol/Opcodes.h` (GameServer) or `Protocol/Session/Opcodes.h`
(ConnectServer), with its subcodes in the namespace of that name. A placeholder name (`Report29Opcode`) becomes the
name of what the packet is. The layout uses the name, never the literal.

```cpp
// Inventory.h: the 27, C1 08 27 (0x4d3820): the place at pkt[3], the mana at pkt[4..5] and, with 0xfe or 0xff, the
// BP at pkt[6..7], both high byte first.
struct ManaRefill { std::uint8_t place = 0; std::uint16_t mana = 0; std::uint16_t bp = 0; };
[[nodiscard]] Core::Expected<ManaRefill, Network::ProtocolError> ReadManaRefill(const Network::Packet& packet);

// Inventory.cpp, anonymous namespace: each field is its type and its offset; the minimum size is what the original
// reads, and the last member is the reader's address.
using RefillPlace = Network::Field<std::uint8_t, 3>;
using Mana = Network::Field<std::uint16_t, 4, std::endian::big>;
using Bp = Network::Field<std::uint16_t, 6, std::endian::big>;
constexpr Network::PacketLayout ManaRefillLayout{
    {Network::PacketType::C1, ManaOpcode, std::nullopt}, Mana::End, 0x4d3820};

// A read past the end records TooShort, which ReadPacket returns: read every field, check once.
return Network::ReadPacket(packet, ManaRefillLayout, [](Network::LayoutReader& reader) { ... });
```

## 2. The handler

Until each module's AR4 design gives it an `In/` folder, the handler is today's applier, next to its neighbors
(`Client/src/Scenes/InventoryMoves.h` for the 27). Its first parameter is the read value; each other parameter is a
part of the world that `Client/src/game/Session/Route.h` hands it by type. It returns `void` or
`Core::Expected<void, std::string>`; an error fails the session. A value wider than the field it lands in narrows once,
in the handler, with the operation the original does there (CODING_RULES §6).

| part | what it is |
| --- | --- |
| `Scenes::WorldState&`, `Core::Log&`, `Crt::CrtRandom&`, `const Tables::TextTable*` | the world, its log, the CRT, the texts |
| `World::LeafWeather&`, `Kernel::PlayerKey` | the weather; the player's key from the F1 00 |
| `const Scenes::ChatReceiveContext&`, `const Scenes::GuildReceiveContext&`, `const Scenes::CastleReceiveContext&`, `const Scenes::ActorCombatContext&` | a module's context, built from the world and the session |
| `const Scenes::ActionSender&`, `const Scenes::GamePacketSender&` | sending through the session's GameServer |
| `const Scenes::WorldEntryResources&` | the whole bag, only for an applier that already takes it |
| `App::ServerSession&` | only the session's own handlers (`Session/SessionHandlers.h`) |

A handler that needs something the list lacks gets a new `Detail::RoutedPart<T>` in `Route.h` (and `Route.cpp` if it
builds a context), not a parameter of the session. One that needs the session itself goes to
`Session/SessionHandlers.{h,cpp}`; a bag applier that sends through the session is
`&BoundToTheSession<Received, &Apply>`.

## 3. The row: `Client/src/game/Session/GameServerOpcodes.cpp` (or `ConnectServerOpcodes.cpp`)

```cpp
Row(ManaOpcode, std::nullopt, 0x4e3b88, "Mana",
    &Route<"se ignora el maná (27)", &Protocol::Items::ReadManaRefill, &Scenes::ApplyManaRefill>),
```

- The address is the case of 0x4e2b10 (`coverage/dispatcher-cases.tsv`), not the handler's. The rows go by opcode and
  then by subcode (`OpcodeTablesTest.TheRowsGoInTheOrderOfTheCases`); porting an `Unported{}` row replaces it in place,
  and only a new row changes the array's size.
- The warning names what is ignored and the code. Log messages go in English (CODING_RULES, "Todo lo del código y de
  Git va en inglés"), so a new row's is English (`ignoring the item value (29)`); the rows that came from the session
  keep their Spanish text word for word, because their tests pin it. The session appends `: no se puede leer` when the
  reader rejects the packet.
- `Route<What, &Read, &Handle, Encryption = AnyEncryption, Unread = Dropped>`:

| the original | the row |
| --- | --- |
| does not look at the encryption flag | `AnyEncryption` (the default) |
| in clear, answers with an F1 03 and does not read | `InClear<"<warning>", &Report>`, with `&Written<&Write, Args...>` to build the report |
| in clear, answers with an F1 03 that draws from the world's CRT | `AnsweredInClear<"…", &Answer>` |
| reads nothing, only checks the flag | `&OnlyTheEncryption<InClear<…>>` instead of `Route` |
| ignores a short packet | `Dropped` (the default) |
| writes something on all its paths, even short | a policy with `static void Unreadable(App::ServerSession&, const Network::Packet&, Network::ProtocolError, std::string_view what)` in `SessionHandlers`: it calls `session.IgnoreUnreadable(what)` and then `Deliver<&Apply>(session, value)` (`PickUpWhenShort`) |
| the dispatcher copies it to the ring of 0x77f27b4 | `CopiedToRing` as the row's last argument: only 0B, 12, 1C, 24 and 2A |

After the handler, the session flushes the chat rooms and then checks the result. Without a world (a test sink with no
`routedWorld`), the packet is read and dropped.

## 4. The tests

| file | tests |
| --- | --- |
| `Client/tests/protocol/`, the reader's file (`test_inventory_moves.cpp` for `Inventory.h`) | the exact packet's fields; `ExpectMinimumSize(&Read, exact)` (`support/MinimumSize.h`) in the file's `ReadsEachMinimumSizeAndNotOneByteLess`; another opcode is `WrongPacket` |
| `Client/tests/scenes/`, where the neighbors' appliers are tested | what the handler writes, and its error path with the address |
| `Client/tests/app/test_server_session.cpp` | the row: the packet reaches the world, and a short one leaves its warning |

The row's test needs a world, or the packet is dropped:

```cpp
Harness tested(IdentityObfuscation::Shared());
ASSERT_NO_FATAL_FAILURE(tested.ReachLoggedIn());
const auto world = std::make_unique<BareWorld>(); // on the heap: MSVC Debug's stack is 1 MB
tested.eventSink.routedWorld = &world->resources;
tested.socket.reads = {Bytes({0xc1, 8, 0x27, 0xff, 0x00, 0x5a, 0x00, 0x1e})};
ASSERT_TRUE(tested.session.Poll().HasValue());
EXPECT_EQ(tested.eventSink.routedAfter.size(), 1u);      // one per handled packet
EXPECT_EQ(world->world.OwnCharacter().mana, 0x5a);
EXPECT_EQ(tested.CountLogged(LogLevel::Warning, "no se puede leer"), 0u);
```

- A packet that must arrive encrypted goes through `FromTheGameServer(bytes, tested.gameServerSends)`.
- A handler that reads `Text.bmd` fails without the line: give it with
  `const auto texts = Client::Tests::MakeTextTable({{1506, "Un texto inventado"}}); world->resources.texts = &texts;`.
- `world->ReadyForTransitions()` gives map 0 and empty item tables, for a warp or a regen.
- Watch the row's test fail before the row exists (or with the row on `Unported{}`), then pass.

## Before pushing

```bash
python3 -I Client/docs/research/scripts/tools/dispatcher_cases.py check \
  --cases Client/docs/research/coverage/dispatcher-cases.tsv \
  --lists Client/src/game/Protocol/Opcodes.h Client/src/game/Protocol/Session/Opcodes.h \
  --tables Client/src/game/Session/GameServerOpcodes.cpp Client/src/game/Session/ConnectServerOpcodes.cpp
```

```bash
python3 -I Client/tools/architecture/check_layers.py --src Client/src --modules Client/modules.tsv \
  --map Client/docs/research/coverage/module-map.tsv --baseline Client/tools/architecture/layers-baseline.tsv \
  --clean-root engine
python3 -I Client/tools/architecture/check_named_values.py --src Client/src --modules Client/modules.tsv \
  --map Client/docs/research/coverage/module-map.tsv --spaces Client/tools/architecture/named-values.tsv \
  --baseline Client/tools/architecture/named-values-baseline.tsv --clean-root engine
```

A new file needs its row in `Client/docs/research/coverage/module-map.tsv`, or `check_layers.py` fails. Then
clang-format 18, the include cleaner, and the tests of `OpcodeTablesTest`, the reader's protocol suite, the applier's
suite and `ServerSessionTest`. Every address the change cites comes from the research.

## Common mistakes

| mistake | what happens |
| --- | --- |
| a session test without `routedWorld` | the packet is read and dropped: `routedAfter` stays empty and the world does not change |
| the handler's address in the row | `dispatcher_cases.py check` does not find the case |
| a second `…Opcode` constant outside `game/Protocol` | `check_named_values.py` reports it: the opcode's name lives in `Opcodes.h` |
| the session as a handler's parameter to send or log | use a part (`GamePacketSender`, `Core::Log&`) or a new one |
| a short packet that the original still applies, handled in the reader | the reader stays exact; the fallback is an `Unread` policy |
