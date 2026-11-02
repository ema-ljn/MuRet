#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/Outgoing.h"
#include "Protocol/ProtocolError.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

// El login con el GameServer: el pedido (F1 01), su respuesta y el aviso de un C3/C4 que no se pudo
// descifrar (F1 03).
// Contrato: Client/docs/research/04-protocolo.md, "El flujo de conexión", 03-red-y-cifrado.md,
// "C3/C4", y el plan de la CL2 (CL2.5).
//
// Los dos que manda el cliente llevan el XOR32 y salen cifrados como C3.
namespace Client::Protocol::Connection
{
// Lo que mide la cuenta y la clave en el login: 10 bytes cada una, completadas con \0.
inline constexpr std::size_t CredentialSize = 10;

// No tiene operator<<: la cuenta y la clave no se imprimen nunca.
struct LoginRequest
{
    std::string_view account;
    std::string_view password;
    std::uint32_t tick; // el GetTickCount del cliente
};

// F1 01: C1 31 F1 01, la cuenta y la clave en 10 bytes cada una (completadas con \0 y con el XOR3
// en los 10 bytes del campo, como 0x4c1690), el tick (u32 little-endian), ClientVersion y
// ClientSerial; con el XOR32 y para cifrar. FieldTooLong con una cuenta o una clave de más de
// CredentialSize bytes. El original no la rechaza: 0x641bf0 copia la cuenta con strcpy a un buffer
// de 10 bytes en 0 que tiene 2 de sobra antes del de la clave, y una de 11 o 12 sale cortada en 10;
// lo único que la limita es el campo de texto de la escena de login (CL5; Q-C43). Una vacía se
// escribe igual: el original no la manda (0x641bf0 mira el largo de las dos antes de armar el
// paquete), y eso lo decide la escena de login. Un \0 en el medio se copia como un byte más, y el
// strcpy del original cortaría ahí: desde un campo de texto no se puede escribir.
[[nodiscard]] Core::Expected<Outgoing, ProtocolError> WriteLoginRequest(const LoginRequest& request);

// El resultado del login que lo acepta.
inline constexpr std::uint8_t LoginAccepted = 1;

// La respuesta F1 01: C1 05 F1 01 y el resultado. El login que arma el cliente, sin el XOR32, tiene
// la misma forma (C1 F1 01 y algo detrás) y también se lee: la CL4 los separa por el sentido.
[[nodiscard]] Core::Expected<std::uint8_t, ProtocolError> ReadLoginResult(const Network::Packet& packet);

// F1 03: C1 06 F1 03 06 00, con el XOR32 y para cifrar. Lo manda el original cuando un C3/C4 que
// llega tiene el checksum malo (03, "C3/C4"; DecryptPacket con ChecksumMismatch o NotWholeBlocks).
[[nodiscard]] Outgoing WriteDecryptErrorReport();
} // namespace Client::Protocol::Connection
