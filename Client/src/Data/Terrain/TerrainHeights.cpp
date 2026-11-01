#include "Data/Terrain/TerrainHeights.h"

namespace Client::Data::Terrain
{

Core::Expected<std::unique_ptr<TerrainHeights>, DataError> LoadTerrainHeights(std::span<const std::byte> file,
                                                                              std::uint8_t world)
{
    if (file.size() < TerrainHeightsSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }

    const float scale =
        world == TerrainHeightsWorldWithDoubleScale ? TerrainHeightsScaleOfWorld56 : TerrainHeightsScale;
    const std::span<const std::byte> values = file.subspan(TerrainHeightsValuesOffset, TerrainCellCount);
    auto heights = std::make_unique<TerrainHeights>();
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        heights->cells[cell] = static_cast<float>(std::to_integer<std::uint8_t>(values[cell])) * scale;
    }
    return heights;
}

} // namespace Client::Data::Terrain
