#include "Protocol/OptionData.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::OptionData
{
Core::Expected<OptionData, ProtocolError> ReadOptionData(const Network::Packet& packet)
{
    auto fields =
        FieldsAfterSubcode(packet, PacketKind{Network::PacketType::C1, Character::CharacterOpcode, OptionDataSubcode});
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    OptionData data;
    for (std::uint8_t& key : data.skillKeys)
    {
        const auto read = reader.ReadU8();
        if (!read)
            return Core::Unexpected(ProtocolError::TooShort);
        key = *read;
    }
    const auto flags = reader.ReadU8();
    if (!flags)
        return Core::Unexpected(ProtocolError::TooShort);
    data.flags = *flags;
    for (std::uint8_t& slot : data.quickSlots)
    {
        const auto read = reader.ReadU8();
        if (!read)
            return Core::Unexpected(ProtocolError::TooShort);
        slot = *read;
    }
    const auto chatWindow = reader.ReadU8();
    if (!chatWindow)
        return Core::Unexpected(ProtocolError::TooShort);
    data.chatWindow = *chatWindow;
    return data;
}

Outgoing WriteOptionData(const OptionBody& body)
{
    return Xor32C1(Network::PacketWriter::C1(Character::CharacterOpcode)
                       .PutU8(OptionDataSubcode)
                       .PutBytes(std::as_bytes(std::span<const std::uint8_t>(body))));
}
} // namespace Client::Protocol::OptionData
