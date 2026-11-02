#pragma once

#include <cstdint>

// Lo que puede fallar en un socket de cliente (Network/ISocket.h) o al resolver su host
// (Network/IResolver.h). Contrato: el plan de la CL4 (CL4.1), "Contrato".
namespace Client::Network
{
enum class SocketError : std::uint8_t
{
    ResolveFailed, // el IResolver no dio ninguna IPv4
    Refused,       // el otro lado rechazó la conexión
    // No hay conexión: el otro lado cerró (recv en 0, o ECONNRESET, ECONNABORTED, EPIPE o sus
    // WSA*), o este lado no conectó todavía o ya cerró (antes de Connected o después de Close).
    Closed,
    Failed, // otro error del sistema: LastSystemError() lo dice, para el log
};
} // namespace Client::Network
