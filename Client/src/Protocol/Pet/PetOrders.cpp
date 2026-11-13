#include "Protocol/Pet/PetOrders.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>

namespace Client::Protocol::Pet
{
namespace
{
// Un C1 de `opcode`, el lector desde pkt[4] (pkt[3] no lo lee nadie).
Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, std::uint8_t opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    Core::ByteReader reader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
    if (!reader.ReadU8())
        return Core::Unexpected(ProtocolError::TooShort);
    return reader;
}
} // namespace

Core::Expected<PetOrder, ProtocolError> ReadPetOrder(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, PetOrderOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto order = reader.ReadU8();
    const auto key = reader.ReadU16Be();
    if (!order || !key)
        return Core::Unexpected(ProtocolError::TooShort);
    return PetOrder{*order, *key};
}

Core::Expected<PetAttack, ProtocolError> ReadPetAttack(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, PetAttackOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto kind = reader.ReadU8();
    const auto owner = reader.ReadU16Be();
    const auto target = reader.ReadU16Be();
    if (!kind || !owner || !target)
        return Core::Unexpected(ProtocolError::TooShort);
    return PetAttack{*kind, *owner, *target};
}
} // namespace Client::Protocol::Pet
