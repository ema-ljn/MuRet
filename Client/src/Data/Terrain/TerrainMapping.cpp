#include "Data/Terrain/TerrainMapping.h"

#include "Core/Expected.h"
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
Core::Expected<std::unique_ptr<TerrainMapping>, DataError> LoadTerrainMapping(std::span<const std::byte> file,
                                                                              std::uint8_t world)
{
    if (file.size() < TerrainMappingSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }

    // K16 es un solo flujo: cada byte depende solo de los anteriores, así que descifrar los
    // primeros TerrainMappingSize da lo mismo que descifrar todo y quedarse con ellos.
    const std::span<const std::byte> encrypted = file.first(TerrainMappingSize);
    std::vector<std::byte> plain(encrypted.begin(), encrypted.end());
    DecryptK16(plain);

    if (std::to_integer<std::uint8_t>(plain[TerrainMappingWorldOffset]) != world)
    {
        return Core::Unexpected(DataError::WrongWorld);
    }

    auto mapping = std::make_unique<TerrainMapping>();
    for (std::size_t cell = 0; cell < TerrainCellCount; ++cell)
    {
        mapping->layer1[cell] = std::to_integer<std::uint8_t>(plain[TerrainMappingLayer1Offset + cell]);
        mapping->layer2[cell] = std::to_integer<std::uint8_t>(plain[TerrainMappingLayer2Offset + cell]);
        const auto alpha = std::to_integer<std::uint8_t>(plain[TerrainMappingAlphaOffset + cell]);
        mapping->alpha[cell] = static_cast<float>(alpha) * TerrainAlphaScale;
    }
    return mapping;
}
} // namespace Client::Data::Terrain
