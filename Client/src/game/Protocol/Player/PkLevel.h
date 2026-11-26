#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"

#include <cstdint>

// A character's PK level, F3 08 (0x4d3b50): the key at pkt[4..5], high byte first (0x4d3b5f..0x4d3b6a), and the level
// at pkt[6] (0x4d3b86). The server sends it on entering the world and when it changes.
namespace Client::Protocol::Player
{
struct PkLevel
{
    std::uint16_t key = 0;
    std::uint8_t level = 0;
};

[[nodiscard]] Core::Expected<PkLevel, Network::ProtocolError> ReadPkLevel(const Network::Packet& packet);
} // namespace Client::Protocol::Player
