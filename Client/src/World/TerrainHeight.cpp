#include "World/TerrainHeight.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Client::World
{
namespace
{
constexpr float CellsPerUnit = 0.01f; // 0x6b86c0
constexpr std::uint16_t FixedHeightBit = 0x40;
constexpr int Side = 256;

// 0x69fd3c (_ftol): truncar a 64 bits y quedarse con la parte baja; con un NaN o fuera de rango, 0.
std::int32_t Ftol(float value)
{
    constexpr float Limit = 9.2233720368547758e18f;
    if (std::isnan(value) || value >= Limit || value < -Limit)
    {
        return 0;
    }
    return static_cast<std::int32_t>(
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(static_cast<std::int64_t>(value))));
}

double At(const Data::Terrain::TerrainHeights& heights, int index)
{
    return static_cast<double>(heights.cells[static_cast<std::size_t>(index)]);
}
} // namespace

double TerrainHeightAt(const Data::Terrain::TerrainHeights& heights, const Data::Terrain::TerrainAttributes& attributes,
                       float x, float y)
{
    // `test ah, 1` después de comparar con 0: menor, o sin orden.
    if (x < 0.0f || std::isnan(x) || y < 0.0f || std::isnan(y))
    {
        return 0.0;
    }
    const auto cellX = static_cast<float>(static_cast<double>(x) * static_cast<double>(CellsPerUnit));
    const auto cellY = static_cast<float>(static_cast<double>(y) * static_cast<double>(CellsPerUnit));
    const std::int32_t row = Ftol(cellY);
    const std::int32_t column = Ftol(cellX);
    const std::int64_t index = (static_cast<std::int64_t>(row) * Side) + column;
    if (index >= static_cast<std::int64_t>(Data::Terrain::TerrainCellCount) ||
        (attributes.cells[static_cast<std::size_t>(index)] & FixedHeightBit) == FixedHeightBit)
    {
        return static_cast<double>(FixedTerrainHeight);
    }
    const auto fractionY = static_cast<float>(static_cast<double>(cellY) - static_cast<double>(row));
    const int r0 = (row & 0xff) * Side;
    const int r1 = ((row + 1) & 0xff) * Side;
    const int c0 = column & 0xff;
    const int c1 = (column + 1) & 0xff;
    const double left =
        ((At(heights, r1 + c0) - At(heights, r0 + c0)) * static_cast<double>(fractionY)) + At(heights, r0 + c0);
    const double right =
        ((At(heights, r1 + c1) - At(heights, r0 + c1)) * static_cast<double>(fractionY)) + At(heights, r0 + c1);
    const double fractionX = static_cast<double>(cellX) - static_cast<double>(column);
    return ((right - left) * fractionX) + left;
}
} // namespace Client::World
