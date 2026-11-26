#include "Protocol/Player/LevelUpPoint.h"

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

namespace Client::Protocol::Player
{
namespace
{
using PointResult = Network::Field<std::uint8_t, 4>;
using MaxLifeOrMana = Network::Field<std::uint16_t, 6>;
using MaxBp = Network::Field<std::uint16_t, 8>;
constexpr Network::PacketLayout PointResultLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::LevelUpPointSubcode}, PointResult::End, 0x4d3200};
constexpr std::uint8_t AddedBits = 0xf0;
} // namespace

Outgoing WriteLevelUpPoint(std::uint8_t stat)
{
    return Xor32C1(Network::PacketWriter::C1(CharacterOpcode, Character::LevelUpPointSubcode).PutU8(stat));
}

Core::Expected<LevelUpPointResult, Network::ProtocolError> ReadLevelUpPointResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PointResultLayout,
                               [](Network::LayoutReader& reader)
                               {
                                   LevelUpPointResult reply{.result = reader.Read<PointResult>()};
                                   if ((reply.result & AddedBits) != 0)
                                   {
                                       reply.maxLifeOrMana = reader.Read<MaxLifeOrMana>();
                                       reply.maxBp = reader.Read<MaxBp>();
                                   }
                                   return reply;
                               });
}
} // namespace Client::Protocol::Player
