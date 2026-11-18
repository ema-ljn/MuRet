#include "Protocol/JewelMix.h"

#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"

#include <cstdint>

namespace Client::Protocol::JewelMix
{
Outgoing WriteJewelMix(std::uint8_t kind, std::uint8_t bundle)
{
    return Xor32C1(Network::PacketWriter::C1(JewelMixOpcode).PutU8(MixSubcode).PutU8(kind).PutU8(bundle));
}

Outgoing WriteJewelUnmix(std::uint8_t kind, std::uint8_t level, std::uint8_t slot)
{
    return Xor32C1(Network::PacketWriter::C1(JewelMixOpcode).PutU8(UnmixSubcode).PutU8(kind).PutU8(level).PutU8(slot));
}
} // namespace Client::Protocol::JewelMix
