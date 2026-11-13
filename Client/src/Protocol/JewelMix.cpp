#include "Protocol/JewelMix.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Protocol::JewelMix
{
namespace
{
// pkt[3] y pkt[4] de un C1.
constexpr std::size_t SubcodeField = 3;
constexpr std::size_t ResultField = 4;
} // namespace

Outgoing WriteJewelMix(std::uint8_t kind, std::uint8_t bundle)
{
    return Xor32C1(Network::PacketWriter::C1(JewelMixOpcode).PutU8(MixSubcode).PutU8(kind).PutU8(bundle));
}

Outgoing WriteJewelUnmix(std::uint8_t kind, std::uint8_t level, std::uint8_t slot)
{
    return Xor32C1(Network::PacketWriter::C1(JewelMixOpcode).PutU8(UnmixSubcode).PutU8(kind).PutU8(level).PutU8(slot));
}

Core::Expected<JewelMixAnswer, ProtocolError> ReadJewelMixAnswer(const Network::Packet& packet)
{
    if (packet.Opcode() != JewelMixOpcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    const std::span<const std::byte> bytes = packet.Bytes();
    if (bytes.size() <= SubcodeField)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto subcode = std::to_integer<std::uint8_t>(bytes[SubcodeField]);
    if (subcode != MixSubcode && subcode != UnmixSubcode)
        return JewelMixAnswer{.subcode = subcode, .result = 0};
    if (bytes.size() <= ResultField)
        return Core::Unexpected(ProtocolError::TooShort);
    return JewelMixAnswer{.subcode = subcode, .result = std::to_integer<std::uint8_t>(bytes[ResultField])};
}
} // namespace Client::Protocol::JewelMix
