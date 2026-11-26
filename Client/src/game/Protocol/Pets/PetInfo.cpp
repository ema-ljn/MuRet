#include "Protocol/Pets/PetInfo.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <cstdint>
#include <optional>

namespace Client::Protocol::Pets
{
namespace
{
using Pet = Network::Field<std::uint8_t, 3>;
using Kind = Network::Field<std::uint8_t, 4>;
using Slot = Network::Field<std::uint8_t, 5>;
using Level = Network::Field<std::uint8_t, 6>;
using Experience = Network::Field<std::uint32_t, 8>;
constexpr Network::PacketLayout PetInfoReplyLayout{
    {Network::PacketType::C1, PetInfoOpcode, std::nullopt}, Experience::End, 0x4e4ca1};
} // namespace

Outgoing WritePetInfoRequest(const PetInfoRequest& request)
{
    return Xor32C1(
        Network::PacketWriter::C1(PetInfoOpcode).PutU8(request.pet).PutU8(request.place).PutU8(request.slot));
}

Core::Expected<PetInfoReply, Network::ProtocolError> ReadPetInfoReply(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PetInfoReplyLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return PetInfoReply{.pet = reader.Read<Pet>(),
                                                       .kind = reader.Read<Kind>(),
                                                       .slot = reader.Read<Slot>(),
                                                       .level = reader.Read<Level>(),
                                                       .experience = reader.Read<Experience>()};
                               });
}
} // namespace Client::Protocol::Pets
