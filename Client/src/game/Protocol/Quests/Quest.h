#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <cstdint>
#include <vector>

// The quests and the NPCs (04, "Las misiones y los NPC"): the 9F and A0..A3 that arrive, and the A0, A2 and 30 that
// leave. The original's handlers (0x4ddaa0, 0x4ddad0, 0x4ddb00, 0x4ddb40 and 0x4ddb80, from 0x4e2b10) read from the
// buffer without looking at the length; here a short packet is TooShort and the session ignores it with a warning, like
// the others.
namespace Client::Protocol::Quests
{
// C1 05 9F index value (0x4e4b3a → 0x4ddaa0): with an index of 0 to 3, the value goes to +6 + index of [0x710148].
struct QuestFlag
{
    std::uint8_t index = 0;
    std::uint8_t value = 0;
};

// C1 xx A0 count states... (0x4ddad0): `states` are the bytes the packet carries after the count, as they are.
struct QuestList
{
    std::uint8_t count = 0;
    std::vector<std::uint8_t> states;
};

// C1 05 A1 index state (0x4ddb00): the state is the whole byte of the four quests (the GS's GetQuestStateBYTE).
struct QuestStateChange
{
    std::uint8_t index = 0;
    std::uint8_t state = 0;
};

// C1 06 A2 index result state (0x4ddb40): the answer to the A2 the window sends; with result 0, like the A1.
struct SetQuestResult
{
    std::uint8_t index = 0;
    std::uint8_t result = 0;
    std::uint8_t state = 0;
};

// C1 07 A3 keyH keyL type value (0x4ddb80): a quest's prize to a character of the viewport.
inline constexpr std::uint8_t PointsPrize = 0xc8;
inline constexpr std::uint8_t ClassChangePrize = 0xc9;
inline constexpr std::uint8_t ClassPointsPrize = 0xca;
inline constexpr std::uint8_t EffectPrize = 0xcb;
struct QuestResult
{
    // (pkt[3] · 0x100 + pkt[4]) & 0x7fff, as 0x5110d0 looks it up.
    std::uint16_t key = 0;
    std::uint8_t kind = 0;
    std::uint8_t value = 0;
};

// Another type or opcode → WrongPacket; incomplete fields → TooShort. Extra bytes are ignored (except the A0's
// states, which are all kept).
[[nodiscard]] Core::Expected<QuestFlag, Network::ProtocolError> ReadQuestFlag(const Network::Packet& packet);
[[nodiscard]] Core::Expected<QuestList, Network::ProtocolError> ReadQuestList(const Network::Packet& packet);
[[nodiscard]] Core::Expected<QuestStateChange, Network::ProtocolError> ReadQuestState(const Network::Packet& packet);
[[nodiscard]] Core::Expected<SetQuestResult, Network::ProtocolError> ReadSetQuestResult(const Network::Packet& packet);
[[nodiscard]] Core::Expected<QuestResult, Network::ProtocolError> ReadQuestResult(const Network::Packet& packet);

// 0x63693a and 0x5676a6: C1 03 A0, with the XOR32 and Delivery::Encrypted (C3). The requester draws the padding's and
// the serial's rand().
[[nodiscard]] Outgoing WriteQuestListRequest();
// 0x419d13 and 0x41a35b: C1 05 A2 quest 1, with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteSetQuest(std::uint8_t quest, std::uint8_t state);
// 0x567c2d..: C1 05 30 keyH keyL (the NPC's +0x1f4, the high byte with SAR), with the XOR32 and as a C3.
[[nodiscard]] Outgoing WriteTalk(std::int16_t key);
} // namespace Client::Protocol::Quests
