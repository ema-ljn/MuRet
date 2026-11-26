#pragma once

#include <cstdint>

// In its own header, like CloseReason.h, for whoever needs only the state and not the whole connection.
namespace Client::Network
{
enum class LinkState : std::uint8_t
{
    Closed,
    Connecting, // from Open to the Poll that gives the Opened
    Open,
};
} // namespace Client::Network
