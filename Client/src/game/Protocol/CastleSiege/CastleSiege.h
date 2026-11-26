#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

// The castle's windows (04, "Castle Siege"): what 0x4e2b10 dispatches with B2 (the table 0x4e6140 and its second
// level, 0x4df080, 0x4df1a0, 0x4df7d0, 0x4df950 and the inline cases), B3 (0x4dfc60), B4 (0x4dfce0), B5 (0x4dfd80), B6
// (0x4e0ed0) and B8 (0x4e0700), applying nothing; and what the castle's windows send. The lab's GameServer 0.97 has no
// Castle Siege: each byte comes from the client's ASM, *static*.
//
// The subcode of the B2 and the B8 is pkt[3] in a C1 and pkt[4] in the others (0x4e509c, 0x4e5883), the byte after the
// opcode, as the Dispatcher reads it. Then each handler reads its fields at fixed offsets from the packet's first byte,
// without looking at the type or the length: here too (in a C2 the result and the subcode are the same pkt[4]). A
// packet without what is read is TooShort, and so is a list that says more entries than it carries; extra bytes are
// ignored. The battle's B2 (0x11..0x19, 0x1D, 0x1E and 0x20) are not read here (CastleBattle).
namespace Client::Protocol::CastleSiege
{
// The ones the session registers. The other cases of 0x4e6140 (0x11..0x19, 0x1D, 0x1E and 0x20) are the battle's, and
// 0x0A..0x0F, 0x1B and 0x1C fall to the default (0x4e5b4a): they still arrive as Unknown.
inline constexpr std::array<Network::Subcode, 13> WindowSubcodes{
    Castle::StateSubcode,        Castle::RegisterSubcode, Castle::GiveUpSubcode,    Castle::RegistrationSubcode,
    Castle::RegisterMarkSubcode, Castle::NpcBuySubcode,   Castle::NpcRepairSubcode, Castle::NpcUpgradeSubcode,
    Castle::TaxInfoSubcode,      Castle::TaxRateSubcode,  Castle::MoneyOutSubcode,  Castle::SiegeFlagsSubcode,
    Castle::HuntZoneGateSubcode};

// The names as 0x64b0f0, 0x46f7e0 and 0x470bb0 copy them (strncpy of 8 and of 10, no terminator in the packet).
// GuildName is Protocol::Guild's type again: they unify when the chain joins.
inline constexpr std::size_t GuildNameSize = 8;
using GuildName = std::array<std::byte, GuildNameSize>;

// 0x4e5b4a: the result each case asks for, pkt[4].
inline constexpr std::uint8_t ResultFailed = 0;
inline constexpr std::uint8_t ResultOk = 1;

// B2 00 (0x4e50bd, table 0x4e61c4): the castle's state. With 1 or 2, 0x64b0f0 reads pkt[5..0x2d].
struct SiegeDate
{
    std::uint16_t year = 0;             // pkt[6..7] (and +0xc, +0x12), the first on top
    std::array<std::uint8_t, 4> rest{}; // pkt[8..0xb] (and +0xe..+0x11, +0x14..+0x17)
};
struct CastleState
{
    std::uint8_t result = 0;
    std::int8_t state = 0;             // pkt[5], MOVSX
    std::array<SiegeDate, 3> dates{};  // pkt[6..0x17]
    GuildName ownerGuild{};            // pkt[0x18..0x1f]
    FrontEnd::NameBytes ownerMaster{}; // pkt[0x20..0x29]
    std::uint32_t remaining = 0;       // pkt[0x2a..0x2d], the first on top
};

// B2 01 (0x4df080, 0..8) and B2 02 (0x4df1a0, 0..3): only the result.
struct RegisterResult
{
    std::uint8_t result = 0;
};
struct GiveUpResult
{
    std::uint8_t result = 0;
};

// B2 03 (0x4e5186): with 1, pkt[0x11] (0x64d290 with pkt[0x11] == 0) and the marks of pkt[0xd..0x10], the first on
// top.
struct RegistrationInfo
{
    std::uint8_t result = 0;
    std::uint32_t marks = 0;
    std::uint8_t gaveUp = 0;
};

// B2 04 (0x4e51fa, table 0x4e61d8): with 1, the marks of pkt[0xd..0x10].
struct RegisterMarkResult
{
    std::uint8_t result = 0;
    std::uint32_t marks = 0;
};

// B2 05 (0x4df7d0), 06 (0x4e52b7) and 07 (0x4df950): with 1, the type (pkt+8), the index (pkt+0xc) and what changes,
// dwords as they are (MOV r32: little-endian).
struct NpcBuyResult
{
    std::uint8_t result = 0;
    std::uint32_t type = 0;
    std::uint32_t index = 0;
};
struct NpcRepairResult
{
    std::uint8_t result = 0;
    std::uint32_t type = 0;
    std::uint32_t index = 0;
    std::uint32_t life = 0;    // pkt+0x10, to +0x14 of the record
    std::uint32_t maxLife = 0; // pkt+0x14, to +0x10 of the record
};
struct NpcUpgradeResult
{
    std::uint8_t result = 0;
    std::uint32_t type = 0;
    std::uint32_t index = 0;
    std::uint32_t kind = 0; // pkt+0x10: 1 the defense, 2 the recovery, 3 the life
    std::uint32_t value = 0;
};

// B2 08 (0x4e537b → 0x669070): with 1, the two taxes (pkt[5] and pkt[6]) and the castle's money, pkt[7..0xe] with the
// first on top.
struct TaxInfo
{
    std::uint8_t result = 0;
    std::uint8_t chaosTax = 0;
    std::uint8_t storeTax = 0;
    std::uint64_t money = 0;
};

// B2 09 (0x4e53e7): with 1, which (pkt[5]: 1 and 2 the taxes, 0x6690f0; 3 the hunting zone's price) and the value of
// pkt[6..9], the first on top.
struct TaxRateResult
{
    std::uint8_t result = 0;
    std::uint8_t kind = 0;
    std::uint32_t value = 0;
};

// B2 10 (0x4e548a → 0x669160): with 1, the castle's money, pkt[5..0xc] with the first on top.
struct MoneyOutResult
{
    std::uint8_t result = 0;
    std::uint64_t money = 0;
};

// B2 1A (0x4e54f6): pkt[4] at 1 writes 0x78aba34 and at 2 0x78aba30 with pkt[5].
struct SiegeFlags
{
    std::uint8_t which = 0;
    std::uint8_t value = 0;
};

// B2 1F (0x4e5532): with 1, the entry to the hunting zone (pkt[5] other than 0).
struct HuntZoneGateResult
{
    std::uint8_t result = 0;
    std::uint8_t allowed = 0;
};

// B3 (0x4dfc60): with pkt[4] at 1, the count (an int at pkt+8; the loop with JL does not run with 0 or a negative one)
// and the 0x1c-byte records from pkt+0xc, which 0x668fd0 copies whole.
inline constexpr std::size_t NpcRecordSize = 0x1c;
using NpcRecord = std::array<std::byte, NpcRecordSize>;
struct NpcList
{
    std::uint8_t result = 0;
    std::vector<NpcRecord> records;
};

// B4 (0x4dfce0): with pkt[4] at 1, 0xe-byte records from pkt+0xc: the name, the marks (pkt[8..0xb] of the record, the
// first on top) and two bytes, as it passes them to 0x64d2c0.
struct SiegeGuildEntry
{
    GuildName name{};
    std::uint32_t marks = 0;
    std::uint8_t first = 0;  // +0xc
    std::uint8_t second = 0; // +0xd
};
struct SiegeGuildList
{
    std::uint8_t result = 0;
    std::vector<SiegeGuildEntry> entries;
};

// B5 (0x4dfd80, table 0x4dfe24): with pkt[4] at 1, 10-byte records from pkt+0xc: two bytes and the name, as it passes
// them to 0x64d310.
struct RegisteredGuildEntry
{
    std::uint8_t first = 0;  // +0
    std::uint8_t second = 0; // +1
    GuildName name{};        // +2
};
struct RegisteredGuildList
{
    std::uint8_t result = 0;
    std::vector<RegisteredGuildEntry> entries;
};

// B6 (0x4e0ed0): the count (an int at pkt+4) and the (x, y) pairs from pkt+8.
struct MinimapPoint
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};
struct MinimapPoints
{
    std::vector<MinimapPoint> points;
};

