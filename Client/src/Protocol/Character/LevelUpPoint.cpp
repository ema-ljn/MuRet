#include "Protocol/Character/LevelUpPoint.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Network/PacketFields.h"
#include "Network/ProtocolError.h"

#include <cstdint>

namespace Client::Protocol::Character
{
Outgoing WriteLevelUpPoint(std::uint8_t stat)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode).PutU8(LevelUpPointSubcode).PutU8(stat));
}

Core::Expected<LevelUpPointResult, Network::ProtocolError> ReadLevelUpPointResult(const Network::Packet& packet)
{
    constexpr Network::PacketKind LevelUpPointKind{Network::PacketType::C1, CharacterOpcode, LevelUpPointSubcode};
    auto fields = Network::FieldsAfterSubcode(packet, LevelUpPointKind);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto result = reader.ReadU8();
    if (!result)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    LevelUpPointResult reply{.result = *result};
    constexpr std::uint8_t AddedBits = 0xf0;
    if ((reply.result & AddedBits) == 0)
        return reply;
    const auto padding = reader.ReadU8();
    const auto lifeOrMana = reader.ReadU16Le();
    const auto bp = reader.ReadU16Le();
    if (!padding || !lifeOrMana || !bp)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    reply.maxLifeOrMana = *lifeOrMana;
    reply.maxBp = *bp;
    return reply;
}
} // namespace Client::Protocol::Character
