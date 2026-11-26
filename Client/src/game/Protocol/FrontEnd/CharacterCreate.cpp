#include "Protocol/FrontEnd/CharacterCreate.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Protocol::FrontEnd
{
namespace
{
using CreateResult = Network::Field<std::uint8_t, 4>;
using CreatedName = Network::Field<NameBytes, 5>;
using CreatedSlot = Network::Field<std::uint8_t, 0xf>;
using CreatedLevel = Network::Field<std::uint16_t, 0x10>;
using CreatedClass = Network::Field<std::uint8_t, 0x12>;
constexpr Network::PacketLayout CreateResultLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::CreateSubcode}, CreateResult::End, 0x4c2290};

// 0x63d990: DAT_07437224 * 16 + DAT_07437225.
constexpr std::uint8_t ClassShift = 4;
} // namespace

Outgoing WriteCharacterCreate(std::span<const std::byte, NameSize> name, std::uint8_t characterClass)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode, Character::CreateSubcode)
                       .PutBytes(NameUpToItsEnd(name))
                       .PutU8(static_cast<std::uint8_t>(characterClass << ClassShift)));
}

Core::Expected<CharacterCreateResult, Network::ProtocolError> ReadCharacterCreateResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, CreateResultLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   CharacterCreateResult created{.result = reader.Read<CreateResult>()};
                                   if (created.result == CharacterCreated)
                                   {
                                       created.name = reader.Read<CreatedName>();
                                       created.slot = reader.Read<CreatedSlot>();
                                       created.level = reader.Read<CreatedLevel>();
                                       created.classByte = reader.Read<CreatedClass>();
                                   }
                                   return created;
                               });
}
} // namespace Client::Protocol::FrontEnd
