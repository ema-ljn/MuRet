#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Outgoing.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

// The guild, the alliance and the war (04, "El gremio, la alianza y la guerra"): the packets 0x4e2b10 hands to
// 0x4d74b0, 0x4d7a20, 0x4d7b60, 0x4d7c30, 0x4d7ff0, 0x4d8150, 0x4d8260, 0x4d8870, 0x4d9260, 0x4d9810, 0x6565e0,
// 0x4d9940, 0x4d9e50 and 0x4d9b80, and the ones the dispatcher handles inline (0x54, 0x5D, 0x61, 0x64, 0x66 and
// 0x67), without applying anything; and the ones the client sends. The original reads their fields at fixed offsets
// without looking at the length: here, a packet without the fields read is TooShort, and so is a list that says more
// entries than it carries, except the E9's: MyDll sends it short and MuRet zeroes what is missing (Q-C1365). Extra
// bytes are ignored. 0x55 and 0x5C have no case in 0x4e2b10 (they go to the default): the client does not read them.
namespace Client::Protocol::Guild
{
// 0x650830: the kind of the EB the client sends to kick a guild from the alliance.
inline constexpr std::uint8_t UnionKickKind = 1;
// The guild names: 8 bytes without a terminator (the original copies them with two DWORDs and puts the zero itself).
inline constexpr std::size_t GuildNameSize = 8;
inline constexpr std::size_t CharacterNameSize = FrontEnd::NameSize;
// The mark: 32 bytes, two nibbles per byte (the high one first).
inline constexpr std::size_t MarkBytes = 32;
// 0x4d7b60: 80 places of 0xe bytes at 0x787a5e8, up to the guilds' table (0x787aa48).
inline constexpr std::size_t MaxMembers = 80;
using GuildName = std::array<std::byte, GuildNameSize>;
// 0x62a878: the ten bytes of the number the player wrote (0x7798bf8).
inline constexpr std::size_t LeaveNumberSize = 10;
using LeaveNumber = std::array<std::byte, LeaveNumberSize>;
using GuildMark = std::array<std::uint8_t, MarkBytes>;

// 50 (0x4d74b0): the key of who asks to join, pkt[3] · 0x100 + pkt[4] (without the 0x7f).
struct JoinRequest
{
    std::uint16_t key = 0;
};

// 51 (0x4d7a20), 53 (0x4d7c30), 56 (0x4d7ff0) and 60 (0x4d8150): the result at pkt[3].
struct JoinResult
{
    std::uint8_t result = 0;
};

struct LeaveResult
{
    std::uint8_t result = 0;
};

struct CreateResult
{
    std::uint8_t result = 0;
};

struct WarRequestResult
{
    std::uint8_t result = 0;
};

// An entry of the 52: 0xd bytes from pkt[0x18].
struct MemberEntry
{
    FrontEnd::NameBytes name{};
    std::uint8_t number = 0; // +0xa
    std::uint8_t server = 0; // +0xb: bit 7 on, connected on the server of the low 7
    std::uint8_t status = 0; // +0xc: the rank (0x80 the master)
};

// 52 (0x4d7b60), a C2: pkt[5] the count, the DWORD at pkt[8] the score, pkt[0xd..0x14] a guild name and the entries
// from pkt[0x18]. pkt[4] (the GS 0.97's result) is not read.
struct MemberList
{
    std::uint8_t count = 0;
    std::int32_t score = 0;
    GuildName name{};
    std::vector<MemberEntry> members;
};

// 54 (0x4e4279): the dispatcher opens panel 0x40000 without reading the packet.
struct MasterQuestion
{
};

// 5D (0x4e42f0): the key, (pkt[3] & 0x7f) · 0x100 + pkt[4].
struct ViewportGuildDelete
{
    std::uint16_t key = 0;
};

// 61 (0x4e4382): the guild's name at pkt[3..10] and the type at pkt[0xb].
struct WarRequest
{
    GuildName name{};
    std::uint8_t type = 0;
};

// 62 (0x4d8260): the name at pkt[3..10], the type at pkt[0xb] and the team at pkt[0xc].
struct WarStart
{
    GuildName name{};
    std::uint8_t type = 0;
    std::uint8_t team = 0;
};

// 63 (0x4d8870): the result at pkt[3]. The name at pkt[4..11] is not read.
struct WarEnd
{
    std::uint8_t result = 0;
};

// 64 (0x4e4400): the two scores at pkt[3] and pkt[4]. pkt[5] is not read.
struct WarScore
{
    std::uint8_t ours = 0;
    std::uint8_t theirs = 0;
};

// 66 (0x4e42c7 → 0x447b90): the key from the DWORD at pkt[4], the two names at pkt[9..0x10] and pkt[0x11..0x18] and
// the mark at pkt[0x19..0x38].
struct GuildInfo
{
    std::int32_t key = 0;
    GuildName unionName{};
    GuildName guildName{};
    GuildMark mark{};
};

// An entry of the 65: 0xc bytes from pkt[5] (0x4d9294..0x4d92e9, 0x4d9799). The three after the character's key are
// not read.
struct ViewportGuildEntry
{
    // +0: the DWORD of the guild's key (0x4d929f).
    std::int32_t guildKey = 0;
    // +4, +5 and +6: what goes to the character's +0x368 (the rank), +0x369 and +0x370 (the relation).
    std::uint8_t status = 0;
    std::uint8_t byte369 = 0;
    std::uint8_t relation = 0;
    // +7 and +8: the character's key, (+7 & 0x7f) · 0x100 + (+8).
    std::uint16_t key = 0;
};

// 65 (0x4e42af → 0x4d9260), a C2: the count at pkt[4] and its entries. The C2 is *inferred*: the original reads the
// count at pkt[4] and the first entry at pkt[5]; the GameServer 0.97 does not send it, and the ASM is the oracle (04).
struct ViewportGuild
{
    std::vector<ViewportGuildEntry> entries;
};

// An entry of the 67: 0x14 bytes from pkt[5] (0x4e44ba..0x4e4509). +2, +3 and +0x11..+0x13 are not read.
struct ViewportUnionEntry
{
    // +0 and +1: the character's key, CONCAT11 without the 65's 0x7f.
    std::uint16_t key = 0;
    // +4: the DWORD of the guild's key.
    std::int32_t guildKey = 0;
    // +8: what goes to the character's +0x370.
    std::uint8_t relation = 0;
    // +9..+0x10: the alliance's name, two DWORDs.
    GuildName unionName{};
};

// 67 (0x4e44aa, inline), a C2 like the 65: the count at pkt[4] and its entries.
struct ViewportUnion
{
    std::vector<ViewportUnionEntry> entries;
};

// E1 (0x4d9810): the type at pkt[3] and the result at pkt[4].
struct AssignResult
{
    std::uint8_t type = 0;
    std::uint8_t result = 0;
};

// E5 (0x4e4444 → 0x6565e0): pkt[3..6], the relation type, the request and the requester's key (high and low).
struct RelationshipRequest
{
    std::uint8_t relation = 0;
    std::uint8_t request = 0;
    std::uint8_t keyHigh = 0;
    std::uint8_t keyLow = 0;
};

// E6 (0x4d9940): the relation type at pkt[3], the request at pkt[4], the result at pkt[5] and the key at pkt[6..7].
struct RelationshipResult
{
    std::uint8_t relation = 0;
    std::uint8_t request = 0;
    std::uint8_t result = 0;
    std::uint16_t key = 0;
};

// An entry of the E9: 0x29 bytes from pkt[8]: a byte, the mark and the name.
struct UnionEntry
{
    std::uint8_t byte0 = 0;
    GuildMark mark{};
    GuildName name{};
};

// E9 (0x4d9e50), a C2: the count at pkt[4], the result at pkt[5] and the entries (the ones that do not arrive, zeroed:
// Q-C1365).
struct UnionList
{
    std::uint8_t count = 0;
    std::uint8_t result = 0;
    std::vector<UnionEntry> entries;
};

// EB (0x4e4486): pkt[3] and, if it is 1, pkt[4] (0x4d9b80).
struct UnionListRefresh
{
    std::uint8_t kind = 0;
    std::uint8_t result = 0;
};

using GuildPacket =
    std::variant<JoinRequest, JoinResult, MemberList, LeaveResult, MasterQuestion, CreateResult, ViewportGuildDelete,
                 WarRequestResult, WarRequest, WarStart, WarEnd, WarScore, GuildInfo, AssignResult, RelationshipRequest,
                 RelationshipResult, UnionList, UnionListRefresh, ViewportGuild, ViewportUnion>;

// The guild packet it is, by its opcode. 52, 65, 67 and E9 are C2s; the others, C1s. Another opcode or another type:
// WrongPacket.
[[nodiscard]] Core::Expected<GuildPacket, Network::ProtocolError> ReadGuildPacket(const Network::Packet& packet);

// 0x4d6b60 and 0x4d754d..: C1 06 51 [yes] [key high] [key low], with the XOR32 and in the clear. With 8 (Confirm) the
// 1; with any other reason, and with the dialog busy when the 50 arrives, the 0.
[[nodiscard]] Outgoing WriteJoinAnswer(bool accept, std::uint16_t key);

// 0x4d7d95, 0x6599a0(0x10) and 0x655d70: C1 03 52 in the clear, the list request.
[[nodiscard]] Outgoing WriteMemberListRequest();

// 0x6570b0 and 0x655d70: C1 04 54 [answer], with the XOR32 and in the clear. Closing panel 0x40000 sends the 0.
[[nodiscard]] Outgoing WriteMasterAnswer(std::uint8_t answer);

// 0x6552b0: C1 2C 55, a byte at 0 (0x6553bf: the guild type of later versions, *inferred*), the guild's name (8 bytes)
// and the mark (32), with the XOR32 and in the clear. The GS 0.97 reads the name at pkt[3] (C1 2B, Q-C1287).
[[nodiscard]] Outgoing WriteCreate(const GuildName& name, const GuildMark& mark);

// 0x6565e0 and 0x653c00: C1 08 E6 [relation] [request] [result] [key high] [key low], with the XOR32 and in the clear.
[[nodiscard]] Outgoing WriteRelationshipAnswer(std::uint8_t relation, std::uint8_t request, std::uint8_t result,
                                               std::uint8_t keyHigh, std::uint8_t keyLow);

// 0x4d9b80 and 0x651840: C1 03 E9 in the clear, the alliance list request.
[[nodiscard]] Outgoing WriteUnionListRequest();

// 0x4d930f..0x4d9690: C1 08 66 [0] [guild key, DWORD], with the XOR32 and in the clear: the 65 asks for the 66 of a
// guild the table does not have. The 0 is the padding up to the DWORD, like the 66's pkt[3].
[[nodiscard]] Outgoing WriteGuildInfoRequest(std::int32_t guildKey);

// 0x4085d1 (command 5 of the commands window): C1 05 50 [key high] [key low], with the XOR32 and in the clear, the
// request to join the other's guild.
[[nodiscard]] Outgoing WriteJoinRequest(std::uint16_t key);

// 0x408a24, 0x409066 and 0x409663 (commands 6, 7 and 8): C1 07 E5 [relation] [request] [key high] [key low], with the
// XOR32 and in the clear: the alliance (1, 1), the rivalry (2, 1) and breaking it (2, 2).
[[nodiscard]] Outgoing WriteRelationshipRequest(std::uint8_t relation, std::uint8_t request, std::uint16_t key);

// 0x64def0: C1 0F E1 [type] [rank] [name, 10 bytes], with the XOR32 and in the clear. Taking a member's post away
// sends type 3 with rank 0.
[[nodiscard]] Outgoing WriteAssignStatus(std::uint8_t type, std::uint8_t status, const FrontEnd::NameBytes& name);

// 0x650830 (answer 8 of the alliance tab's dialog): C1 0C EB 01 [guild name, 8 bytes from 0x7d535a8], with the XOR32
// and in the clear. The GS 0.97 has no EB: what the client says is the ASM.
[[nodiscard]] Outgoing WriteUnionKick(const GuildName& name);

// 0x62a878 (the yes of the game's dialog 0x7d): C1 17 53 [name, 10 bytes of place 0x6d5e2c of 0x787a5e8] [number, 10
// bytes of 0x7798bf8], with the XOR32 and in the clear. The GS (0x420bf0, CGGuildDelUser) reads the name at +3 and the
// number at +0xd: the master with their own name dissolves the guild, with another's kicks them; a non-master leaves.
[[nodiscard]] Outgoing WriteLeave(const FrontEnd::NameBytes& name, const LeaveNumber& number);

// 0x62b39c (the yes of the game's dialog 0x7f, the 61's war request) and 0x62c5c2 (the no): C1 04 61 [1 or 0], with
// the XOR32 and in the clear through the socket, without rand(). The GS (CGGuildWarRequestResultRecv, 0x422020) reads
// the answer at pkt[3].
[[nodiscard]] Outgoing WriteWarAnswer(bool accept);
} // namespace Client::Protocol::Guild
