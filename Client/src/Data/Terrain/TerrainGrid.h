// La grilla del terreno de un mundo (Client/docs/research/05-formatos-de-datos.md, "Terreno de un
// mundo"): 256 × 256 celdas, guardadas fila por fila. La celda (x, y) es la y * 256 + x, como la
// cuenta el original (0x5fac90).

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Client::Data::Terrain
{

// Celdas por lado. Una coordenada va de 0 a 255: entra justa en un byte.
inline constexpr std::size_t TerrainSide = 256;
// Las celdas de la grilla. Cada capa del .map y el .att tiene un valor por celda.
inline constexpr std::size_t TerrainCellCount = TerrainSide * TerrainSide;
static_assert(TerrainCellCount == 0x10000, "lo que ocupa una capa de un byte (05, Terreno de un mundo)");

// Una capa: un valor por celda, en el orden de CellIndex. Es un std::array y no un std::vector
// porque el tamaño es parte del formato: está en el tipo y nadie lo puede cambiar.
template <typename T> using TerrainLayer = std::array<T, TerrainCellCount>;

// El lugar de la celda (x, y) en una capa. Con coordenadas de un byte no hay forma de salirse de
// la grilla: la última celda, (255, 255), es la TerrainCellCount - 1.
constexpr std::size_t CellIndex(std::uint8_t x, std::uint8_t y)
{
    return std::size_t{y} * TerrainSide + std::size_t{x};
}
static_assert(CellIndex(1, 0) == 1, "x avanza de a una celda");
static_assert(CellIndex(0, 1) == TerrainSide, "y avanza de a una fila");
static_assert(CellIndex(255, 255) == TerrainCellCount - 1, "la última celda es la última de la capa");

} // namespace Client::Data::Terrain
