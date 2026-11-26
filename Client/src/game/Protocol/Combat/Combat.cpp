#include "Protocol/Combat/Combat.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <bit>
#include <cstdint>
#include <optional>

namespace Client::Protocol::Combat
{
namespace
{
constexpr std::uint16_t KeyMask = 0x7fff;
constexpr std::uint16_t ShockMask = 0x8000;

using KillKey = Network::Field<std::uint16_t, 3, std::endian::big>;
using KillAmount = Network::Field<std::uint16_t, 5, std::endian::big>;
using KillDamage = Network::Field<std::uint16_t, 7, std::endian::big>;
constexpr Network::PacketLayout KillExperienceLayout{
    {Network::PacketType::C1, KillExperienceOpcode, std::nullopt}, KillDamage::End, 0x4cf200};

using AmountHigh = Network::Field<std::uint16_t, 6>;
using AmountLow = Network::Field<std::uint16_t, 8>;
using ExtendedDamage = Network::Field<std::uint16_t, 0xa, std::endian::big>;
constexpr Network::PacketLayout ExtendedKillExperienceLayout{
    {Network::PacketType::C1, ExtendedKillExperienceOpcode, std::nullopt}, ExtendedDamage::End, 0x4cfce0};

using DeathSkill = Network::Field<std::uint8_t, 5>;
using Killer = Network::Field<std::uint16_t, 6, std::endian::big>;
constexpr Network::PacketLayout DeathLayout{
    {Network::PacketType::C1, DeathOpcode, std::nullopt}, KillKey::End, 0x4d0a30};

// What arrived of `F`, or 0.
template <typename F> typename F::Type IfArrived(Network::LayoutReader& reader)
{
    return reader.Size() >= F::End ? reader.Read<F>() : typename F::Type{};
}

KillExperience ExperienceOf(std::uint16_t key, std::uint32_t amount, std::uint16_t damage)
{
    return KillExperience{static_cast<std::uint16_t>(key & KeyMask), (key & ShockMask) != 0, amount, damage};
}
} // namespace

Outgoing WriteAttack(std::uint16_t key, std::uint8_t action, std::uint8_t direction)
{
    return Xor32C1(Network::PacketWriter::C1(AttackOpcode).PutU16Be(key).PutU8(action).PutU8(direction));
}

Outgoing WriteElfCell(std::uint8_t x, std::uint8_t y)
{
    return Xor32C1(Network::PacketWriter::C1(ElfCellOpcode).PutU8(x).PutU8(y));
}

Outgoing WriteClearKillReport(Network::Opcode opcode)
{
    return EncryptedC1(
        Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode).PutU8(0).PutU8(Network::ByteOf(opcode)));
}

// 0x4cf2b4..0x4cf2f0: the key, the experience and the damage, two bytes each, high first.
Core::Expected<KillExperience, Network::ProtocolError> ReadKillExperience(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, KillExperienceLayout, [](Network::LayoutReader& reader)
        { return ExperienceOf(reader.Read<KillKey>(), reader.Read<KillAmount>(), reader.Read<KillDamage>()); });
}

// 0x4d0439..0x4d047a: past the key it skips +5 (the natural padding of the GameServer's WORD) and reads the two WORDs
// little endian, ExpH * 65536 + ExpL.
Core::Expected<KillExperience, Network::ProtocolError> ReadExtendedKillExperience(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, ExtendedKillExperienceLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   const std::uint32_t amount =
                                       (std::uint32_t{reader.Read<AmountHigh>()} << 16) + reader.Read<AmountLow>();
                                   return ExperienceOf(reader.Read<KillKey>(), amount, reader.Read<ExtendedDamage>());
                               });
}

// 0x4d0a3d..0x4d0a46 and 0x4d0b49..0x4d0b56: the key unmasked; the skill and the killer, if they arrived.
Core::Expected<Death, Network::ProtocolError> ReadDeath(const Network::Packet& packet)
{
    return Network::ReadPacket(
        packet, DeathLayout, [](Network::LayoutReader& reader)
        { return Death{reader.Read<KillKey>(), IfArrived<DeathSkill>(reader), IfArrived<Killer>(reader)}; });
}
} // namespace Client::Protocol::Combat
