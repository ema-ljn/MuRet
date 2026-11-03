#include "Protocol/Pet/PetInfo.h"

#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"

namespace Client::Protocol::Pet
{
Outgoing WritePetInfoRequest(const PetInfoRequest& request)
{
    return Xor32C1(
        Network::PacketWriter::C1(PetInfoOpcode).PutU8(request.pet).PutU8(request.place).PutU8(request.slot));
}
} // namespace Client::Protocol::Pet
