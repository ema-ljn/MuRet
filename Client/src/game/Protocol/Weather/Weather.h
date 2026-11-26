#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"

#include <cstdint>

// The weather, C1 04 0F xy (04, "El clima y Crywolf").
namespace Client::Protocol::Weather
{
// 0x4e36b5: the whole pkt[3]. Its high nibble picks the branch; above 1 the target does not change.
struct Weather
{
    std::uint8_t code;
};

// The raw byte, branches above 1 included; it applies nothing.
[[nodiscard]] Core::Expected<Weather, Network::ProtocolError> ReadWeather(const Network::Packet& packet);
} // namespace Client::Protocol::Weather
