#pragma once

#include <bit>
#include <cstdint>

namespace Client::Terrain
{
// 0x6b86b4: the side of a terrain cell in world units.
inline constexpr float CellSize = 100.0f;
static_assert(std::bit_cast<std::uint32_t>(CellSize) == 0x42C80000);
inline constexpr double CellSizeAsDouble = static_cast<double>(CellSize);
} // namespace Client::Terrain
