#include "Protocol/Connection/Logout.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <cstdint>
#include <optional>

namespace Client::Protocol::Connection
{
namespace
{
// La respuesta de la salida llega en un C1, ya descifrada.
constexpr PacketKind LogoutResultKind{Network::PacketType::C1, JoinOpcode, LogoutSubcode};
} // namespace

Core::Expected<std::uint8_t, ProtocolError> ReadLogoutResult(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, LogoutResultKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    const std::optional<std::uint8_t> result = fields.Value().ReadU8();
    if (!result)
    {
        return Core::Unexpected(ProtocolError::TooShort);
    }
    return *result;
}
} // namespace Client::Protocol::Connection
