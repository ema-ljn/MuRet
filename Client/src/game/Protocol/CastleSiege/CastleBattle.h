#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

// The castle's battle (04, "Castle Siege"): what 0x4e2b10 dispatches with the B9 that arrives (0x4e58b0, table
// 0x4e61f8: 01 → 0x4e0750, 02 → 0x4e0790, 03 → 0x4e07f0, 05 → 0x4e0860; 04 and the rest to the default), the BA
// (0x4e5929 → 0x4cf1b0), the BB (0x4e57f5 → 0x4e0f30), the B7 (0x4e580d) and the battle's B2 (0x4e5099), applying
// nothing. The lab's GameServer 0.97 has no Castle Siege: each byte comes from the client's ASM, *static*.
//
// As in CastleSiege.h, the subcode of the B9, the B7 and the B2 is pkt[3] in a C1 and pkt[4] in the others, and then
// each handler reads at fixed offsets from the first byte, without looking at the type or the length. What the case
// reads and did not arrive is TooShort; extra bytes are ignored.
namespace Client::Protocol::CastleSiege
{
inline constexpr std::array<Network::Subcode, 4> BattleSubcodes{
    CastleInfo::NpcLevelSubcode, CastleInfo::OwnerMarkSubcode, CastleInfo::HuntZoneInfoSubcode,
    CastleInfo::HuntZoneEntrySubcode};
inline constexpr std::array<Network::Subcode, 3> WeaponSubcodes{Network::Subcode{0}, Network::Subcode{1},
                                                                Network::Subcode{2}};
// The B2 20, in the B2's table 0x4e6140 (CastleSiege.h).
inline constexpr std::array<Network::Subcode, 12> CastleSubcodes{
    Network::Subcode{0x11}, Network::Subcode{0x12}, Network::Subcode{0x13}, Network::Subcode{0x14},
    Network::Subcode{0x15}, Network::Subcode{0x16}, Network::Subcode{0x17}, Network::Subcode{0x18},
    Network::Subcode{0x19}, Network::Subcode{0x1d}, Network::Subcode{0x1e}, Castle::SwitchHolderSubcode};

// 0x4e55c5..0x4e5776: the battle's cases of the B2 table 0x4e6140.
struct GateOpen
{
    std::uint8_t result = 0;
    std::uint16_t key = 0;
};
struct GateCommand
{
    std::uint8_t result = 0;
    std::uint8_t open = 0;
    std::uint16_t key = 0;
};
struct GateState
{
    std::uint8_t open = 0;
    std::uint16_t key = 0;
};
struct CrownAccess
{
    std::uint16_t firstKey = 0;
    std::uint16_t secondKey = 0;
    std::uint8_t status = 0;
};
struct CrownRegistration
{
    std::uint8_t result = 0;
    std::uint32_t elapsed = 0;
};
struct CrownState
{
    std::uint8_t result = 0;
};
struct SiegeActivation
{
    std::uint8_t active = 0;
};
struct CastleOwner
{
    std::uint8_t result = 0;
    std::array<std::uint8_t, 8> guild{};
};
struct SiegeSide
{
    std::uint8_t side = 0;
};
struct PanelStatus
{
    std::array<std::uint8_t, 4> bytes{};
};
struct SiegeTime
{
    std::uint8_t hours = 0;
    std::uint8_t minutes = 0;
};
// B7 00/01/02: 0x4e08a0, 0x4e08f0 and 0x44efc0.
struct WeaponOpen
{
    std::uint8_t result = 0;
    std::uint8_t mode = 0;
    std::uint16_t key = 0;
};
struct WeaponFire
{
    std::uint8_t kind = 0; // pkt[7], the projectile's kind at 0x44ef0e.
    std::uint16_t key = 0;
    std::uint8_t mode = 0; // pkt[4]: zero rejects, one keeps the target and the key (0x44eed1).
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};
struct WeaponImpact
{
    std::uint8_t kind = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// B9 01 (0x4e0750): the key (pkt[4] << 8 | pkt[5]) and the byte that goes to +0x93 of its character (pkt[6]).
// *Inferred*: the level of a gate or a statue, which the 13's creation also writes (0x4c9ee9) and the siege's draws
// compare with 2, 3 and 5 (0x42e3d0, 0x42dfe0, 0x42a2b0).
struct NpcLevel
{
    std::uint16_t key = 0;
    std::uint8_t level = 0;
};

// B9 02 (0x4e0790): the 32 bytes of pkt[4..0x23], the owner guild's mark in 8 × 8 colors of 4 bits.
inline constexpr std::size_t PackedMarkSize = 32;
struct OwnerMark
{
    std::array<std::uint8_t, PackedMarkSize> packed{};
};

// B9 03 (0x4e07f0): pkt[4]; other than 0, also pkt[5] and the dwords of pkt+8, pkt+0xc and pkt+0x10 (MOV r32, the low
// byte first), which go to [0x7d53700].
struct HuntZoneInfo
{
    std::uint8_t mode = 0;    // pkt[4], to +0x16
    std::uint8_t allowed = 0; // pkt[5], to +0x15 with SETNZ
    std::uint32_t fee = 0;    // pkt+8, to +0x18 and +0x1c
    std::uint32_t at0c = 0;   // pkt+0xc, to +0x24
    std::uint32_t at10 = 0;   // pkt+0x10, to +0x20
};

// B9 05 (0x4e0860): pkt[4].
struct HuntZoneEntryResult
{
    std::uint8_t result = 0;
};

// BA (0x4cf1b0): the key (pkt[3] << 8 | pkt[4], in a C2 too), which (pkt[5]) and, only with which at 0x28, the byte
// that goes to +0x1bc of its character (pkt[6]).
inline constexpr std::uint8_t ActorField1bcKind = 0x28;
struct ActorField
{
    std::uint16_t key = 0;
    std::uint8_t kind = 0;
    std::uint8_t value = 0;
};

// BB (0x4e0f30): the count (pkt[4], an unsigned byte) and the (index, x, y) triples from pkt+5, which go to
// 0x411ed0(index + 1, x, y) of the siege panel of class 2. It reads them only with that panel: here they are always
// read.
struct MinimapMember
{
    std::uint8_t index = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};
struct MinimapMembers
{
    std::vector<MinimapMember> members;
};

// B2 20 (0x4e04e0): the packet's first 0x1b bytes as they are (REP MOVSD × 6, MOVSW and MOVSB from pkt+0), which go to
// the half of the object of 0x5767924 that the key picks (pkt[4] << 8 | pkt[5]): 0x13d the first and another the
// second.
inline constexpr std::size_t SwitchHolderSize = 0x1b;
struct SwitchHolder
{
    std::uint16_t key = 0;
    std::array<std::uint8_t, SwitchHolderSize> bytes{};
};

using CastleBattlePacket =
    std::variant<NpcLevel, OwnerMark, HuntZoneInfo, HuntZoneEntryResult, ActorField, MinimapMembers, SwitchHolder,
                 GateOpen, GateCommand, GateState, CrownAccess, CrownRegistration, CrownState, SiegeActivation,
                 CastleOwner, SiegeSide, PanelStatus, SiegeTime, WeaponOpen, WeaponFire, WeaponImpact>;

// The battle's packet it is, by its opcode and, in the B9, the B7 and the B2, its subcode. Another opcode, or another
// subcode of the B9 (the 04 and from the 06 on fall to the default, 0x4e58c8), of the B7 or of the B2: WrongPacket.
[[nodiscard]] Core::Expected<CastleBattlePacket, Network::ProtocolError>
ReadCastleBattlePacket(const Network::Packet& packet);
} // namespace Client::Protocol::CastleSiege
