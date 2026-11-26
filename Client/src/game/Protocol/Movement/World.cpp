#include "Protocol/Movement/World.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Outgoing.h"
#include "Network/PacketFields.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::Movement
{
namespace
{
constexpr Network::PacketKind RegenKind{Network::PacketType::C1, CharacterOpcode, Character::RegenSubcode};
// 0x4c4b80: los bytes +0xe/+0xf no se leen; +0x10 es la experiencia.
constexpr std::size_t RegenPaddingSize = 2;
} // namespace

Core::Expected<Teleport, Network::ProtocolError> ReadTeleport(const Network::Packet& packet)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != TeleportOpcode)
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    Core::ByteReader reader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
    const auto gate = reader.ReadU8();
    const auto map = reader.ReadU8();
    const auto x = reader.ReadU8();
    const auto y = reader.ReadU8();
    const auto direction = reader.ReadU8();
    if (!gate || !map || !x || !y || !direction)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return Teleport{*gate, *map, *x, *y, *direction};
}

Core::Expected<Regen, Network::ProtocolError> ReadRegen(const Network::Packet& packet)
{
    auto fields = Network::FieldsAfterSubcode(packet, RegenKind);
    if (!fields.HasValue())
        return Core::Unexpected(fields.Error());
    Core::ByteReader& reader = fields.Value();
    const auto x = reader.ReadU8();
    const auto y = reader.ReadU8();
    const auto map = reader.ReadU8();
    const auto direction = reader.ReadU8();
    // 0x4c4c23..0x4c4c4a: cargas x86 de word/dword, sin invertir bytes.
    const auto life = reader.ReadU16Le();
    const auto mana = reader.ReadU16Le();
    const auto bp = reader.ReadU16Le();
    const auto padding = reader.ReadBytes(RegenPaddingSize);
    const auto experience = reader.ReadU32Le();
    const auto zen = reader.ReadU32Le();
    if (!x || !y || !map || !direction || !life || !mana || !bp || !padding || !experience || !zen)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return Regen{*x, *y, *map, *direction, *life, *mana, *bp, *experience, *zen};
}

Core::Expected<Weather, Network::ProtocolError> ReadWeather(const Network::Packet& packet)
{
    if (packet.Type() != Network::PacketType::C1 || packet.Opcode() != WeatherOpcode)
        return Core::Unexpected(Network::ProtocolError::WrongPacket);
    Core::ByteReader reader(packet.Bytes().subspan(Network::MinPacketSize(Network::PacketType::C1)));
    const auto code = reader.ReadU8();
    if (!code)
        return Core::Unexpected(Network::ProtocolError::TooShort);
    return Weather{*code};
}

Outgoing WriteGateRequest(std::uint8_t gate)
{
    return EncryptedC1(Network::PacketWriter::C1(TeleportOpcode).PutU8(gate).PutU8(0).PutU8(0));
}

Outgoing WriteTeleportDone()
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode, Character::TeleportDoneSubcode));
}

Outgoing WriteRejectedTeleport()
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode)
                           .PutU8(0)
                           .PutU8(Network::ByteOf(TeleportOpcode)));
}

Outgoing WriteCrywolfState()
{
    return Xor32C1(Network::PacketWriter::C1(CrywolfOpcode).PutU8(0));
}

Outgoing WriteCastleSiegeState()
{
    return Xor32C1(Network::PacketWriter::C1(CastleInfoOpcode, CastleInfo::OwnerMarkSubcode));
}
} // namespace Client::Protocol::Movement
