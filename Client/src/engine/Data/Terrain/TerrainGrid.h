#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// 256 × 256 cells, stored row by row (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo"). Cell
// (x, y) is y * 256 + x, as the original counts it (0x5fac90).
namespace Client::Data::Terrain
{
// A coordinate goes from 0 to 255: it fits a byte exactly.
inline constexpr std::size_t TerrainSide = 256;
inline constexpr std::size_t TerrainCellCount = TerrainSide * TerrainSide;
static_assert(TerrainCellCount == 0x10000, "lo que ocupa una capa de un byte (05, Terreno de un mundo)");

// A std::array and not a std::vector because the size is part of the format: it is in the type and nobody can change
// it.
template <typename T> using TerrainLayer = std::array<T, TerrainCellCount>;

// With one-byte coordinates there is no way out of the grid: the last cell, (255, 255), is TerrainCellCount - 1.
constexpr std::size_t CellIndex(std::uint8_t x, std::uint8_t y)
{
    return std::size_t{y} * TerrainSide + std::size_t{x};
}
static_assert(CellIndex(1, 0) == 1, "x avanza de a una celda");
static_assert(CellIndex(0, 1) == TerrainSide, "y avanza de a una fila");
static_assert(CellIndex(255, 255) == TerrainCellCount - 1, "la última celda es la última de la capa");
} // namespace Client::Data::Terrain
