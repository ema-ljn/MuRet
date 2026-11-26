#include "Protocol/Pets/PetOrders.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::Protocol::Pets
{
namespace
{
// Nobody reads pkt[3].
using Order = Network::Field<std::uint8_t, 4>;
using OrderKey = Network::Field<std::uint16_t, 5, std::endian::big>;
constexpr Network::PacketLayout PetOrderLayout{
    {Network::PacketType::C1, PetOrderOpcode, std::nullopt}, OrderKey::End, 0x4e4c0a};

using AttackKind = Network::Field<std::uint8_t, 4>;
using AttackOwner = Network::Field<std::uint16_t, 5, std::endian::big>;
using AttackTarget = Network::Field<std::uint16_t, 7, std::endian::big>;
constexpr Network::PacketLayout PetAttackLayout{
    {Network::PacketType::C1, PetAttackOpcode, std::nullopt}, AttackTarget::End, 0x4e4c42};
} // namespace

Core::Expected<PetOrder, Network::ProtocolError> ReadPetOrder(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PetOrderLayout, [](Network::LayoutReader& reader)
                               { return PetOrder{reader.Read<Order>(), reader.Read<OrderKey>()}; });
}

Core::Expected<PetAttack, Network::ProtocolError> ReadPetAttack(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, PetAttackLayout, [](Network::LayoutReader& reader)
        { return PetAttack{reader.Read<AttackKind>(), reader.Read<AttackOwner>(), reader.Read<AttackTarget>()}; });
}

Outgoing WritePetCommand(const PetCommand& command)
{
    return Xor32C1(
        Network::PacketWriter::C1(PetOrderOpcode).PutU8(command.pet).PutU8(command.command).PutU16Be(command.key));
}
} // namespace Client::Protocol::Pets
