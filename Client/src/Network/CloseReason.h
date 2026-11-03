#pragma once

#include <cstdint>
#include <string_view>

// Por qué se cerró una Connection (Network/Connection.h): para el log y para quien la usa, que
// decide qué hacer (la ServerSession, CL4.3).
// Contrato: el plan de la CL4 (CL4.3), "Interfaces".
namespace Client::Network
{
enum class CloseReason : std::uint8_t
{
    ByClient,        // Close(), u Open sobre una conexión que no estaba cerrada
    ResolveFailed,   // el host no dio ninguna IPv4
    Refused,         // el otro lado rechazó la conexión
    ConnectTimedOut, // pasó ConnectTimeout sin terminar de conectar
    PeerClosed,      // el otro lado cerró, con un FIN o con un RST
    SocketFailed,    // otro error del socket: el log trae el del sistema
    BadStream,       // el PacketReader perdió el corte: no se sabe dónde empieza el próximo paquete
    SendQueueFull,   // lo pendiente más lo nuevo pasaba de MaxPendingSend: el original también cierra
};

// El motivo en palabras, para el log: "el otro lado cerró".
[[nodiscard]] std::string_view CloseReasonText(CloseReason reason);
} // namespace Client::Network
