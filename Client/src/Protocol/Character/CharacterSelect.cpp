#include "Protocol/Character/CharacterSelect.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/CharacterSelectResult.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <optional>
#include <span>

namespace Client::Protocol::Character
{
namespace
{
constexpr PacketKind SelectKind{Network::PacketType::C1, CharacterOpcode, SelectSubcode};
// 04-protocolo, "La entrada al mundo": 0x34 bytes en total, 0x30 después del subcódigo.
// Q-C144: Leadership ocupa los últimos dos; los WORD fuera de ese paquete no son campos.
constexpr std::size_t SelectResultFieldsSize = 0x30;
constexpr std::size_t SelectResultPaddingSize = 2; // pkt[0x26..0x27], antes del zen.
} // namespace

Outgoing WriteCharacterSelect(std::span<const std::byte, NameSize> name)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode).PutU8(SelectSubcode).PutBytes(name));
}

Core::Expected<CharacterSelectResult, ProtocolError> ReadCharacterSelectResult(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, SelectKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    Core::ByteReader& reader = fields.Value();
    if (reader.Remaining() < SelectResultFieldsSize)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    CharacterSelectResult result;
    result.x = *reader.ReadU8();
    result.y = *reader.ReadU8();
    result.map = *reader.ReadU8();
    result.direction = *reader.ReadU8();
    result.experience = *reader.ReadU32Le();
    result.nextExperience = *reader.ReadU32Le();
    result.levelUpPoints = *reader.ReadU16Le();
    result.strength = *reader.ReadU16Le();
    result.agility = *reader.ReadU16Le();
    result.vitality = *reader.ReadU16Le();
    result.energy = *reader.ReadU16Le();
    result.life = *reader.ReadU16Le();
    result.maxLife = *reader.ReadU16Le();
    result.mana = *reader.ReadU16Le();
    result.maxMana = *reader.ReadU16Le();
    result.bp = *reader.ReadU16Le();
    result.maxBp = *reader.ReadU16Le();
    static_cast<void>(reader.ReadBytes(SelectResultPaddingSize));
    result.zen = *reader.ReadU32Le();
    result.pk = *reader.ReadU8();
    result.controlCode = *reader.ReadU8();
    result.addedPoints = *reader.ReadU16Le();
    result.maxAddedPoints = *reader.ReadU16Le();
    result.leadership = *reader.ReadU16Le();
    return result;
}
} // namespace Client::Protocol::Character
