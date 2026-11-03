#include "Data/Terrain/TerrainAttributes.h"

#include "Core/Expected.h"
#include "Core/Xor3.h"
#include "Data/DataError.h"
#include "Data/Terrain/K16.h"
#include "Data/Terrain/TerrainGrid.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Client::Data::Terrain
{
namespace
{
// La cabecera en claro es 00 N FF FF (005f8390-encterrain-att.md, "Formato"): el primer byte y
// los dos últimos son fijos, y el del medio es el mundo, que se mira aparte.
constexpr std::byte HeaderStart{0x00};
constexpr std::byte HeaderEnd{0xFF};

bool IsTheHeader(std::span<const std::byte> plain)
{
    return plain[0] == HeaderStart && plain[2] == HeaderEnd && plain[3] == HeaderEnd;
}
} // namespace

Core::Expected<std::unique_ptr<TerrainAttributes>, DataError> LoadTerrainAttributes(std::span<const std::byte> file,
                                                                                    std::uint8_t world)
{
    if (file.size() != TerrainAttributesSizeWithByteCells && file.size() != TerrainAttributesSizeWithWordCells)
    {
        return Core::Unexpected(DataError::UnexpectedSize);
    }

    std::vector<std::byte> plain(file.begin(), file.end());
    DecryptK16(plain);
    Core::Xor3(plain);
    if (!IsTheHeader(plain))
    {
        return Core::Unexpected(DataError::InvalidData);
    }

    // Con dos bytes por celda, el de abajo es el primero (little-endian) y el de arriba se tira.
    const std::size_t cellSize = file.size() == TerrainAttributesSizeWithWordCells ? 2 : 1;
    auto attributes = std::make_unique<TerrainAttributes>();
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        const auto low = std::to_integer<std::uint16_t>(plain[TerrainAttributesHeaderSize + cell * cellSize]);
        if (low >= TerrainAttributeLimit)
        {
            return Core::Unexpected(DataError::InvalidData);
        }
        attributes->cells[cell] = low;
    }

    if (std::to_integer<std::uint8_t>(plain[TerrainAttributesWorldOffset]) != world)
    {
        return Core::Unexpected(DataError::WrongWorld);
    }
    return attributes;
}
} // namespace Client::Data::Terrain
