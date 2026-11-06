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

// 0x69fd3c (_ftol): truncar a 64 bits y quedarse con la parte baja; con un NaN o fuera de rango, 0. Recibe
// lo que haya en la pila de la x87 (en 53 bits).
std::int32_t Ftol(double value)
{
    constexpr double Limit = 9.2233720368547758e18;
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
                       float x, float y, float fixedHeight)
{
    // `test ah, 1` después de comparar con 0: menor, o sin orden.
    if (x < 0.0f || std::isnan(x) || y < 0.0f || std::isnan(y))
    {
        return 0.0;
    }
    // 0x5f940f: la x se guarda en float (fstp) y ftol la lee de ahí; la y, con fst, que la deja en la
    // pila: ftol trunca el producto en 53 bits (20.000 · 0,01f, 199,9999955, en float es 200).
    const auto cellX = static_cast<float>(static_cast<double>(x) * static_cast<double>(CellsPerUnit));
    const double productY = static_cast<double>(y) * static_cast<double>(CellsPerUnit);
    const auto cellY = static_cast<float>(productY);
    const std::int32_t row = Ftol(productY);
    const std::int32_t column = Ftol(static_cast<double>(cellX));
    // 0x5f9436: la fila por 256 más la columna, en 32 bits, y `jl` con signo contra 0x10000. Con un índice
    // negativo (desde la fila 2^23 puede pasar) el original lee antes de la tabla de atributos: MuRet da
    // 1.200, como fuera de la grilla.
    const auto index =
        static_cast<std::int32_t>((static_cast<std::uint32_t>(row) << 8U) + static_cast<std::uint32_t>(column));
    if (index < 0 || index >= static_cast<std::int32_t>(Data::Terrain::TerrainCellCount) ||
        (attributes.cells[static_cast<std::size_t>(index)] & FixedHeightBit) == FixedHeightBit)
    {
        return static_cast<double>(fixedHeight);
    }
    const auto fractionY = static_cast<float>(static_cast<double>(cellY) - static_cast<double>(row));
    // 0x5f946d: `inc` y `and 0xff`, en 32 bits.
    const auto low = [](std::int32_t value, std::uint32_t plus)
    { return static_cast<int>((static_cast<std::uint32_t>(value) + plus) & 0xffU); };
    const int r0 = low(row, 0U) * Side;
    const int r1 = low(row, 1U) * Side;
    const int c0 = low(column, 0U);
    const int c1 = low(column, 1U);
    const double left =
        ((At(heights, r1 + c0) - At(heights, r0 + c0)) * static_cast<double>(fractionY)) + At(heights, r0 + c0);
    const double right =
        ((At(heights, r1 + c1) - At(heights, r0 + c1)) * static_cast<double>(fractionY)) + At(heights, r0 + c1);
    const double fractionX = static_cast<double>(cellX) - static_cast<double>(column);
    return ((right - left) * fractionX) + left;
}
} // namespace Client::World
