#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The account's characters, F3 00: the request and the list (04, "La selección de personaje (F3)").
namespace Client::Protocol::FrontEnd
{
// 0x634e0e, in the step where 0x634c60 enters scene 4: C1 04 F3 00, with the XOR32 and in clear.
[[nodiscard]] Outgoing WriteCharacterListRequest();

// An entry's equipment, which 0x5139f0 reads.
inline constexpr std::size_t EquipmentSize = 17;
// An entry of the list, as 0x4c20a0 reads it. Its [0xb] is not read.
inline constexpr std::size_t EntrySize = 0x22;

struct CharacterEntry
{
    std::uint8_t slot = 0;                            // [0], 0 to MaxCharacterSlot
    NameBytes name{};                                 // [1..10]
    std::uint16_t level = 0;                          // [0xc..0xd] (inferred: the level)
    std::uint8_t flags = 0;                           // [0xe], to +0x1d8: LockedCharacterFlag
    std::uint8_t classByte = 0;                       // [0xf]: CharacterClassOf
    std::array<std::byte, EquipmentSize> equipment{}; // [0x10..0x20], to 0x5139f0
    std::uint8_t guildStatus = 0;                     // [0x21], to +0x368 (inferred: the guild)
    // Not from the list: the create reply built it (0x4c2290 with 0x517270), with no equipment (Q-C103).
    bool withoutEquipment = false;
};

// +0x368 of a character without a guild: 0x511290 (0x511ca6) sets it on every character it builds, and the create reply
// keeps it.
inline constexpr std::uint8_t NoGuildStatus = 0xff;

// The highest slot of the selection: five, 0 to 4 (0x4c20a0).
inline constexpr std::uint8_t MaxCharacterSlot = 4;

// With it, entering the world shows notice 0x84 (0x6342c0).
inline constexpr std::uint8_t LockedCharacterFlag = 0x01;

[[nodiscard]] constexpr bool IsLocked(const CharacterEntry& entry)
{
    return (entry.flags & LockedCharacterFlag) != 0;
}

struct CharacterList
{
    std::uint8_t creatableClasses = 0;      // pkt[4], to 0x622b20: the create window reads it (inferred)
    std::uint8_t notice = 0;                // pkt[5]: the two numbers of text 1240
    std::vector<CharacterEntry> characters; // in the packet's order
};

// The count at pkt[6], then one entry per character from pkt[7]. TooShort if the count names more entries than the
// packet holds: 0x4c20a0 does not check the size and would read past it (a named difference). An entry whose slot is
// above MaxCharacterSlot is dropped: the original leaves it without a place, and it cannot be chosen (Q-C101).
[[nodiscard]] Core::Expected<CharacterList, Network::ProtocolError> ReadCharacterList(const Network::Packet& packet);

// An entry's class, from its [0xf]: ((b & 0x10) | (b >> 4)) >> 1, as 0x4c20a0 passes it to 0x517270. Bits 0 to 2 are
// the class (bits 5 to 7 of the byte) and bit 3 the second class (bit 4 of the byte).
[[nodiscard]] constexpr std::uint8_t CharacterClassOf(std::uint8_t classByte)
{
    return static_cast<std::uint8_t>(((classByte & 0x10) | (classByte >> 4)) >> 1);
}
} // namespace Client::Protocol::FrontEnd
