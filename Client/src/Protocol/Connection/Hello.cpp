#include "Protocol/Connection/Hello.h"

#include "Core/ByteReader.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/PacketFields.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <span>

namespace Client::Protocol::Connection
{

namespace
{

// El saludo llega en un C1.
constexpr PacketKind HelloKind{Network::PacketType::C1, JoinOpcode, HelloSubcode};

} // namespace

Core::Expected<ServerHello, ProtocolError> ReadServerHello(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, HelloKind);
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
        return Core::Unexpected(ProtocolError::TooShort);
    }
    ServerHello hello{*result, *playerIndex, {}};
    std::ranges::transform(*version, hello.version.begin(),
                           [](std::byte value) { return std::to_integer<char>(value); });
    return hello;
}

} // namespace Client::Protocol::Connection
