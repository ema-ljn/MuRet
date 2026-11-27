#include "Terrain/TerrainHeight.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Math/Ftol.h"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Client::Terrain
{
namespace
{
constexpr float CellsPerUnit = 0.01f; // 0x6b86c0
constexpr std::uint16_t FixedHeightBit = 0x40;
constexpr int Side = 256;

double At(const Data::Terrain::TerrainHeights& heights, int index)
{
    return static_cast<double>(heights.cells[static_cast<std::size_t>(index)]);
}
} // namespace

double TerrainHeightAt(const Data::Terrain::TerrainHeights& heights, const Data::Terrain::TerrainAttributes& attributes,
                       float x, float y, float fixedHeight)
{
    // `test ah, 1` after comparing with 0: less, or unordered.
    if (x < 0.0f || std::isnan(x) || y < 0.0f || std::isnan(y))
    {
        return 0.0;
    }
    // 0x5f940f: x is stored as float (fstp) and ftol reads it from there; y with fst, which leaves it on the stack:
    // ftol truncates the product in 53 bits (20,000 · 0.01f, 199.9999955, is 200 as float).
    const auto cellX = static_cast<float>(static_cast<double>(x) * static_cast<double>(CellsPerUnit));
    const double productY = static_cast<double>(y) * static_cast<double>(CellsPerUnit);
    const auto cellY = static_cast<float>(productY);
    const std::int32_t row = Math::FtolLow(productY);
    const std::int32_t column = Math::FtolLow(static_cast<double>(cellX));
    // 0x5f9436: the row times 256 plus the column, in 32 bits, and a signed `jl` against 0x10000. With a negative index
    // (it can happen from row 2^23) the original reads before the attribute table: MuRet gives 1,200, as outside the
    // grid.
    const auto index =
        static_cast<std::int32_t>((static_cast<std::uint32_t>(row) << 8U) + static_cast<std::uint32_t>(column));
    if (index < 0 || index >= static_cast<std::int32_t>(Data::Terrain::TerrainCellCount) ||
        (attributes.cells[static_cast<std::size_t>(index)] & FixedHeightBit) == FixedHeightBit)
    {
        return static_cast<double>(fixedHeight);
    }
    const auto fractionY = static_cast<float>(static_cast<double>(cellY) - static_cast<double>(row));
    // 0x5f946d: `inc` and `and 0xff`, in 32 bits.
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
} // namespace Client::Terrain
