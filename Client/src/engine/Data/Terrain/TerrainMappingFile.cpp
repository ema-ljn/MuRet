#include "Data/Terrain/TerrainMappingFile.h"

#include "Core/Expected.h"
#include "Crypto/K16.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainMapping.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace Client::Data::Terrain
{
namespace
{
void ReadLayer(std::span<const std::byte> plain, std::size_t offset, TerrainLayer<std::uint8_t>& layer)
{
    std::ranges::transform(plain.subspan(offset, TerrainCellCount), layer.begin(),
                           [](std::byte value) { return std::to_integer<std::uint8_t>(value); });
}

void WriteLayer(const TerrainLayer<std::uint8_t>& layer, std::size_t offset, std::span<std::byte> plain)
{
    std::ranges::transform(layer, plain.subspan(offset, TerrainCellCount).begin(),
                           [](std::uint8_t value) { return std::byte{value}; });
}
} // namespace

Core::Expected<std::unique_ptr<TerrainMappingFile>, DataError> ReadTerrainMappingFile(std::span<const std::byte> file)
{
    if (file.size() < TerrainMappingSize)
    {
        return Core::Unexpected(DataError::FileTooShort);
    }

    std::vector<std::byte> plain(file.begin(), file.end());
    Crypto::DecryptK16(plain);

    auto mapping = std::make_unique<TerrainMappingFile>();
    mapping->version = std::to_integer<std::uint8_t>(plain[0]);
    mapping->world = std::to_integer<std::uint8_t>(plain[TerrainMappingWorldOffset]);
    ReadLayer(plain, TerrainMappingLayer1Offset, mapping->layer1);
    ReadLayer(plain, TerrainMappingLayer2Offset, mapping->layer2);
    ReadLayer(plain, TerrainMappingAlphaOffset, mapping->alpha);
    mapping->tail.assign(plain.begin() + static_cast<std::ptrdiff_t>(TerrainMappingSize), plain.end());
    return mapping;
}

// 0x5f8960 writes the plain file, then encrypts it whole with K16.
std::vector<std::byte> WriteTerrainMappingFile(const TerrainMappingFile& mapping)
{
    std::vector<std::byte> file(TerrainMappingSize);
    file[0] = std::byte{mapping.version};
    file[TerrainMappingWorldOffset] = std::byte{mapping.world};
    WriteLayer(mapping.layer1, TerrainMappingLayer1Offset, file);
    WriteLayer(mapping.layer2, TerrainMappingLayer2Offset, file);
    WriteLayer(mapping.alpha, TerrainMappingAlphaOffset, file);
    file.insert(file.end(), mapping.tail.begin(), mapping.tail.end());
    Crypto::EncryptK16(file);
    return file;
}
} // namespace Client::Data::Terrain
