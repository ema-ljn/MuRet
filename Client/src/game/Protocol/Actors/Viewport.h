#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterList.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Items/InventoryList.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

// The other players in the viewport (04, "Los otros jugadores en el viewport"): the 12 (0x4c8860), the 14 (0x4ca780),
// the 45 (0x4c92a0), the 25 (0x4c8380) and the DF (0x4c6b40), as the dispatcher 0x4e2b10 reads them, without the
// scene or encryption guards. The layouts are the 1.02 ASM's; the GS 0.97 builds others (08, P-C160).
namespace Client::Protocol::Actors
{
// 0x4c926c and 0x4c9773: how far each record of the 12 and the 45 advances.
inline constexpr std::size_t PlayerRecordSize = 0x2c;
inline constexpr std::size_t TransformedRecordSize = 0x1c;
// Bit 15 of a record's key: the one that just came in (the arrival effect instead of the path).
inline constexpr std::uint16_t NewBit = 0x8000;
inline constexpr std::uint16_t KeyMask = 0x7fff;

// A record of the 12, 0x2c bytes each from pkt[5]. Nobody reads +0x16, +0x17 and +0x29..+0x2b.
struct PlayerRecord
{
    // +0 and +1, big-endian, without bit 15.
    std::uint16_t key = 0;
    bool isNew = false;
    std::uint8_t x = 0; // +2
    std::uint8_t y = 0; // +3
    // +4: the low nibble is the state (1 to 4) and the high ones the class, ((b & 0x10) | (b >> 4)) >> 1 (0x4c89f3).
    std::uint8_t classAndState = 0;
    // +5..+0x15: the equipment 0x5139f0 reads (the character list's).
    std::array<std::byte, FrontEnd::EquipmentSize> equipment{};
    // +0x18, little-endian: the flags of 0x4e6380 and 0x42ab40.
    std::uint32_t effects = 0;
    // +0x1c..+0x25, without a terminator if it fills them.
    FrontEnd::NameBytes name{};
    // +0x26 and +0x27: where it goes (0x4eac00).
    std::uint8_t targetX = 0;
    std::uint8_t targetY = 0;
    // +0x28: the direction in the high nibble and the PK in the low one.
    std::uint8_t directionAndPk = 0;
};

// The C2 12: the count at pkt[4] and the records. `packet` is the whole packet, which the dispatcher keeps in the queue
// of 0x77f3ba0 with its size (0x4e372e..0x4e37b8).
struct Players
{
    std::vector<PlayerRecord> records;
    std::vector<std::byte> packet;
};

// The C1 14: the count at pkt[3] and the keys from pkt[4], two bytes each big-endian, with bit 15 (0x4ca7a2 compares it
// whole with 0x13d; 0x4ca7e1 takes it out for 0x510ff0).
struct Leave
{
    std::vector<std::uint16_t> keys;
};

// A record of the 45, 0x1c bytes each from pkt[5]. Nobody reads +6, +7 and +0x19..+0x1b.
struct TransformedRecord
{
    std::uint16_t key = 0;
    bool isNew = false;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    // +4 and +5, big-endian: the type for 0x5141e0.
    std::uint16_t type = 0;
    // +8, little-endian.
    std::uint32_t effects = 0;
    FrontEnd::NameBytes name{};      // +0xc..+0x15
    std::uint8_t targetX = 0;        // +0x16
    std::uint8_t targetY = 0;        // +0x17
    std::uint8_t directionAndPk = 0; // +0x18
};

// The C2 45: the count at pkt[4].
struct Transformed
{
    std::vector<TransformedRecord> records;
};

// The C1 25: the key at pkt[3..4] (big-endian, whole: 0x4c838e does not take out bit 15) and the item from pkt[5]. Of
// its seven bytes, 0x4c8380 reads byte 1 (the place high and the level low), byte 3 (the excellent, & 0x3f) and byte 4
// (the set), and 0x54b3a0 bytes 0, 3 and 5: nobody reads byte 6 (pkt[11]), which stays 0.
struct EquipmentShown
{
    std::uint16_t key = 0;
    std::array<std::byte, Items::InventoryItemDataSize> item{};
};

// The C1 DF: the whole key at pkt[3..4] and the cell at pkt[5] and pkt[6].
struct Position
{
    std::uint16_t key = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// The monsters and NPCs that come in (04, "Los monstruos y los NPC que entran"): the 13 (0x4c9c60), the 1F (0x4ca0b0)
// and the 68 (0x4e0980), three C2s with the count at pkt[4] and the records from pkt[5].
inline constexpr std::size_t MonsterRecordSize = 0x10;
inline constexpr std::size_t SummonRecordSize = 0x18;
inline constexpr std::size_t ObjectRecordSize = 0x1c;

// A record of the 13; the last byte 0x4c9c60 reads is +0xc.
struct MonsterRecord
{
    // +0 and +1, high first: the key without bit 15, and bit 15 (0x4c9cc8), the one just arrived.
    std::uint16_t key = 0;
    bool isNew = false;
    // +2 whole: bit 7 goes to +0x42d and bits 4..6 to +0x93; bits 0..1 are the type's high part.
    std::uint8_t typeHigh = 0;
    // (+2 & 3) << 8 | +3.
    std::uint16_t type = 0;
    // +4..+7, low first (0x4e6380).
    std::uint32_t effects = 0;
    std::uint8_t x = 0;       // +8
    std::uint8_t y = 0;       // +9
    std::uint8_t targetX = 0; // +0xa
    std::uint8_t targetY = 0; // +0xb
    // +0xc: the direction in the high nibble (nobody reads the low one).
    std::uint8_t direction = 0;
};

struct Monsters
{
    std::vector<MonsterRecord> records;
};

// A record of the 1F; the last byte 0x4ca0b0 reads is +0x16.
struct SummonRecord
{
    std::uint16_t key = 0;
    bool isNew = false;
    // +2 and +3, high first, the 16 bits: 0x98 to 0x9e go through 0x517400.
    std::uint16_t type = 0;
    std::uint32_t effects = 0; // +4..+7
    std::uint8_t x = 0;        // +8
    std::uint8_t y = 0;        // +9
    std::uint8_t targetX = 0;  // +0xa
    std::uint8_t targetY = 0;  // +0xb
    // +0xc: the direction high and the PK low.
    std::uint8_t directionAndPk = 0;
    // +0xd..+0x16: the owner's name, without a terminator if it fills them.
    FrontEnd::NameBytes owner{};
};

struct Summons
{
    std::vector<SummonRecord> records;
};

// A record of the 68: with `kind` 1, a player (0x20a) with the class of +1 and the equipment; with 2 or 3, the monster
// of type (+1 << 8 | +2); with another, nothing.
struct ObjectRecord
{
    std::uint8_t kind = 0;            // +0
    std::uint8_t classOrTypeHigh = 0; // +1
    std::uint8_t typeLow = 0;         // +2
    // +3 and +4, high first, without bit 15 (0x4e09b3).
    std::uint16_t key = 0;
    std::uint8_t x = 0; // +5
    std::uint8_t y = 0; // +6
    // +7..+0x17 (0x5139f0).
    std::array<std::byte, FrontEnd::EquipmentSize> equipment{};
    std::uint32_t effects = 0; // +0x18..+0x1b
};

struct Objects
{
    std::vector<ObjectRecord> records;
};

// The skill that hits several (PMSG_MAGICATTACK_COUNT_SEND, 0x4cedd0): the caster's whole key at pkt[3..4] (high
// first), the skill at pkt[5], the cell at pkt[6] and pkt[7] (which 0x4cedd0 does not read), the count at pkt[8] and
// the targets' keys from pkt[9], two bytes each, high first (0x4ceff6..0x4cf009).
struct MagicAttackCount
{
    std::uint16_t key = 0;
    // pkt[5]: what stays at the caster's +0x38e (0x4ceee4).
    std::uint8_t magic = 0;
    std::vector<std::uint16_t> targets;
};

// WrongPacket with another type or opcode; TooShort without a field the original reads (of the last record, up to the
// last byte it reads). Extra bytes are ignored.
[[nodiscard]] Core::Expected<Players, Network::ProtocolError> ReadPlayers(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Leave, Network::ProtocolError> ReadLeave(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Transformed, Network::ProtocolError> ReadTransformed(const Network::Packet& packet);
[[nodiscard]] Core::Expected<EquipmentShown, Network::ProtocolError> ReadEquipmentShown(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Position, Network::ProtocolError> ReadPosition(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Monsters, Network::ProtocolError> ReadMonsters(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Summons, Network::ProtocolError> ReadSummons(const Network::Packet& packet);
[[nodiscard]] Core::Expected<Objects, Network::ProtocolError> ReadObjects(const Network::Packet& packet);
// TooShort without the count or without the last key's last byte (pkt[8 + 2 · count]).
[[nodiscard]] Core::Expected<MagicAttackCount, Network::ProtocolError>
ReadMagicAttackCount(const Network::Packet& packet);
} // namespace Client::Protocol::Actors
