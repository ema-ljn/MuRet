#pragma once

#include "EngineApi.h"

#include <cstdint>
#include <string_view>

// Why a Connection (Network/Connection.h) closed: for the log and for its user, which decides what to do.
namespace Client::Network
{
enum class CloseReason : std::uint8_t
{
    ByClient,      // Close(), or Open on a connection that was not closed
    ResolveFailed, // the host gave no IPv4
    Refused,
    ConnectTimedOut, // ConnectTimeout passed before connecting
    PeerClosed,      // with a FIN or an RST
    SocketFailed,    // another socket error: the log has the system's
    BadStream,       // the PacketReader lost the framing: where the next packet starts is unknown
    SendQueueFull,   // pending plus new exceeded MaxPendingSend: the original closes too
};

// For the log.
[[nodiscard]] ENGINE_API std::string_view CloseReasonText(CloseReason reason);
} // namespace Client::Network
