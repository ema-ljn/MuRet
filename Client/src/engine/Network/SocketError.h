#pragma once

#include <cstdint>

namespace Client::Network
{
enum class SocketError : std::uint8_t
{
    ResolveFailed, // the IResolver gave no IPv4
    Refused,
    // The other side closed (recv at 0, or ECONNRESET, ECONNABORTED, EPIPE or their WSA*), or this side has not
    // connected yet or already closed (before Connected or after Close).
    Closed,
    Failed, // LastSystemError() says which, for the log
};
} // namespace Client::Network
