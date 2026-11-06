#pragma once

#include "GameLogic/PathCell.h"

#include <array>
#include <cstddef>
#include <cstdint>

// El resultado de PathFinding2 (0x4eacef..0x4ead56), con hasta 15 celdas incluido el inicio.
// Representación propia: los tres contadores son bytes; «CurrentPathFloat» también es un byte.
namespace Client::GameLogic
{
inline constexpr std::size_t MaxPathCells = 15;

struct Path
{
    std::uint8_t current = 0;
    std::uint8_t progress = 0;
    std::uint8_t count = 0;
    std::array<PathCell, MaxPathCells> cells{};
};
} // namespace Client::GameLogic
