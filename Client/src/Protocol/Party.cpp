#include "Protocol/Party.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>

namespace Client::Protocol::Party
{
namespace
{
// Los dos bytes de un miembro del 42 que 0x4d67e0 no copia (+0xe y +0xf).
constexpr std::size_t MemberGap = 2;

Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, std::uint8_t opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
}

std::optional<PartyMember> ReadMember(Core::ByteReader& reader)
{
    const auto name = reader.ReadBytes(Character::NameSize);
    const auto number = reader.ReadU8();
    const auto map = reader.ReadU8();
    const auto x = reader.ReadU8();
    const auto y = reader.ReadU8();
    const auto gap = reader.ReadBytes(MemberGap);
    const auto life = reader.ReadU32Le();
    const auto maxLife = reader.ReadU32Le();
    if (!name || !number || !map || !x || !y || !gap || !life || !maxLife)
        return std::nullopt;
    PartyMember member{.number = *number, .map = *map, .x = *x, .y = *y, .life = *life, .maxLife = *maxLife};
    std::ranges::copy(*name, member.name.begin());
    return member;
}
} // namespace

Outgoing WritePartyListRequest()
{
    return ClearC1(Network::PacketWriter::C1(PartyListOpcode));
}

Core::Expected<PartyRequest, ProtocolError> ReadPartyRequest(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, RequestOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto key = fields.Value().ReadU16Be();
    if (!key)
        return Core::Unexpected(ProtocolError::TooShort);
    return PartyRequest{*key};
}

Core::Expected<PartyRequestResult, ProtocolError> ReadPartyRequestResult(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, RequestResultOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    const auto result = fields.Value().ReadU8();
    if (!result)
        return Core::Unexpected(ProtocolError::TooShort);
    return PartyRequestResult{*result};
}

Core::Expected<PartyList, ProtocolError> ReadPartyList(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, PartyListOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    const auto count = reader.ReadU8();
    if (!result || !count)
        return Core::Unexpected(ProtocolError::TooShort);
    PartyList list{.result = *result, .members = {}};
    list.members.reserve(*count);
    for (std::uint8_t at = 0; at < *count; ++at)
    {
        const auto member = ReadMember(reader);
        if (!member)
            return Core::Unexpected(ProtocolError::TooShort);
        list.members.push_back(*member);
    }
    return list;
}

Core::Expected<PartyLeft, ProtocolError> ReadPartyLeft(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, LeaveOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    return PartyLeft{};
}

Core::Expected<PartyLife, ProtocolError> ReadPartyLife(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, LifeOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto count = reader.ReadU8();
    if (!count)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto bytes = reader.ReadBytes(*count);
    if (!bytes)
        return Core::Unexpected(ProtocolError::TooShort);
    PartyLife life{};
    life.entries.reserve(bytes->size());
    std::ranges::transform(*bytes, std::back_inserter(life.entries),
                           [](std::byte b) { return std::to_integer<std::uint8_t>(b); });
    return life;
}

Outgoing WritePartyRequest(std::uint16_t key)
{
    return EncryptedC1(Network::PacketWriter::C1(RequestOpcode).PutU16Be(key));
}

Outgoing WritePartyRequestResult(std::uint8_t accepted, std::uint16_t key)
{
    return EncryptedC1(Network::PacketWriter::C1(RequestResultOpcode).PutU8(accepted).PutU16Be(key));
}

Outgoing WritePartyLeave(std::uint8_t number)
{
    return Xor32C1(Network::PacketWriter::C1(LeaveOpcode).PutU8(number));
}
} // namespace Client::Protocol::Party
