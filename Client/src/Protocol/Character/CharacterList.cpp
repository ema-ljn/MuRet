#include "Protocol/Character/CharacterList.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Character/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Client::Protocol::Character
{
namespace
{
// La lista es un C1: con cinco personajes mide 7 + 5 · 0x22 = 177.
constexpr PacketKind ListKind{Network::PacketType::C1, CharacterOpcode, ListSubcode};

// Dónde va cada campo en una entrada.
constexpr std::size_t NameOffset = 1;
constexpr std::size_t LevelOffset = 0xC;
constexpr std::size_t FlagsOffset = 0xE;
constexpr std::size_t ClassOffset = 0xF;
constexpr std::size_t EquipmentOffset = 0x10;
constexpr std::size_t GuildStatusOffset = 0x21;

// Una entrada, de sus 0x22 bytes: cada lectura entra.
CharacterEntry ReadEntry(std::span<const std::byte, EntrySize> bytes)
{
    CharacterEntry entry;
    entry.slot = std::to_integer<std::uint8_t>(bytes[0]);
    std::ranges::copy(bytes.subspan<NameOffset, NameSize>(), entry.name.begin());
    Core::ByteReader level(bytes.subspan<LevelOffset, 2>());
    entry.level = *level.ReadU16Le();
    entry.flags = std::to_integer<std::uint8_t>(bytes[FlagsOffset]);
    entry.classByte = std::to_integer<std::uint8_t>(bytes[ClassOffset]);
    std::ranges::copy(bytes.subspan<EquipmentOffset, EquipmentSize>(), entry.equipment.begin());
    entry.guildStatus = std::to_integer<std::uint8_t>(bytes[GuildStatusOffset]);
    return entry;
}
} // namespace

Outgoing WriteCharacterListRequest()
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode).PutU8(ListSubcode));
}

Core::Expected<CharacterList, ProtocolError> ReadCharacterList(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, ListKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    Core::ByteReader& reader = fields.Value();
    const std::optional<std::uint8_t> creatableClasses = reader.ReadU8();
    const std::optional<std::uint8_t> notice = reader.ReadU8();
    const std::optional<std::uint8_t> count = reader.ReadU8();
    if (!count || *count > reader.Remaining() / EntrySize)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    CharacterList list{*creatableClasses, *notice, {}};
    list.characters.reserve(*count);
    // La cantidad ya se comparó con lo que queda: cada entrada entra.
    for (std::size_t k = 0; k < *count; ++k)
    {
        const std::optional<std::span<const std::byte>> bytes = reader.ReadBytes(EntrySize);
        CharacterEntry entry = ReadEntry(bytes->first<EntrySize>());
        if (entry.slot <= MaxCharacterSlot)
        {
            list.characters.push_back(entry);
        }
    }
    return list;
}
} // namespace Client::Protocol::Character
