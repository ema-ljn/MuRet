#pragma once

#include "Core/Expected.h"
#include "Network/Packet.h"
#include "Network/ProtocolError.h"

#include <cstdint>

// El estado de PK de un personaje (F3 08, S->C, 0x4d3b50): C1 07 F3 08, la clave en pkt[4..5] con el byte alto primero
// (0x4d3b5f..0x4d3b6a) y el estado en pkt[6] (0x4d3b86). El servidor lo manda al entrar al mundo y cuando cambia.
namespace Client::Protocol::Player
{
struct PkLevel
{
    std::uint16_t key = 0;
    std::uint8_t level = 0;
};

// Sin los siete bytes, TooShort: el original los lee igual.
[[nodiscard]] Core::Expected<PkLevel, Network::ProtocolError> ReadPkLevel(const Network::Packet& packet);
} // namespace Client::Protocol::Player
