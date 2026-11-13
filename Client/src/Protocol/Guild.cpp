#include "Protocol/Guild.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>

namespace Client::Protocol::Guild
{
namespace
{
// 0x4d7b60: el puntaje en pkt[8], el nombre en pkt[0xd] y las entradas desde pkt[0x18], de 0xd bytes.
constexpr std::size_t MemberScoreOffset = 8;
constexpr std::size_t MemberNameOffset = 0xd;
constexpr std::size_t FirstMemberOffset = 0x18;
constexpr std::size_t MemberEntrySize = 0xd;
// 0x4e42c7: la clave en pkt[4] y los nombres en pkt[9] y pkt[0x11], la marca en pkt[0x19].
constexpr std::size_t GuildInfoKeyOffset = 4;
constexpr std::size_t GuildInfoNameOffset = 9;
// 0x4d9294 y 0x4e44ba: las entradas del 65 y del 67 desde pkt[5], de 0xc y de 0x14 bytes; los bytes que se leen de
// cada una (hasta +8 y hasta +0x10).
constexpr std::size_t FirstViewportEntryOffset = 5;
constexpr std::size_t ViewportGuildEntrySize = 0xc;
constexpr std::size_t ViewportUnionEntrySize = 0x14;
constexpr std::size_t ViewportUnionGuildKey = 4;
// 0x4d92a2: el bit 7 de la clave alta del 65 no es de la clave.
constexpr std::uint8_t ViewportKeyMask = 0x7f;
// 0x4d9e50: las entradas desde pkt[8], de 0x29 bytes.
constexpr std::size_t FirstUnionOffset = 8;
// EB: solo pkt[3] == 1 lee pkt[4].
constexpr std::uint8_t RefreshKind = 1;

// Un lector parado en `offset` del paquete entero (pkt[0] es el C1 o el C2).
std::optional<Core::ByteReader> At(const Network::Packet& packet, std::size_t offset)
{
    const auto bytes = packet.Bytes();
    if (offset > bytes.size())
        return std::nullopt;
    return Core::ByteReader(bytes.subspan(offset));
}

// Los campos del C1 desde pkt[3].
Core::ByteReader AfterTheOpcode(const Network::Packet& packet)
{
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
}

template <std::size_t Size> bool CopyTo(Core::ByteReader& reader, std::array<std::byte, Size>& target)
{
    const auto bytes = reader.ReadBytes(Size);
    if (!bytes)
        return false;
    std::ranges::copy(*bytes, target.begin());
    return true;
}

bool CopyMark(Core::ByteReader& reader, GuildMark& mark)
{
    const auto bytes = reader.ReadBytes(MarkBytes);
    if (!bytes)
        return false;
    std::ranges::transform(*bytes, mark.begin(), [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
    return true;
}

template <typename Result> Core::Expected<GuildPacket, ProtocolError> ReadResult(const Network::Packet& packet)
{
    Core::ByteReader reader = AfterTheOpcode(packet);
    const auto result = reader.ReadU8();
    if (!result)
        return Core::Unexpected(ProtocolError::TooShort);
    return GuildPacket{Result{*result}};
}

Core::Expected<GuildPacket, ProtocolError> ReadMemberList(const Network::Packet& packet)
{
    // 0x4d7b60: pkt[5], la cuenta; ((int)DWORD < 0) − 1 & DWORD deja el puntaje en 0 si es negativo (lo hace el
    // aplicador).
    auto head = At(packet, Network::MinPacketSize(Network::PacketType::C2) + 1);
    auto score = At(packet, MemberScoreOffset);
    auto name = At(packet, MemberNameOffset);
    if (!head || !score || !name)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto count = head->ReadU8();
    const auto value = score->ReadU32Le();
    MemberList list;
    if (!count || !value || !CopyTo(*name, list.name))
        return Core::Unexpected(ProtocolError::TooShort);
    list.count = *count;
    list.score = static_cast<std::int32_t>(*value);
    auto entries = At(packet, FirstMemberOffset);
    if (list.count != 0 && !entries)
        return Core::Unexpected(ProtocolError::TooShort);
    for (std::uint8_t i = 0; i < list.count; ++i)
    {
        MemberEntry entry;
        if (!CopyTo(*entries, entry.name))
            return Core::Unexpected(ProtocolError::TooShort);
        const auto number = entries->ReadU8();
        const auto server = entries->ReadU8();
        const auto status = entries->ReadU8();
        if (!number || !server || !status)
            return Core::Unexpected(ProtocolError::TooShort);
        entry.number = *number;
        entry.server = *server;
        entry.status = *status;
        list.members.push_back(entry);
    }
    static_assert(CharacterNameSize + 3 == MemberEntrySize);
    return GuildPacket{std::move(list)};
}

Core::Expected<GuildPacket, ProtocolError> ReadWarRequest(const Network::Packet& packet)
{
    Core::ByteReader reader = AfterTheOpcode(packet);
    WarRequest request;
    if (!CopyTo(reader, request.name))
        return Core::Unexpected(ProtocolError::TooShort);
    const auto type = reader.ReadU8();
    if (!type)
        return Core::Unexpected(ProtocolError::TooShort);
    request.type = *type;
    return GuildPacket{request};
}

Core::Expected<GuildPacket, ProtocolError> ReadWarStart(const Network::Packet& packet)
{
    Core::ByteReader reader = AfterTheOpcode(packet);
    WarStart start;
    if (!CopyTo(reader, start.name))
        return Core::Unexpected(ProtocolError::TooShort);
    const auto type = reader.ReadU8();
    const auto team = reader.ReadU8();
    if (!type || !team)
        return Core::Unexpected(ProtocolError::TooShort);
    start.type = *type;
    start.team = *team;
    return GuildPacket{start};
}

Core::Expected<GuildPacket, ProtocolError> ReadWarScore(const Network::Packet& packet)
{
    Core::ByteReader reader = AfterTheOpcode(packet);
    const auto ours = reader.ReadU8();
    const auto theirs = reader.ReadU8();
    if (!ours || !theirs)
        return Core::Unexpected(ProtocolError::TooShort);
    return GuildPacket{WarScore{.ours = *ours, .theirs = *theirs}};
}

Core::Expected<GuildPacket, ProtocolError> ReadKeyed(const Network::Packet& packet, bool viewport)
{
    Core::ByteReader reader = AfterTheOpcode(packet);
    const auto high = reader.ReadU8();
    const auto low = reader.ReadU8();
    if (!high || !low)
        return Core::Unexpected(ProtocolError::TooShort);
    // 0x4e42fb: el 5D le saca el bit 7 al byte alto; 0x4d74d6, el 50, no.
    constexpr std::uint8_t KeyHighMask = 0x7f;
    const std::uint8_t keyHigh = viewport ? static_cast<std::uint8_t>(*high & KeyHighMask) : *high;
    const auto key = static_cast<std::uint16_t>((keyHigh << 8) | *low);
    if (viewport)
        return GuildPacket{ViewportGuildDelete{key}};
    return GuildPacket{JoinRequest{key}};
}

Core::Expected<GuildPacket, ProtocolError> ReadGuildInfo(const Network::Packet& packet)
{
    auto key = At(packet, GuildInfoKeyOffset);
    auto rest = At(packet, GuildInfoNameOffset);
    if (!key || !rest)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto value = key->ReadU32Le();
    GuildInfo info;
    if (!value || !CopyTo(*rest, info.unionName) || !CopyTo(*rest, info.guildName) || !CopyMark(*rest, info.mark))
        return Core::Unexpected(ProtocolError::TooShort);
    info.key = static_cast<std::int32_t>(*value);
    return GuildPacket{info};
}

Core::Expected<GuildPacket, ProtocolError> ReadAssignResult(const Network::Packet& packet)
{
    Core::ByteReader reader = AfterTheOpcode(packet);
    const auto type = reader.ReadU8();
    const auto result = reader.ReadU8();
    if (!type || !result)
        return Core::Unexpected(ProtocolError::TooShort);
    return GuildPacket{AssignResult{.type = *type, .result = *result}};
}

Core::Expected<GuildPacket, ProtocolError> ReadRelationshipRequest(const Network::Packet& packet)
{
    Core::ByteReader reader = AfterTheOpcode(packet);
    const auto relation = reader.ReadU8();
    const auto request = reader.ReadU8();
    const auto high = reader.ReadU8();
    const auto low = reader.ReadU8();
    if (!relation || !request || !high || !low)
        return Core::Unexpected(ProtocolError::TooShort);
    return GuildPacket{
        RelationshipRequest{.relation = *relation, .request = *request, .keyHigh = *high, .keyLow = *low}};
}

Core::Expected<GuildPacket, ProtocolError> ReadRelationshipResult(const Network::Packet& packet)
{
    Core::ByteReader reader = AfterTheOpcode(packet);
    const auto relation = reader.ReadU8();
    const auto request = reader.ReadU8();
    const auto result = reader.ReadU8();
    // 0x4d9ae9: CONCAT11(pkt[6], pkt[7]).
    const auto key = reader.ReadU16Be();
    if (!relation || !request || !result || !key)
        return Core::Unexpected(ProtocolError::TooShort);
    return GuildPacket{RelationshipResult{.relation = *relation, .request = *request, .result = *result, .key = *key}};
}

Core::Expected<GuildPacket, ProtocolError> ReadUnionList(const Network::Packet& packet)
{
    auto head = At(packet, Network::MinPacketSize(Network::PacketType::C2));
    if (!head)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto count = head->ReadU8();
    const auto result = head->ReadU8();
    if (!count || !result)
        return Core::Unexpected(ProtocolError::TooShort);
    UnionList list{.count = *count, .result = *result, .entries = {}};
    // 0x4d9e5f: solo con el resultado en 1 y alguna entrada se leen.
    if (list.result != 1 || list.count == 0)
        return GuildPacket{std::move(list)};
    auto entries = At(packet, FirstUnionOffset);
    if (!entries)
        return Core::Unexpected(ProtocolError::TooShort);
    for (std::uint8_t i = 0; i < list.count; ++i)
    {
        UnionEntry entry;
        const auto first = entries->ReadU8();
        if (!first || !CopyMark(*entries, entry.mark) || !CopyTo(*entries, entry.name))
            return Core::Unexpected(ProtocolError::TooShort);
        entry.byte0 = *first;
        list.entries.push_back(entry);
    }
    return GuildPacket{std::move(list)};
}

Core::Expected<GuildPacket, ProtocolError> ReadUnionListRefresh(const Network::Packet& packet)
{
    Core::ByteReader reader = AfterTheOpcode(packet);
    const auto kind = reader.ReadU8();
    if (!kind)
        return Core::Unexpected(ProtocolError::TooShort);
    UnionListRefresh refresh{.kind = *kind, .result = 0};
    if (refresh.kind != RefreshKind)
        return GuildPacket{refresh};
    const auto result = reader.ReadU8();
    if (!result)
        return Core::Unexpected(ProtocolError::TooShort);
    refresh.result = *result;
    return GuildPacket{refresh};
}

// La cuenta de pkt[4] de un C2 (0x4d9289 y 0x4e44af).
std::optional<std::uint8_t> CountOf(const Network::Packet& packet)
{
    auto head = At(packet, Network::MinPacketSize(Network::PacketType::C2));
    if (!head)
        return std::nullopt;
    return head->ReadU8();
}

Core::Expected<GuildPacket, ProtocolError> ReadViewportGuild(const Network::Packet& packet)
{
    const auto count = CountOf(packet);
    if (!count)
        return Core::Unexpected(ProtocolError::TooShort);
    ViewportGuild guild;
    for (std::size_t i = 0; i < *count; ++i)
    {
        auto entry = At(packet, FirstViewportEntryOffset + i * ViewportGuildEntrySize);
        if (!entry)
            return Core::Unexpected(ProtocolError::TooShort);
        const auto guildKey = entry->ReadU32Le();
        const auto status = entry->ReadU8();
        const auto byte369 = entry->ReadU8();
        const auto relation = entry->ReadU8();
        const auto high = entry->ReadU8();
        const auto low = entry->ReadU8();
        if (!guildKey || !status || !byte369 || !relation || !high || !low)
            return Core::Unexpected(ProtocolError::TooShort);
        guild.entries.push_back(ViewportGuildEntry{
            .guildKey = static_cast<std::int32_t>(*guildKey),
            .status = *status,
            .byte369 = *byte369,
            .relation = *relation,
            .key = static_cast<std::uint16_t>(((*high & ViewportKeyMask) << 8) | *low),
        });
    }
    return GuildPacket{std::move(guild)};
}

Core::Expected<GuildPacket, ProtocolError> ReadViewportUnion(const Network::Packet& packet)
{
    const auto count = CountOf(packet);
    if (!count)
        return Core::Unexpected(ProtocolError::TooShort);
    ViewportUnion list;
    for (std::size_t i = 0; i < *count; ++i)
    {
        const std::size_t offset = FirstViewportEntryOffset + i * ViewportUnionEntrySize;
        auto key = At(packet, offset);
        auto rest = At(packet, offset + ViewportUnionGuildKey);
        if (!key || !rest)
            return Core::Unexpected(ProtocolError::TooShort);
        const auto value = key->ReadU16Be();
        const auto guildKey = rest->ReadU32Le();
        const auto relation = rest->ReadU8();
        ViewportUnionEntry entry;
        if (!value || !guildKey || !relation || !CopyTo(*rest, entry.unionName))
            return Core::Unexpected(ProtocolError::TooShort);
        entry.key = *value;
        entry.guildKey = static_cast<std::int32_t>(*guildKey);
        entry.relation = *relation;
        list.entries.push_back(entry);
    }
    return GuildPacket{std::move(list)};
}

Core::Expected<GuildPacket, ProtocolError> ReadC1(const Network::Packet& packet, std::uint8_t opcode)
{
    switch (opcode)
    {
    case JoinRequestOpcode:
        return ReadKeyed(packet, false);
    case JoinResultOpcode:
        return ReadResult<JoinResult>(packet);
    case LeaveResultOpcode:
        return ReadResult<LeaveResult>(packet);
    case MasterQuestionOpcode:
        return GuildPacket{MasterQuestion{}};
    case CreateResultOpcode:
        return ReadResult<CreateResult>(packet);
    case ViewportGuildDeleteOpcode:
        return ReadKeyed(packet, true);
    case WarRequestResultOpcode:
        return ReadResult<WarRequestResult>(packet);
    case WarRequestOpcode:
        return ReadWarRequest(packet);
    case WarStartOpcode:
        return ReadWarStart(packet);
    case WarEndOpcode:
        return ReadResult<WarEnd>(packet);
    case WarScoreOpcode:
        return ReadWarScore(packet);
    case GuildInfoOpcode:
        return ReadGuildInfo(packet);
    case AssignResultOpcode:
        return ReadAssignResult(packet);
    case RelationshipRequestOpcode:
        return ReadRelationshipRequest(packet);
    case RelationshipResultOpcode:
        return ReadRelationshipResult(packet);
    case UnionListRefreshOpcode:
        return ReadUnionListRefresh(packet);
    default:
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
}
} // namespace

Core::Expected<GuildPacket, ProtocolError> ReadGuildPacket(const Network::Packet& packet)
{
    const std::optional<std::uint8_t> opcode = packet.Opcode();
    if (!opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    if (packet.Type() == Network::PacketType::C2)
    {
        if (*opcode == MemberListOpcode)
            return ReadMemberList(packet);
        if (*opcode == UnionListOpcode)
            return ReadUnionList(packet);
        if (*opcode == ViewportGuildOpcode)
            return ReadViewportGuild(packet);
        if (*opcode == ViewportUnionOpcode)
            return ReadViewportUnion(packet);
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
    if (packet.Type() != Network::PacketType::C1)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return ReadC1(packet, *opcode);
}

Outgoing WriteJoinAnswer(bool accept, std::uint16_t key)
{
    return Xor32C1(
        Network::PacketWriter::C1(JoinResultOpcode).PutU8(accept ? std::uint8_t{1} : std::uint8_t{0}).PutU16Be(key));
}

Outgoing WriteMemberListRequest()
{
    return ClearC1(Network::PacketWriter::C1(MemberListOpcode));
}

Outgoing WriteMasterAnswer(std::uint8_t answer)
{
    return Xor32C1(Network::PacketWriter::C1(MasterQuestionOpcode).PutU8(answer));
}

// 0x6553bf..0x6553e8: el byte que sigue al 55, siempre 0.
constexpr std::uint8_t CreateKindByte = 0;

Outgoing WriteCreate(const GuildName& name, const GuildMark& mark)
{
    std::array<std::byte, MarkBytes> markBytes{};
    std::ranges::transform(mark, markBytes.begin(), [](std::uint8_t value) { return std::byte{value}; });
    return Xor32C1(Network::PacketWriter::C1(CreateOpcode).PutU8(CreateKindByte).PutBytes(name).PutBytes(markBytes));
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
    return Xor32C1(Network::PacketWriter::C1(JoinRequestOpcode)
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

Outgoing WriteAssignStatus(std::uint8_t type, std::uint8_t status, const CharacterName& name)
{
    return Xor32C1(Network::PacketWriter::C1(AssignResultOpcode).PutU8(type).PutU8(status).PutBytes(name));
}
} // namespace Client::Protocol::Guild
