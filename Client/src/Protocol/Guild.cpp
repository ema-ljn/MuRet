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
    case GuildInfoOpcode:
        return ReadGuildInfo(packet);
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

Outgoing WriteCreate(const GuildName& name, const GuildMark& mark)
{
    std::array<std::byte, MarkBytes> markBytes{};
    std::ranges::transform(mark, markBytes.begin(), [](std::uint8_t value) { return std::byte{value}; });
    return Xor32C1(Network::PacketWriter::C1(CreateOpcode).PutBytes(name).PutBytes(markBytes));
}

} // namespace Client::Protocol::Guild
