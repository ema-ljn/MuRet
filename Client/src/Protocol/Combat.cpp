#include "Protocol/Combat.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>

namespace Client::Protocol::Combat
{
namespace
{
constexpr std::uint16_t KeyMask = 0x7fff;
constexpr std::uint16_t ShockMask = 0x8000;

Core::Expected<Core::ByteReader, ProtocolError> FieldsOf(const Network::Packet& packet, std::uint8_t opcode)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != opcode)
        return Core::Unexpected(ProtocolError::WrongPacket);
    return Core::ByteReader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
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

Outgoing WriteClearKillReport(std::uint8_t opcode)
{
    return EncryptedC1(Network::PacketWriter::C1(Connection::JoinOpcode)
                           .PutU8(Connection::DecryptErrorSubcode)
                           .PutU8(0)
                           .PutU8(opcode));
}

// 0x4cf2b4..0x4cf2f0: la clave, la experiencia y el daño, de a dos bytes con el alto primero.
Core::Expected<KillExperience, ProtocolError> ReadKillExperience(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, KillExperienceOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto key = reader.ReadU16Be();
    const auto amount = reader.ReadU16Be();
    const auto damage = reader.ReadU16Be();
    if (!key || !amount || !damage)
        return Core::Unexpected(ProtocolError::TooShort);
    return ExperienceOf(*key, *amount, *damage);
}

// 0x4cfd94..0x4cfdf0: después de la clave saltea +5 (el relleno natural del WORD de la GS) y lee los dos WORD en
// little endian, ExpH · 65536 + ExpL.
Core::Expected<KillExperience, ProtocolError> ReadExtendedKillExperience(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, ExtendedKillExperienceOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto key = reader.ReadU16Be();
    const auto padding = reader.ReadU8();
    const auto high = reader.ReadU16Le();
    const auto low = reader.ReadU16Le();
    const auto damage = reader.ReadU16Be();
    if (!key || !padding || !high || !low || !damage)
        return Core::Unexpected(ProtocolError::TooShort);
    return ExperienceOf(*key, (std::uint32_t{*high} << 16) + *low, *damage);
}

// 0x4d0a3d..0x4d0a46 y 0x4d0b49..0x4d0b56: la clave sin máscara; la skill y el que mató, si llegaron.
Core::Expected<Death, ProtocolError> ReadDeath(const Network::Packet& packet)
{
    auto fields = FieldsOf(packet, DeathOpcode);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto key = reader.ReadU16Be();
    if (!key)
        return Core::Unexpected(ProtocolError::TooShort);
    const auto skill = reader.ReadU8();
    const auto killer = reader.ReadU16Be();
    return Death{*key, skill.value_or(0), killer.value_or(0)};
}
} // namespace Client::Protocol::Combat
