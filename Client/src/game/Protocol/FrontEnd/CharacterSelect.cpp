#include "Protocol/FrontEnd/CharacterSelect.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/FrontEnd/CharacterName.h"
#include "Protocol/FrontEnd/CharacterSelectResult.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Protocol::FrontEnd
{
namespace
{
using X = Network::Field<std::uint8_t, 4>;
using Y = Network::Field<std::uint8_t, 5>;
using Map = Network::Field<std::uint8_t, 6>;
using Direction = Network::Field<std::uint8_t, 7>;
using Experience = Network::Field<std::uint32_t, 8>;
using NextExperience = Network::Field<std::uint32_t, 0xc>;
using LevelUpPoints = Network::Field<std::uint16_t, 0x10>;
using Strength = Network::Field<std::uint16_t, 0x12>;
using Agility = Network::Field<std::uint16_t, 0x14>;
using Vitality = Network::Field<std::uint16_t, 0x16>;
using Energy = Network::Field<std::uint16_t, 0x18>;
using Life = Network::Field<std::uint16_t, 0x1a>;
using MaxLife = Network::Field<std::uint16_t, 0x1c>;
using Mana = Network::Field<std::uint16_t, 0x1e>;
using MaxMana = Network::Field<std::uint16_t, 0x20>;
using Bp = Network::Field<std::uint16_t, 0x22>;
using MaxBp = Network::Field<std::uint16_t, 0x24>;
using Zen = Network::Field<std::uint32_t, 0x28>;
using Pk = Network::Field<std::uint8_t, 0x2c>;
using ControlCode = Network::Field<std::uint8_t, 0x2d>;
using AddedPoints = Network::Field<std::uint16_t, 0x2e>;
using MaxAddedPoints = Network::Field<std::uint16_t, 0x30>;
// Q-C144: the original reads it from two bytes the GameServer's struct leaves as padding.
using Leadership = Network::Field<std::uint16_t, 0x32>;
constexpr Network::PacketLayout SelectResultLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::SelectSubcode}, Leadership::End, 0x4c3940};
} // namespace

Outgoing WriteCharacterSelect(std::span<const std::byte, NameSize> name)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode, Character::SelectSubcode).PutBytes(name));
}

Core::Expected<CharacterSelectResult, Network::ProtocolError> ReadCharacterSelectResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, SelectResultLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return CharacterSelectResult{.x = reader.Read<X>(),
                                                                .y = reader.Read<Y>(),
                                                                .map = reader.Read<Map>(),
                                                                .direction = reader.Read<Direction>(),
                                                                .experience = reader.Read<Experience>(),
                                                                .nextExperience = reader.Read<NextExperience>(),
                                                                .levelUpPoints = reader.Read<LevelUpPoints>(),
                                                                .strength = reader.Read<Strength>(),
                                                                .agility = reader.Read<Agility>(),
                                                                .vitality = reader.Read<Vitality>(),
                                                                .energy = reader.Read<Energy>(),
                                                                .life = reader.Read<Life>(),
                                                                .maxLife = reader.Read<MaxLife>(),
                                                                .mana = reader.Read<Mana>(),
                                                                .maxMana = reader.Read<MaxMana>(),
                                                                .bp = reader.Read<Bp>(),
                                                                .maxBp = reader.Read<MaxBp>(),
                                                                .zen = reader.Read<Zen>(),
                                                                .pk = reader.Read<Pk>(),
                                                                .controlCode = reader.Read<ControlCode>(),
                                                                .addedPoints = reader.Read<AddedPoints>(),
                                                                .maxAddedPoints = reader.Read<MaxAddedPoints>(),
                                                                .leadership = reader.Read<Leadership>()};
                               });
}
} // namespace Client::Protocol::FrontEnd
