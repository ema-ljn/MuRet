#pragma once

#include "EngineApi.h"
#include "Network/ISocket.h"
#include "Network/SocketError.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// The original's send buffer is 0x2000 (+0x10, the length at +0x2010, in 0x4e87b0) and its drain subtracts what left
// twice: with several partial sends in a row it repeats the start and loses the end (Q-C11). This one keeps a cursor
// in a fixed std::array of the same size: Push shifts what is pending to the start when the new bytes do not fit
// behind, and what does not fit in 0x2000 bytes is Full, like the original, which closes. The Connection never sends
// directly with something pending (0x4b1664's inline send does, and reorders).
namespace Client::Network
{
inline constexpr std::size_t MaxPendingSend = 0x2000;

enum class SendQueueError : std::uint8_t
{
    Full,   // pending plus new exceeds MaxPendingSend: the original closes
    Socket, // SocketError in LastSocketError()
};

class ENGINE_API SendQueue
{
public:
    // Or Full without adding anything.
    [[nodiscard]] std::optional<SendQueueError> Push(std::span<const std::byte> bytes);

    // Sends what the socket takes, from the cursor, until it takes no more or nothing is left. With nothing pending the
    // socket is not called. On a socket error what already left does not come back; the rest stays.
    [[nodiscard]] std::optional<SendQueueError> Flush(ISocket& socket);

    [[nodiscard]] std::size_t Pending() const;

    // Empty if there was none.
    [[nodiscard]] std::optional<SocketError> LastSocketError() const;

    // The connection closed: what did not leave does not go to the next one.
    void Clear();

private:
    void Compact();

    std::array<std::byte, MaxPendingSend> m_buffer{};
    std::size_t m_sent = 0; // the cursor: what already left of what there is
    std::size_t m_end = 0;
    std::optional<SocketError> m_lastSocketError;
};
} // namespace Client::Network
