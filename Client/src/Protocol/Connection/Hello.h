#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Protocol/BuildConstants.h"
#include "Protocol/ProtocolError.h"

#include <array>
#include <cstdint>

// El saludo del GameServer (F1 00), lo primero que llega al conectar.
// Contrato: Client/docs/research/04-protocolo.md, "El flujo de conexión", y el plan de la CL2
// (CL2.5).
namespace Client::Protocol::Connection
{
// El resultado del saludo que acepta la conexión: con otro, 0x4c1e20 muestra "Connectting error".
inline constexpr std::uint8_t HelloAccepted = 1;

struct ServerHello
{
    std::uint8_t result;                   // HelloAccepted, o un error
    std::uint16_t playerIndex;             // el índice del jugador en el GameServer
    std::array<char, VersionSize> version; // la del GameServer, que se compara con ClientVersion
};

// F1 00: C1 0C F1 00, el resultado, el índice en dos bytes big-endian (0x4c1e20 lo arma como
// pkt[5] * 0x100 + pkt[6]) y la versión, 5 bytes sin \0. Compararla con ClientVersion es de quien
// lo usa (CL4): el original lo hace aunque el resultado no sea HelloAccepted.
[[nodiscard]] Core::Expected<ServerHello, ProtocolError> ReadServerHello(const Network::Packet& packet);
} // namespace Client::Protocol::Connection
