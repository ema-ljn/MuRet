#include "Protocol/Pet/PetInfo.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

namespace Client::Protocol::Pet
{
Outgoing WritePetInfoRequest(const PetInfoRequest& request)
{
    return Xor32C1(
        Network::PacketWriter::C1(PetInfoOpcode).PutU8(request.pet).PutU8(request.place).PutU8(request.slot));
}

Core::Expected<PetInfoReply, ProtocolError> ReadPetInfoReply(const Network::Packet& packet)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != PetInfoOpcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    Core::ByteReader reader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
    const auto pet = reader.ReadU8();
    const auto kind = reader.ReadU8();
    const auto slot = reader.ReadU8();
    const auto level = reader.ReadU8();
    const auto padding = reader.ReadU8();
    const auto experience = reader.ReadU32Le();
    if (!pet || !kind || !slot || !level || !padding || !experience)
        return Core::Unexpected(ProtocolError::TooShort);
    return PetInfoReply{.pet = *pet, .kind = *kind, .slot = *slot, .level = *level, .experience = *experience};
}
} // namespace Client::Protocol::Pet