// B8 01 (0x4e0700): pkt[4], which in a C2 is the subcode itself.
struct KillCount
{
    std::uint8_t value = 0;
};

using CastleSiegePacket =
    std::variant<CastleState, RegisterResult, GiveUpResult, RegistrationInfo, RegisterMarkResult, NpcBuyResult,
                 NpcRepairResult, NpcUpgradeResult, TaxInfo, TaxRateResult, MoneyOutResult, SiegeFlags,
                 HuntZoneGateResult, NpcList, SiegeGuildList, RegisteredGuildList, MinimapPoints, KillCount>;

// The castle's packet it is, by its opcode and, in the B2 and the B8, its subcode. Another opcode, a battle's B2 or a
// B8 with another subcode: WrongPacket.
[[nodiscard]] Core::Expected<CastleSiegePacket, Network::ProtocolError>
ReadCastleSiegePacket(const Network::Packet& packet);

// What the windows send: the writer of 0x6b85f4 on the stack, send() in the clear on the game's socket, and the XOR32
// from byte 3 as it puts each field.
// 0x4d25dd (the NPC with the result 13): C1 04 B2 00.
[[nodiscard]] Outgoing WriteStateRequest();
// 0x64c0b0 (the button with the state 1): C1 04 B2 01.
[[nodiscard]] Outgoing WriteRegister();
// 0x64ccd0 (the dialog's answer 8): C1 05 B2 02 01.
[[nodiscard]] Outgoing WriteGiveUp();
// 0x64ae10 (opening the 0x80000 with +4 at 0) and 0x4df1a0 (the B2 02 with 1): C1 04 B2 03.
[[nodiscard]] Outgoing WriteRegistrationRequest();
// 0x64c0b0 (the button with the state 3): C1 05 B2 04 and the mark's place in the inventory (0x64b9d0).
[[nodiscard]] Outgoing WriteRegisterMark(std::uint8_t slot);
// 0x6623e0 and 0x664370 (the answer 8): C1 0C B2 05 with +0x1a of the record at 0 and C1 0C B2 06 with another value;
// the record's type and index, dwords as they are.
[[nodiscard]] Outgoing WriteNpcBuy(std::uint32_t type, std::uint32_t index);
[[nodiscard]] Outgoing WriteNpcRepair(std::uint32_t type, std::uint32_t index);
// 0x662d50 and 0x664ce0 (3, the life), 0x663440 and 0x6653d0 (1, the defense) and 0x665ab0 (2, the recovery): C1 14
// B2 07 with the type, the index, which and the new value, four dwords as they are.
[[nodiscard]] Outgoing WriteNpcUpgrade(std::uint32_t type, std::uint32_t index, std::uint32_t kind,
                                       std::uint32_t value);
