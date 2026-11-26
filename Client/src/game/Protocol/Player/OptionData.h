#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>

// The character's saved options, F3 30 (04, "El chat que llega"): 0x4e32c9 passes them to 0x4dd570, and 0x61c210
// saves them back with a C1 1C F3 30 the GameServer does not answer.
namespace Client::Protocol::Player
{
inline constexpr std::size_t SkillKeyCount = 10;
inline constexpr std::size_t QuickSlotCount = 3;

struct OptionData
{
    // pkt[4..0xd]: each key's skill number, 0xff without a skill.
    std::array<std::uint8_t, SkillKeyCount> skillKeys{};
    // pkt[0xe]: 0x01 the automatic attack, 0x04 the notices' sound and 0x10 the signs off.
    std::uint8_t flags = 0;
    // pkt[0xf..0x11]: Q, W and E, without the 0x1c00 that 0x4dd800 adds.
    std::array<std::uint8_t, QuickSlotCount> quickSlots{};
    // pkt[0x12]: the high nibble times 3 is the log's line count, the low one times 0.1 its transparency.
    std::uint8_t chatWindow = 0;
};

[[nodiscard]] Core::Expected<OptionData, Network::ProtocolError> ReadOptionData(const Network::Packet& packet);

// The bytes 0x61c210 builds on its stack (EBP - 0x4c .. EBP - 0x35), zeroed: [k] the skill of key k, [10] the flags,
// [11..13] Q, W and E and [14] the chat's log.
inline constexpr std::size_t OptionBodySize = 0x18;
using OptionBody = std::array<std::uint8_t, OptionBodySize>;

// 0x61c210: C1 1C F3 30 and `body`, with the XOR32 from byte 3 and in clear: it goes out with send (Ordinal_19), past
// the C3's encryption.
[[nodiscard]] Outgoing WriteOptionData(const OptionBody& body);
} // namespace Client::Protocol::Player
