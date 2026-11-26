#pragma once

#include "Protocol/Outgoing.h"

#include <cstdint>

// 04 § Castle Siege, batalla: órdenes de 0x44dd50, 0x44f810 y 0x4105f0.
namespace Client::Protocol::CastleSiege
{
[[nodiscard]] Outgoing WriteWeapon(std::uint16_t key, std::uint8_t choice);
[[nodiscard]] Outgoing WriteGate(std::uint8_t open, std::uint16_t key);
[[nodiscard]] Outgoing WriteCommand(std::uint8_t row, std::uint8_t x, std::uint8_t y, std::uint8_t command);
} // namespace Client::Protocol::CastleSiege