// 0x661310 (opening the 0x100000): C1 04 B2 08.
[[nodiscard]] Outgoing WriteTaxInfoRequest();
// 0x666b30 (1 and 2, the taxes) and 0x648a30 (3, the hunting zone's price): C1 09 B2 09 with which and the value, the
// high byte first (SAR 0x18, 0x10, 8 and the low one).
[[nodiscard]] Outgoing WriteTaxRate(std::uint8_t kind, std::uint32_t value);
// 0x667a40 (the answer 2): C1 08 B2 10 with what is taken out, the low byte first (BL, BH and the high word).
[[nodiscard]] Outgoing WriteMoneyOut(std::uint32_t amount);
// 0x648a30: C1 05 B2 1F with +0x15 ^ 1 (0x648c55).
[[nodiscard]] Outgoing WriteHuntZoneGate(std::uint8_t allowed);
// 0x661310: C1 04 B3 01 and C1 04 B3 02.
[[nodiscard]] Outgoing WriteNpcListRequest(std::uint8_t kind);
// 0x64d350 and 0x4df1a0: C1 03 B4; 0x64d350: C1 03 B5. No fields, nothing to encrypt.
[[nodiscard]] Outgoing WriteSiegeGuildListRequest();
[[nodiscard]] Outgoing WriteRegisteredGuildListRequest();
// 0x648a30 (+0x1c4, in both modes): C1 08 B9 05 with the price of +0x18, the dword as it is.
[[nodiscard]] Outgoing WriteHuntZoneEntry(std::uint32_t fee);
} // namespace Client::Protocol::CastleSiege
