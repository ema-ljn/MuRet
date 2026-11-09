#include "World/TerrainAttributeQuery.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "World/Ftol.h"
#include "World/ITerrainAttributeQuerySource.h"

#include <cstddef>
#include <cstdint>

namespace Client::World
{
namespace
{
// 0x5f82c0: base nativa de la misma capa WORD que TerrainAttributes::cells.
constexpr std::uint32_t AttributeBase = 0x07d0d348;
constexpr unsigned RowShift = 8;
constexpr unsigned WordShift = 1;
constexpr std::uint32_t HighHalfMask = 0xffff0000;
constexpr std::uint32_t AttributeBytes = Data::Terrain::TerrainCellCount * sizeof(std::uint16_t);
} // namespace

Core::Expected<std::uint32_t, TerrainAttributeQueryError> TerrainAttributeQueryAt(ITerrainAttributeQuerySource& source,
                                                                                  float x, float y)
{
    const double productY = static_cast<double>(y) * static_cast<double>(source.ReadScale());
    const auto row = static_cast<std::uint32_t>(FtolLow(productY));
    const double productX = static_cast<double>(x) * static_cast<double>(source.ReadScale());
    const auto column = static_cast<std::uint32_t>(FtolLow(productX));
    const std::uint32_t index = (row << RowShift) + column;
    const std::uint32_t address = AttributeBase - (index << WordShift);
    const auto& attributes = source.Attributes();
    // Primero el wrap nativo; sólo después se comprueba lo representable por el port.
    const std::uint32_t offset = address - AttributeBase;
    if (offset >= AttributeBytes)
    {
        return Core::Unexpected(TerrainAttributeQueryError::UnrepresentedAddress);
    }
    const auto cell = static_cast<std::size_t>(offset >> WordShift);
    const std::uint16_t word = attributes.cells[cell];
    return (address & HighHalfMask) | word;
}
} // namespace Client::World
