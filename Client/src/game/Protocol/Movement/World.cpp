#include "Protocol/Movement/World.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"
#include "Protocol/Outgoing.h"

#include <cstdint>
#include <optional>

namespace Client::Protocol::Movement
{
namespace
{
using Gate = Network::Field<std::uint8_t, 3>;
using TeleportMap = Network::Field<std::uint8_t, 4>;
using TeleportX = Network::Field<std::uint8_t, 5>;
using TeleportY = Network::Field<std::uint8_t, 6>;
using TeleportDirection = Network::Field<std::uint8_t, 7>;
constexpr Network::PacketLayout TeleportLayout{
    {Network::PacketType::C1, TeleportOpcode, std::nullopt}, TeleportDirection::End, 0x4c6e10};

using RegenX = Network::Field<std::uint8_t, 4>;
using RegenY = Network::Field<std::uint8_t, 5>;
using RegenMap = Network::Field<std::uint8_t, 6>;
using RegenDirection = Network::Field<std::uint8_t, 7>;
// 0x4c4c23..0x4c4c4a: x86 word and dword loads, with no byte swap.
using Life = Network::Field<std::uint16_t, 8>;
using Mana = Network::Field<std::uint16_t, 0xa>;
using Bp = Network::Field<std::uint16_t, 0xc>;
// 0x4c4b80 does not read +0xe and +0xf.
using Experience = Network::Field<std::uint32_t, 0x10>;
using Zen = Network::Field<std::uint32_t, 0x14>;
constexpr Network::PacketLayout RegenLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::RegenSubcode}, Zen::End, 0x4c4b80};
} // namespace

Core::Expected<Teleport, Network::ProtocolError> ReadTeleport(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, TeleportLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return Teleport{reader.Read<Gate>(), reader.Read<TeleportMap>(),
                                                   reader.Read<TeleportX>(), reader.Read<TeleportY>(),
                                                   reader.Read<TeleportDirection>()};
                               });
}

Core::Expected<Regen, Network::ProtocolError> ReadRegen(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, RegenLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   return Regen{reader.Read<RegenX>(),   reader.Read<RegenY>(),
                                                reader.Read<RegenMap>(), reader.Read<RegenDirection>(),
                                                reader.Read<Life>(),     reader.Read<Mana>(),
                                                reader.Read<Bp>(),       reader.Read<Experience>(),
                                                reader.Read<Zen>()};
                               });
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
