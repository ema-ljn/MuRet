#include "Data/Terrain/TerrainAttributesFile.h"

#include "Core/Expected.h"
#include "Crypto/K16.h"
#include "Crypto/Xor3.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainAttributes.h"
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
constexpr std::byte HeaderStart{0x00};
constexpr std::byte HeaderEnd{0xFF};
constexpr std::size_t ByteCell = 1;
constexpr std::size_t WordCell = 2;

bool IsTheHeader(std::span<const std::byte> plain)
{
    return plain[0] == HeaderStart && plain[2] == HeaderEnd && plain[3] == HeaderEnd;
}

std::size_t CellOffset(std::size_t cell, std::size_t cellSize)
{
    return TerrainAttributesHeaderSize + cell * cellSize;
}
} // namespace

Core::Expected<std::unique_ptr<TerrainAttributesFile>, DataError>
ReadTerrainAttributesFile(std::span<const std::byte> file)
{
    if (file.size() != TerrainAttributesSizeWithByteCells && file.size() != TerrainAttributesSizeWithWordCells)
    {
        return Core::Unexpected(DataError::UnexpectedSize);
    }

    std::vector<std::byte> plain(file.begin(), file.end());
    Crypto::DecryptK16(plain);
    Crypto::Xor3(plain);
    if (!IsTheHeader(plain))
    {
        return Core::Unexpected(DataError::InvalidData);
    }

    const std::size_t cellSize = file.size() == TerrainAttributesSizeWithWordCells ? WordCell : ByteCell;
    auto attributes = std::make_unique<TerrainAttributesFile>();
    attributes->world = std::to_integer<std::uint8_t>(plain[TerrainAttributesWorldOffset]);
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        attributes->cells[cell] = std::to_integer<std::uint8_t>(plain[CellOffset(cell, cellSize)]);
    }
    if (cellSize == WordCell)
    {
        TerrainLayer<std::uint8_t>& high = attributes->highBytes.emplace();
        for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
        {
            high[cell] = std::to_integer<std::uint8_t>(plain[CellOffset(cell, cellSize) + 1]);
        }
    }
    return attributes;
}

std::vector<std::byte> WriteTerrainAttributesFile(const TerrainAttributesFile& attributes)
{
    const bool words = attributes.highBytes.has_value();
    const std::size_t cellSize = words ? WordCell : ByteCell;
    std::vector<std::byte> file(words ? TerrainAttributesSizeWithWordCells : TerrainAttributesSizeWithByteCells);
    file[0] = HeaderStart;
    file[TerrainAttributesWorldOffset] = std::byte{attributes.world};
    file[2] = HeaderEnd;
    file[3] = HeaderEnd;
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        file[CellOffset(cell, cellSize)] = std::byte{attributes.cells[cell]};
        if (words)
        {
            file[CellOffset(cell, cellSize) + 1] = std::byte{(*attributes.highBytes)[cell]};
        }
    }
    Crypto::Xor3(file);
    Crypto::EncryptK16(file);
    return file;
}
} // namespace Client::Data::Terrain
