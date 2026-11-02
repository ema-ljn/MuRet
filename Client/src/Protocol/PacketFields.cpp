#include "Protocol/PacketFields.h"

#include <cstdint>
#include <optional>

namespace Client::Protocol
{
Core::Expected<Core::ByteReader, ProtocolError> FieldsAfterSubcode(const Network::Packet& packet,
                                                                   const PacketKind& kind)
{
    // Opcode() es std::nullopt en un C3/C4, que lo lleva cifrado: también es otro paquete.
    if (packet.Type() != kind.type || packet.Opcode() != kind.opcode)
    {
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
    Core::ByteReader fields(packet.Bytes().subspan(Network::MinPacketSize(kind.type)));
    const std::optional<std::uint8_t> subcode = fields.ReadU8();
    if (!subcode)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    if (*subcode != kind.subcode)
    {
        return Core::Unexpected(ProtocolError::WrongPacket);
    }
    return fields;
}

std::string TextField(std::span<const std::byte> field)
{
    std::string text;
    for (const std::byte value : field)
    {
        if (value == std::byte{0})
        {
            break;
        }
        text.push_back(std::to_integer<char>(value));
    }
    return text;
}
} // namespace Client::Protocol
