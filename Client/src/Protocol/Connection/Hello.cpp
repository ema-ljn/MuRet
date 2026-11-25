#include "Protocol/Connection/Hello.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Connection/Opcodes.h"
#include "Network/PacketFields.h"
#include "Network/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::Protocol::Connection
{
namespace
{
// El saludo llega en un C1.
constexpr Network::PacketKind HelloKind{Network::PacketType::C1, JoinOpcode, HelloSubcode};
} // namespace

Core::Expected<ServerHello, Network::ProtocolError> ReadServerHello(const Network::Packet& packet)
{
    auto fields = Network::FieldsAfterSubcode(packet, HelloKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    Core::ByteReader& reader = fields.Value();
    const std::optional<std::uint8_t> result = reader.ReadU8();
    const std::optional<std::uint16_t> playerIndex = reader.ReadU16Be();
    const std::optional<std::span<const std::byte>> version = reader.ReadBytes(VersionSize);
    if (!result || !playerIndex || !version)
    {
        return Core::Unexpected(Network::ProtocolError::TooShort);
    }
    ServerHello hello{*result, *playerIndex, {}};
    for (std::size_t index = 0; index < hello.version.size(); ++index)
    {
        hello.version[index] = std::to_integer<char>((*version)[index]);
    }
    return hello;
}
} // namespace Client::Protocol::Connection
