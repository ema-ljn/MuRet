#include "Protocol/Session/Hello.h"

#include "Core/Expected.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/ProtocolError.h"
#include "Protocol/Session/BuildConstants.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace Client::Protocol::Connection
{
namespace
{
using HelloResult = Network::Field<std::uint8_t, 4>;
using HelloPlayerIndex = Network::Field<std::uint16_t, 5, std::endian::big>;
using HelloVersion = Network::Field<std::array<std::byte, VersionSize>, 7>;

constexpr Network::PacketLayout HelloLayout{
    {Network::PacketType::C1, JoinOpcode, Join::HelloSubcode}, HelloVersion::End, 0x4c1e20};

ServerHello ReadHello(Network::LayoutReader& reader)
{
    ServerHello hello{reader.Read<HelloResult>(), reader.Read<HelloPlayerIndex>(), {}};
    std::ranges::transform(reader.Read<HelloVersion>(), hello.version.begin(),
                           [](std::byte value) { return std::to_integer<char>(value); });
    return hello;
}
} // namespace

Core::Expected<ServerHello, Network::ProtocolError> ReadServerHello(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, HelloLayout, ReadHello);
}
} // namespace Client::Protocol::Connection
