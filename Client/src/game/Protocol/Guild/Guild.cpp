#include "Protocol/Guild/Guild.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace Client::Protocol::Guild
{
namespace
{
constexpr Network::PacketLayout C1Layout(Network::Opcode opcode, std::size_t minimumSize, std::uint32_t address)
{
    return {{Network::PacketType::C1, opcode, std::nullopt}, minimumSize, address};
}

constexpr Network::PacketLayout C2Layout(Network::Opcode opcode, std::size_t minimumSize, std::uint32_t address)
{
    return {{Network::PacketType::C2, opcode, std::nullopt}, minimumSize, address};
}

using Result = Network::Field<std::uint8_t, 3>;

// 0x4d7b60: (DWORD < 0) − 1 & DWORD leaves the score at 0 if it is negative (the applier does it).
using MemberCount = Network::Field<std::uint8_t, 5>;
using MemberScore = Network::Field<std::uint32_t, 8>;
using MemberGuildName = Network::Field<GuildName, 0xd>;
constexpr std::size_t FirstMemberOffset = 0x18;
constexpr std::size_t MemberEntrySize = 0xd;
static_assert(CharacterNameSize + 3 == MemberEntrySize);

using KeyHigh = Network::Field<std::uint8_t, 3>;
using KeyLow = Network::Field<std::uint8_t, 4>;
// 0x4e42fb: the 5D takes bit 7 off the high byte; 0x4d74d6, the 50, does not.
constexpr std::uint8_t KeyHighMask = 0x7f;

using WarName = Network::Field<GuildName, 3>;
using WarType = Network::Field<std::uint8_t, 0xb>;
using WarTeam = Network::Field<std::uint8_t, 0xc>;

using ScoreOurs = Network::Field<std::uint8_t, 3>;
using ScoreTheirs = Network::Field<std::uint8_t, 4>;

using InfoKey = Network::Field<std::uint32_t, 4>;
using InfoUnionName = Network::Field<GuildName, 9>;
using InfoGuildName = Network::Field<GuildName, 0x11>;
constexpr std::size_t InfoMarkOffset = 0x19;

using AssignType = Network::Field<std::uint8_t, 3>;
using AssignCode = Network::Field<std::uint8_t, 4>;

using Relation = Network::Field<std::uint8_t, 3>;
using RelationRequest = Network::Field<std::uint8_t, 4>;
using RequesterHigh = Network::Field<std::uint8_t, 5>;
using RequesterLow = Network::Field<std::uint8_t, 6>;
using RelationResult = Network::Field<std::uint8_t, 5>;
// 0x4d9ae9: CONCAT11(pkt[6], pkt[7]).
using RelationKey = Network::Field<std::uint16_t, 6, std::endian::big>;

using UnionCount = Network::Field<std::uint8_t, 4>;
using UnionResult = Network::Field<std::uint8_t, 5>;
constexpr std::size_t FirstUnionOffset = 8;
constexpr std::size_t UnionEntrySize = 0x29;

// EB: only pkt[3] at 1 reads pkt[4].
using RefreshKindField = Network::Field<std::uint8_t, 3>;
using RefreshResult = Network::Field<std::uint8_t, 4>;
constexpr std::uint8_t RefreshKind = 1;

// 0x4d9289 and 0x4e44af: the count at pkt[4] and the entries of the 65 and the 67 from pkt[5], of 0xc and of 0x14
// bytes, read up to +8 and up to +0x10.
using ViewportCount = Network::Field<std::uint8_t, 4>;
constexpr std::size_t ViewportGuildEntrySize = 0xc;
constexpr std::size_t ViewportGuildEntryRead = 9;
constexpr std::size_t ViewportUnionEntrySize = 0x14;
constexpr std::size_t ViewportUnionEntryRead = 0x11;
// 0x4d92a2: bit 7 of the 65's high key byte is not the key's.
constexpr std::uint8_t ViewportKeyMask = 0x7f;

std::uint8_t U8At(std::span<const std::byte> bytes, std::size_t at)
{
    return std::to_integer<std::uint8_t>(bytes[at]);
}

std::uint32_t U32LeAt(std::span<const std::byte> bytes, std::size_t at)
{
    return static_cast<std::uint32_t>(U8At(bytes, at)) | static_cast<std::uint32_t>(U8At(bytes, at + 1)) << 8U |
           static_cast<std::uint32_t>(U8At(bytes, at + 2)) << 16U |
           static_cast<std::uint32_t>(U8At(bytes, at + 3)) << 24U;
}

template <std::size_t Size> void CopyTo(std::span<const std::byte> bytes, std::array<std::byte, Size>& target)
{
    std::ranges::copy(bytes.first(Size), target.begin());
}

GuildMark MarkOf(std::span<const std::byte> bytes)
{
    GuildMark mark{};
    std::ranges::transform(bytes.first(MarkBytes), mark.begin(),
                           [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
    return mark;
}

template <typename ResultPacket>
Core::Expected<GuildPacket, Network::ProtocolError> ReadResult(const Network::Packet& packet, Network::Opcode opcode,
                                                               std::uint32_t address)
{
    return Network::ReadPacket(packet, C1Layout(opcode, Result::End, address), [](Network::LayoutReader& reader)
                               { return GuildPacket{ResultPacket{reader.Read<Result>()}}; });
}

MemberEntry ReadMemberEntry(std::span<const std::byte, MemberEntrySize> bytes)
{
    MemberEntry entry;
    CopyTo(bytes, entry.name);
    entry.number = U8At(bytes, CharacterNameSize);
    entry.server = U8At(bytes, CharacterNameSize + 1);
    entry.status = U8At(bytes, CharacterNameSize + 2);
    return entry;
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadMemberList(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, C2Layout(GuildMemberListOpcode, MemberGuildName::End, 0x4d7b60),
                               [](Network::LayoutReader& reader)
                               {
                                   MemberList list{.count = reader.Read<MemberCount>(),
                                                   .score = static_cast<std::int32_t>(reader.Read<MemberScore>()),
                                                   .name = reader.Read<MemberGuildName>(),
                                                   .members = {}};
                                   if (list.count != 0)
                                   {
                                       list.members = Network::ReadList<MemberEntrySize>(reader, FirstMemberOffset,
                                                                                         list.count, ReadMemberEntry);
                                   }
                                   return GuildPacket{std::move(list)};
                               });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadJoinRequest(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, C1Layout(GuildJoinRequestOpcode, KeyLow::End, 0x4d74b0),
                               [](Network::LayoutReader& reader)
                               {
                                   const auto key =
                                       static_cast<std::uint16_t>(reader.Read<KeyHigh>() << 8 | reader.Read<KeyLow>());
                                   return GuildPacket{JoinRequest{key}};
                               });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadViewportGuildDelete(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, C1Layout(ViewportGuildDeleteOpcode, KeyLow::End, 0x4e42f0),
                               [](Network::LayoutReader& reader)
                               {
                                   const auto key = static_cast<std::uint16_t>(
                                       (reader.Read<KeyHigh>() & KeyHighMask) << 8 | reader.Read<KeyLow>());
                                   return GuildPacket{ViewportGuildDelete{key}};
                               });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadWarRequest(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, C1Layout(GuildWarRequestOpcode, WarType::End, 0x4e4382), [](Network::LayoutReader& reader)
        { return GuildPacket{WarRequest{.name = reader.Read<WarName>(), .type = reader.Read<WarType>()}}; });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadWarStart(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, C1Layout(GuildWarStartOpcode, WarTeam::End, 0x4d8260),
                               [](Network::LayoutReader& reader)
                               {
                                   return GuildPacket{WarStart{.name = reader.Read<WarName>(),
                                                               .type = reader.Read<WarType>(),
                                                               .team = reader.Read<WarTeam>()}};
                               });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadWarScore(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, C1Layout(GuildWarScoreOpcode, ScoreTheirs::End, 0x4e4400), [](Network::LayoutReader& reader)
        { return GuildPacket{WarScore{.ours = reader.Read<ScoreOurs>(), .theirs = reader.Read<ScoreTheirs>()}}; });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadGuildInfo(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, C1Layout(GuildInfoOpcode, InfoMarkOffset + MarkBytes, 0x4e42c7),
                               [](Network::LayoutReader& reader)
                               {
                                   return GuildPacket{
                                       GuildInfo{.key = static_cast<std::int32_t>(reader.Read<InfoKey>()),
                                                 .unionName = reader.Read<InfoUnionName>(),
                                                 .guildName = reader.Read<InfoGuildName>(),
                                                 .mark = MarkOf(reader.Bytes(InfoMarkOffset, MarkBytes))}};
                               });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadAssignResult(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, C1Layout(GuildAssignResultOpcode, AssignCode::End, 0x4d9810), [](Network::LayoutReader& reader)
        { return GuildPacket{AssignResult{.type = reader.Read<AssignType>(), .result = reader.Read<AssignCode>()}}; });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadRelationshipRequest(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, C1Layout(RelationshipRequestOpcode, RequesterLow::End, 0x4e4444),
                               [](Network::LayoutReader& reader)
                               {
                                   return GuildPacket{RelationshipRequest{.relation = reader.Read<Relation>(),
                                                                          .request = reader.Read<RelationRequest>(),
                                                                          .keyHigh = reader.Read<RequesterHigh>(),
                                                                          .keyLow = reader.Read<RequesterLow>()}};
                               });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadRelationshipResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, C1Layout(RelationshipResultOpcode, RelationKey::End, 0x4d9940),
                               [](Network::LayoutReader& reader)
                               {
                                   return GuildPacket{RelationshipResult{.relation = reader.Read<Relation>(),
                                                                         .request = reader.Read<RelationRequest>(),
                                                                         .result = reader.Read<RelationResult>(),
                                                                         .key = reader.Read<RelationKey>()}};
                               });
}

UnionEntry ReadUnionEntry(std::span<const std::byte> bytes)
{
    UnionEntry entry{.byte0 = U8At(bytes, 0), .mark = MarkOf(bytes.subspan(1)), .name = {}};
    CopyTo(bytes.subspan(1 + MarkBytes), entry.name);
    return entry;
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadUnionList(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, C2Layout(UnionListOpcode, UnionResult::End, 0x4d9e50),
        [](Network::LayoutReader& reader)
        {
            UnionList list{.count = reader.Read<UnionCount>(), .result = reader.Read<UnionResult>(), .entries = {}};
            // 0x4d9e5f: only with the result at 1 and some entry are they read.
            if (list.result != 1 || list.count == 0)
            {
                return GuildPacket{std::move(list)};
            }
            // 0x4d9e50 reads `count` entries without looking at the length. MyDll (alliance_list_send) announces every
            // guild of the alliance and writes only the ones it finds, with a tail of 8 zeros: the original reads the
            // missing ones from the tail and whatever follows in the buffer. MuRet takes what arrived and zeroes the
            // rest: an empty name, which the list (0x46d900) does not show (Q-C1365).
            std::vector<std::byte> region(list.count * UnionEntrySize);
            if (reader.Size() > FirstUnionOffset)
            {
                const std::size_t arrived = std::min(reader.Size() - FirstUnionOffset, region.size());
                std::ranges::copy(reader.Bytes(FirstUnionOffset, arrived), region.begin());
            }
            const std::span<const std::byte> entries(region);
            for (std::size_t i = 0; i < list.count; ++i)
            {
                list.entries.push_back(ReadUnionEntry(entries.subspan(i * UnionEntrySize, UnionEntrySize)));
            }
            return GuildPacket{std::move(list)};
        });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadUnionListRefresh(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, C1Layout(UnionKickOpcode, RefreshKindField::End, 0x4e4486),
                               [](Network::LayoutReader& reader)
                               {
                                   UnionListRefresh refresh{.kind = reader.Read<RefreshKindField>(), .result = 0};
                                   if (refresh.kind == RefreshKind)
                                   {
                                       refresh.result = reader.Read<RefreshResult>();
                                   }
                                   return GuildPacket{refresh};
                               });
}

ViewportGuildEntry ReadViewportGuildEntry(std::span<const std::byte, ViewportGuildEntryRead> bytes)
{
    return {.guildKey = static_cast<std::int32_t>(U32LeAt(bytes, 0)),
            .status = U8At(bytes, 4),
            .byte369 = U8At(bytes, 5),
            .relation = U8At(bytes, 6),
            .key = static_cast<std::uint16_t>((U8At(bytes, 7) & ViewportKeyMask) << 8 | U8At(bytes, 8))};
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadViewportGuild(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, C2Layout(ViewportGuildOpcode, ViewportCount::End, 0x4d9260),
        [](Network::LayoutReader& reader)
        {
            return GuildPacket{ViewportGuild{Network::ReadList<ViewportGuildEntrySize, ViewportGuildEntryRead>(
                reader, ViewportCount::End, reader.Read<ViewportCount>(), ReadViewportGuildEntry)}};
        });
}

ViewportUnionEntry ReadViewportUnionEntry(std::span<const std::byte, ViewportUnionEntryRead> bytes)
{
    constexpr std::size_t GuildKeyOffset = 4;
    constexpr std::size_t RelationOffset = 8;
    constexpr std::size_t UnionNameOffset = 9;
    ViewportUnionEntry entry{.key = static_cast<std::uint16_t>(U8At(bytes, 0) << 8 | U8At(bytes, 1)),
                             .guildKey = static_cast<std::int32_t>(U32LeAt(bytes, GuildKeyOffset)),
                             .relation = U8At(bytes, RelationOffset),
                             .unionName = {}};
    CopyTo(bytes.subspan(UnionNameOffset), entry.unionName);
    return entry;
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadViewportUnion(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, C2Layout(ViewportUnionOpcode, ViewportCount::End, 0x4e44aa),
        [](Network::LayoutReader& reader)
        {
            return GuildPacket{ViewportUnion{Network::ReadList<ViewportUnionEntrySize, ViewportUnionEntryRead>(
                reader, ViewportCount::End, reader.Read<ViewportCount>(), ReadViewportUnionEntry)}};
        });
}

Core::Expected<GuildPacket, Network::ProtocolError> ReadC1(const Network::Packet& packet, Network::Opcode opcode)
{
    switch (Network::ByteOf(opcode))
    {
    case Network::ByteOf(GuildJoinRequestOpcode):
        return ReadJoinRequest(packet);
    case Network::ByteOf(GuildJoinResultOpcode):
        return ReadResult<JoinResult>(packet, GuildJoinResultOpcode, 0x4d7a20);
    case Network::ByteOf(GuildLeaveResultOpcode):
        return ReadResult<LeaveResult>(packet, GuildLeaveResultOpcode, 0x4d7c30);
    case Network::ByteOf(GuildMasterQuestionOpcode):
        return GuildPacket{MasterQuestion{}};
    case Network::ByteOf(GuildCreateResultOpcode):
        return ReadResult<CreateResult>(packet, GuildCreateResultOpcode, 0x4d7ff0);
    case Network::ByteOf(ViewportGuildDeleteOpcode):
        return ReadViewportGuildDelete(packet);
    case Network::ByteOf(GuildWarRequestResultOpcode):
        return ReadResult<WarRequestResult>(packet, GuildWarRequestResultOpcode, 0x4d8150);
    case Network::ByteOf(GuildWarRequestOpcode):
        return ReadWarRequest(packet);
    case Network::ByteOf(GuildWarStartOpcode):
        return ReadWarStart(packet);
    case Network::ByteOf(GuildWarEndOpcode):
        return ReadResult<WarEnd>(packet, GuildWarEndOpcode, 0x4d8870);
    case Network::ByteOf(GuildWarScoreOpcode):
        return ReadWarScore(packet);
    case Network::ByteOf(GuildInfoOpcode):
        return ReadGuildInfo(packet);
    case Network::ByteOf(GuildAssignResultOpcode):
        return ReadAssignResult(packet);
    case Network::ByteOf(RelationshipRequestOpcode):
        return ReadRelationshipRequest(packet);
    case Network::ByteOf(RelationshipResultOpcode):
        return ReadRelationshipResult(packet);
    case Network::ByteOf(UnionKickOpcode):
        return ReadUnionListRefresh(packet);
    default:
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
}
} // namespace

Core::Expected<GuildPacket, Network::ProtocolError> ReadGuildPacket(const Network::Packet& packet)
{
    const std::optional<Network::Opcode> opcode = packet.Opcode();
    if (!opcode)
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    if (packet.Type() == Network::PacketType::C2)
    {
        if (*opcode == GuildMemberListOpcode)
            return ReadMemberList(packet);
        if (*opcode == UnionListOpcode)
            return ReadUnionList(packet);
        if (*opcode == ViewportGuildOpcode)
            return ReadViewportGuild(packet);
        if (*opcode == ViewportUnionOpcode)
            return ReadViewportUnion(packet);
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    }
    if (packet.Type() != Network::PacketType::C1)
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    return ReadC1(packet, *opcode);
}

Outgoing WriteJoinAnswer(bool accept, std::uint16_t key)
{
    return Xor32C1(Network::PacketWriter::C1(GuildJoinResultOpcode)
                       .PutU8(accept ? std::uint8_t{1} : std::uint8_t{0})
                       .PutU16Be(key));
}

Outgoing WriteMemberListRequest()
{
    return ClearC1(Network::PacketWriter::C1(GuildMemberListOpcode));
}

Outgoing WriteMasterAnswer(std::uint8_t answer)
{
    return Xor32C1(Network::PacketWriter::C1(GuildMasterQuestionOpcode).PutU8(answer));
}

// 0x6553bf..0x6553e8: the byte after the 55, always 0.
constexpr std::uint8_t CreateKindByte = 0;

Outgoing WriteCreate(const GuildName& name, const GuildMark& mark)
{
    std::array<std::byte, MarkBytes> markBytes{};
    std::ranges::transform(mark, markBytes.begin(), [](std::uint8_t value) { return std::byte{value}; });
    return Xor32C1(
        Network::PacketWriter::C1(GuildCreateOpcode).PutU8(CreateKindByte).PutBytes(name).PutBytes(markBytes));
}

Outgoing WriteRelationshipAnswer(std::uint8_t relation, std::uint8_t request, std::uint8_t result, std::uint8_t keyHigh,
                                 std::uint8_t keyLow)
{
    return Xor32C1(Network::PacketWriter::C1(RelationshipResultOpcode)
                       .PutU8(relation)
                       .PutU8(request)
                       .PutU8(result)
                       .PutU8(keyHigh)
                       .PutU8(keyLow));
}

Outgoing WriteUnionListRequest()
{
    return ClearC1(Network::PacketWriter::C1(UnionListOpcode));
}

Outgoing WriteGuildInfoRequest(std::int32_t guildKey)
{
    return Xor32C1(Network::PacketWriter::C1(GuildInfoOpcode).PutU8(0).PutU32Le(static_cast<std::uint32_t>(guildKey)));
}

Outgoing WriteJoinRequest(std::uint16_t key)
{
    return Xor32C1(Network::PacketWriter::C1(GuildJoinRequestOpcode)
                       .PutU8(static_cast<std::uint8_t>(key >> 8))
                       .PutU8(static_cast<std::uint8_t>(key & 0xff)));
}

Outgoing WriteRelationshipRequest(std::uint8_t relation, std::uint8_t request, std::uint16_t key)
{
    return Xor32C1(Network::PacketWriter::C1(RelationshipRequestOpcode)
                       .PutU8(relation)
                       .PutU8(request)
                       .PutU8(static_cast<std::uint8_t>(key >> 8))
                       .PutU8(static_cast<std::uint8_t>(key & 0xff)));
}

Outgoing WriteAssignStatus(std::uint8_t type, std::uint8_t status, const FrontEnd::NameBytes& name)
{
    return Xor32C1(Network::PacketWriter::C1(GuildAssignResultOpcode).PutU8(type).PutU8(status).PutBytes(name));
}

Outgoing WriteUnionKick(const GuildName& name)
{
    return Xor32C1(Network::PacketWriter::C1(UnionKickOpcode).PutU8(UnionKickKind).PutBytes(name));
}

Outgoing WriteLeave(const FrontEnd::NameBytes& name, const LeaveNumber& number)
{
    return Xor32C1(Network::PacketWriter::C1(GuildLeaveResultOpcode).PutBytes(name).PutBytes(number));
}

Outgoing WriteWarAnswer(bool accept)
{
    return Xor32C1(Network::PacketWriter::C1(GuildWarRequestOpcode).PutU8(accept ? std::uint8_t{1} : std::uint8_t{0}));
}
} // namespace Client::Protocol::Guild
