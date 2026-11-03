#include "Protocol/Connection/Login.h"

#include "Core/ByteReader.h"
#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Network/Packet.h"
#include "Network/PacketWriter.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Connection/Opcodes.h"
#include "Protocol/Outgoing.h"
#include "Protocol/PacketFields.h"
#include "Protocol/ProtocolError.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Client::Protocol::Connection
{
namespace
{
// La respuesta del login llega en un C1.
constexpr PacketKind LoginResultKind{Network::PacketType::C1, JoinOpcode, LoginSubcode};

// Lo que lleva el aviso después del subcódigo, 06 00 (03 § C3/C4). El GameServer los lee como dos
// bytes sueltos (CGClientHackLogRecv, 0x4172e0, en el research del servidor); qué significan para
// él está sin leer.
constexpr std::uint8_t DecryptErrorFirst = 0x06;
constexpr std::uint8_t DecryptErrorSecond = 0x00;

using CredentialField = std::array<std::byte, CredentialSize>;

// Un campo de la cuenta o de la clave: el texto, \0 hasta los 10 bytes y el XOR3 sobre el campo
// entero, como 0x641bf0 con 0x4c1690. Quien llama ya miró que el texto entre.
CredentialField Credential(std::string_view text)
{
    CredentialField field{};
    std::ranges::copy(std::as_bytes(std::span(text)), field.begin());
    Core::Xor3(field);
    return field;
}

// Los bytes de una constante del build.
std::span<const std::byte> BytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text));
}
} // namespace

Core::Expected<Outgoing, ProtocolError> WriteLoginRequest(const LoginRequest& request)
{
    if (request.account.size() > CredentialSize || request.password.size() > CredentialSize)
    {
        return Core::Unexpected(ProtocolError::FieldTooLong);
    }
    const CredentialField account = Credential(request.account);
    const CredentialField password = Credential(request.password);
    Network::PacketWriter writer = Network::PacketWriter::C1(JoinOpcode);
    writer.PutU8(LoginSubcode)
        .PutBytes(account)
        .PutBytes(password)
        .PutU32Le(request.tick)
        .PutBytes(BytesOf(ClientVersion))
        .PutBytes(BytesOf(ClientSerial));
    return EncryptedC1(writer);
}

Core::Expected<std::uint8_t, ProtocolError> ReadLoginResult(const Network::Packet& packet)
{
    auto fields = FieldsAfterSubcode(packet, LoginResultKind);
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

Outgoing WriteDecryptErrorReport()
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode)
                           .PutU8(DecryptErrorSubcode)
                           .PutU8(DecryptErrorFirst)
                           .PutU8(DecryptErrorSecond));
}
} // namespace Client::Protocol::Connection
