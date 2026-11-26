#include "Protocol/FrontEnd/CharacterList.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Network/ReadList.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Protocol::FrontEnd
{
namespace
{
using CreatableClasses = Network::Field<std::uint8_t, 4>;
using ListNotice = Network::Field<std::uint8_t, 5>;
using EntryCount = Network::Field<std::uint8_t, 6>;
// With five characters the list measures 7 + 5 * 0x22 = 177 bytes.
constexpr Network::PacketLayout ListLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::ListSubcode}, EntryCount::End, 0x4c20a0};

constexpr std::size_t NameOffset = 1;
constexpr std::size_t LevelOffset = 0xC;
constexpr std::size_t FlagsOffset = 0xE;
constexpr std::size_t ClassOffset = 0xF;
constexpr std::size_t EquipmentOffset = 0x10;
constexpr std::size_t GuildStatusOffset = 0x21;

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
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode, Character::ListSubcode));
}

Core::Expected<CharacterList, Network::ProtocolError> ReadCharacterList(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, ListLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   CharacterList list{reader.Read<CreatableClasses>(), reader.Read<ListNotice>(), {}};
                                   for (const CharacterEntry& entry : Network::ReadList<EntrySize>(
                                            reader, EntryCount::End, reader.Read<EntryCount>(), ReadEntry))
                                   {
                                       if (entry.slot <= MaxCharacterSlot)
                                       {
                                           list.characters.push_back(entry);
                                       }
                                   }
                                   return list;
                               });
}
} // namespace Client::Protocol::FrontEnd
