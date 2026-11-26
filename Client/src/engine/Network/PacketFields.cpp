#include "Network/PacketFields.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Network
{
Core::Expected<Core::ByteReader, ProtocolError> FieldsAfterSubcode(const Network::Packet& packet,
                                                                   const PacketKind& kind)
{
    // Opcode() is std::nullopt in a C3/C4, which carries it encrypted: also another packet.
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
    if (Subcode{*subcode} != kind.subcode)
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
} // namespace Client::Network
