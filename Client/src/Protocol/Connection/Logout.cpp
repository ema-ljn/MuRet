#include "Protocol/Connection/Logout.h"

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Network/PacketFields.h"
#include "Network/ProtocolError.h"

#include <cstdint>
#include <optional>

namespace Client::Protocol::Connection
{
namespace
{
// La respuesta de la salida llega en un C1, ya descifrada.
constexpr Network::PacketKind LogoutResultKind{Network::PacketType::C1, JoinOpcode, LogoutSubcode};
// 0x4c30ce y 0x4c31b9: lo que manda el F1 02 con 1 que llega sin cifrar, el 0 y el F1 de su paquete.
constexpr std::uint8_t ClearLogoutFirst = 0x00;
constexpr std::uint8_t ClearLogoutHead = JoinOpcode;
} // namespace

Core::Expected<std::uint8_t, Network::ProtocolError> ReadLogoutResult(const Network::Packet& packet)
{
    auto fields = Network::FieldsAfterSubcode(packet, LogoutResultKind);
    if (!fields.HasValue())
    {
        return Core::Unexpected(fields.Error());
    }
    const std::optional<std::uint8_t> result = fields.Value().ReadU8();
    if (!result)
    {
        return Core::Unexpected(Network::ProtocolError::TooShort);
    }
    return *result;
}

Outgoing WriteLogoutRequest(std::uint8_t kind)
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode).PutU8(LogoutSubcode).PutU8(kind));
}

Outgoing WriteClearLogoutReport()
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode)
                           .PutU8(DecryptErrorSubcode)
                           .PutU8(ClearLogoutFirst)
                           .PutU8(ClearLogoutHead));
}

Outgoing WriteWindowClose()
{
    return ClearC1(Network::PacketWriter::C1(WindowCloseOpcode));
}
} // namespace Client::Protocol::Connection
