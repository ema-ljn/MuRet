#include "Protocol/Character/PkLevel.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Character/Opcodes.h"
#include "Network/PacketFields.h"
#include "Network/ProtocolError.h"

namespace Client::Protocol::Character
{
Core::Expected<PkLevel, Network::ProtocolError> ReadPkLevel(const Network::Packet& packet)
{
    constexpr Network::PacketKind PkLevelKind{Network::PacketType::C1, CharacterOpcode, PkLevelSubcode};
    auto fields = Network::FieldsAfterSubcode(packet, PkLevelKind);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto key = reader.ReadU16Be();
    const auto level = reader.ReadU8();
    if (!key || !level)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return PkLevel{.key = *key, .level = *level};
}
} // namespace Client::Protocol::Character
