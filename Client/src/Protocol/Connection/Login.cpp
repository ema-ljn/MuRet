#include "Protocol/Connection/Login.h"

#include "Core/Expected.h"
#include "Crypto/Xor3.h"
#include "Network/Field.h"
#include "Network/LayoutReader.h"
#include "Network/Packet.h"
#include "Network/PacketLayout.h"
#include "Network/PacketWriter.h"
#include "Network/ProtocolError.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Opcodes.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Client::Protocol::Connection
{
namespace
{
using LoginResult = Network::Field<std::uint8_t, 4>;
// 0x4e2b6e: the dispatcher reads the result itself.
constexpr Network::PacketLayout LoginResultLayout{
    {Network::PacketType::C1, JoinOpcode, Join::LoginSubcode}, LoginResult::End, 0x4e2b6e};

// Lo que lleva el aviso después del subcódigo, 06 00 (03 § C3/C4). El GameServer los lee como dos
// bytes sueltos (CGClientHackLogRecv, 0x4172e0, en el research del servidor); qué significan para
// él está sin leer.
constexpr std::uint8_t DecryptErrorFirst = 0x06;
constexpr std::uint8_t DecryptErrorSecond = 0x00;
// 0x4c396a..0x4c3d14: lo que manda la respuesta del F3 03 que llega sin cifrar.
constexpr std::uint8_t ClearReplyFirst = 0x00;
constexpr std::uint8_t ClearWorldEntryHead = Network::ByteOf(CharacterOpcode);

using CredentialField = std::array<std::byte, CredentialSize>;

// Un campo de la cuenta o de la clave: el texto, \0 hasta los 10 bytes y el XOR3 sobre el campo
// entero, como 0x641bf0 con 0x4c1690. Quien llama ya miró que el texto entre.
CredentialField Credential(std::string_view text)
{
    CredentialField field{};
    std::ranges::copy(std::as_bytes(std::span(text)), field.begin());
    Crypto::Xor3(field);
    return field;
}

// Los bytes de una constante del build.
std::span<const std::byte> BytesOf(std::string_view text)
{
    return std::as_bytes(std::span(text));
}
} // namespace

Core::Expected<Outgoing, Network::ProtocolError> WriteLoginRequest(const LoginRequest& request)
{
    if (request.account.size() > CredentialSize || request.password.size() > CredentialSize)
    {
        return Core::Unexpected(Network::ProtocolError::FieldTooLong);
    }
    const CredentialField account = Credential(request.account);
    const CredentialField password = Credential(request.password);
    Network::PacketWriter writer = Network::PacketWriter::C1(JoinOpcode, Join::LoginSubcode);
    writer.PutBytes(account)
        .PutBytes(password)
        .PutU32Le(request.tick)
        .PutBytes(BytesOf(ClientVersion))
        .PutBytes(BytesOf(ClientSerial));
    return EncryptedC1(writer);
}

Core::Expected<std::uint8_t, Network::ProtocolError> ReadLoginResult(const Network::Packet& packet)
{
    return Network::ReadPacket(packet, LoginResultLayout,
                               [](Network::LayoutReader& reader) { return reader.Read<LoginResult>(); });
}

Outgoing WriteDecryptErrorReport()
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode)
                           .PutU8(DecryptErrorFirst)
                           .PutU8(DecryptErrorSecond));
}

Outgoing WriteClearWorldEntryReport()
{
    return EncryptedC1(Network::PacketWriter::C1(JoinOpcode, Join::DecryptErrorSubcode)
                           .PutU8(ClearReplyFirst)
                           .PutU8(ClearWorldEntryHead));
}
} // namespace Client::Protocol::Connection
