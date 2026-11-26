#include "Protocol/ChaosMix/JewelMix.h"

#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Opcodes.h"

#include <cstdint>

namespace Client::Protocol::ChaosMix
{
Outgoing WriteJewelMix(std::uint8_t kind, std::uint8_t bundle)
{
    return Xor32C1(Network::PacketWriter::C1(JewelMixOpcode, JewelMix::MixSubcode).PutU8(kind).PutU8(bundle));
}

Outgoing WriteJewelUnmix(std::uint8_t kind, std::uint8_t level, std::uint8_t slot)
{
    return Xor32C1(
        Network::PacketWriter::C1(JewelMixOpcode, JewelMix::UnmixSubcode).PutU8(kind).PutU8(level).PutU8(slot));
}
} // namespace Client::Protocol::ChaosMix
