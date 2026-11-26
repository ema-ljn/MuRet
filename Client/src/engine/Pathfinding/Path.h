#pragma once

#include "Pathfinding/PathCell.h"

#include <array>
#include <cstddef>
#include <cstdint>

// PathFinding2's result (0x4eacef..0x4ead56), up to 15 cells including the start. The three counters are bytes;
// «CurrentPathFloat» is a byte too.
namespace Client::Pathfinding
{
inline constexpr std::size_t MaxPathCells = 15;

struct Path
{
    std::uint8_t current = 0;
    std::uint8_t progress = 0;
    std::uint8_t count = 0;
    std::array<PathCell, MaxPathCells> cells{};
};
} // namespace Client::Pathfinding
