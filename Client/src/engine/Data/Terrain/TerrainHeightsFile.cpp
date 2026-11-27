#include "Data/Terrain/TerrainHeightsFile.h"

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <vector>

namespace Client::Data::Terrain
{
Core::Expected<std::unique_ptr<TerrainHeightsFile>, DataError> ReadTerrainHeightsFile(std::span<const std::byte> file)
{
    if (file.size() < TerrainHeightsSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }

    auto heights = std::make_unique<TerrainHeightsFile>();
    std::ranges::copy(file.first(TerrainHeightsPrefixSize), heights->prefix.begin());
    std::ranges::copy(file.subspan(TerrainHeightsPrefixSize, TerrainHeightsBitmapHeaderSize),
                      heights->bitmapHeader.begin());
    std::ranges::transform(file.subspan(TerrainHeightsValuesOffset, TerrainCellCount), heights->heights.begin(),
                           [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
    const std::span<const std::byte> tail = file.subspan(TerrainHeightsSize);
    heights->tail.assign(tail.begin(), tail.end());
    return heights;
}

std::vector<std::byte> WriteTerrainHeightsFile(const TerrainHeightsFile& heights)
{
    std::vector<std::byte> file;
    file.reserve(TerrainHeightsSize + heights.tail.size());
    file.insert(file.end(), heights.prefix.begin(), heights.prefix.end());
    file.insert(file.end(), heights.bitmapHeader.begin(), heights.bitmapHeader.end());
    std::ranges::transform(heights.heights, std::back_inserter(file),
                           [](std::uint8_t value) { return std::byte{value}; });
    file.insert(file.end(), heights.tail.begin(), heights.tail.end());
    return file;
}
} // namespace Client::Data::Terrain
