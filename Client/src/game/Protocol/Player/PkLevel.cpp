#include "Protocol/Player/PkLevel.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"
#include "Protocol/Opcodes.h"

#include <bit>
#include <cstdint>

namespace Client::Protocol::Player
{
namespace
{
using Key = Network::Field<std::uint16_t, 4, std::endian::big>;
using Level = Network::Field<std::uint8_t, 6>;
constexpr Network::PacketLayout PkLevelLayout{
    {Network::PacketType::C1, CharacterOpcode, Character::PkLevelSubcode}, Level::End, 0x4d3b50};
} // namespace

Core::Expected<PkLevel, Network::ProtocolError> ReadPkLevel(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, PkLevelLayout, [](Network::LayoutReader& reader)
                               { return PkLevel{.key = reader.Read<Key>(), .level = reader.Read<Level>()}; });
}
} // namespace Client::Protocol::Player
