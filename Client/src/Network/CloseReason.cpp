#include "Network/CloseReason.h"

namespace Client::Network
{
std::string_view CloseReasonText(CloseReason reason)
{
    switch (reason)
    {
    case CloseReason::ByClient:
        return "la cerró el cliente";
    case CloseReason::ResolveFailed:
        return "el host no resolvió a ninguna IPv4";
    case CloseReason::Refused:
        return "el servidor rechazó la conexión";
    case CloseReason::ConnectTimedOut:
        return "el connect no terminó a tiempo";
    case CloseReason::PeerClosed:
        return "el otro lado cerró";
    case CloseReason::SocketFailed:
        return "falló el socket";
    case CloseReason::BadStream:
        return "llegó una cabecera mala y se perdió el corte";
    case CloseReason::SendQueueFull:
        return "lo que no salió no entra en la cola de envío";
    }
    return "un motivo fuera del enum";
}
} // namespace Client::Network
