#include "Protocol/Character/CharacterCreate.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/CharacterName.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::Character
{
namespace
{
constexpr PacketKind CreateKind{Network::PacketType::C1, CharacterOpcode, CreateSubcode};
// 0x63d990: DAT_07437224 · 16 + DAT_07437225.
constexpr std::uint8_t ClassShift = 4;
} // namespace

Outgoing WriteCharacterCreate(std::span<const std::byte, NameSize> name, std::uint8_t characterClass)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode)
                       .PutU8(CreateSubcode)
                       .PutBytes(NameUpToItsEnd(name))
                       .PutU8(static_cast<std::uint8_t>(characterClass << ClassShift)));
}

Core::Expected<CharacterCreateResult, ProtocolError> ReadCharacterCreateResult(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, CreateKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    Core::ByteReader& reader = fields.Value();
    const std::optional<std::uint8_t> result = reader.ReadU8();
    if (!result)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    CharacterCreateResult created;
    created.result = *result;
    if (*result != CharacterCreated)
    {
        return created;
    }
    const std::optional<std::span<const std::byte>> name = reader.ReadBytes(NameSize);
    const std::optional<std::uint8_t> slot = reader.ReadU8();
    const std::optional<std::uint16_t> level = reader.ReadU16Le();
    const std::optional<std::uint8_t> classByte = reader.ReadU8();
    if (!classByte)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    std::ranges::copy(*name, created.name.begin());
    created.slot = *slot;
    created.level = *level;
    created.classByte = *classByte;
    return created;
}
} // namespace Client::Protocol::Character
