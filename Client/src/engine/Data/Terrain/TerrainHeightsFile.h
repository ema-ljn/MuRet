#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "EngineApi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// TerrainHeight.OZB whole, for writing it back (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo";
// read by 0x5f9020). Unencrypted: 0x5f92b0 writes back the BMP header it read (0x07d0ced8) and the heights in table
// order, and 0x63b280 puts the BMP's first 4 bytes before it. LoadTerrainHeights makes the game's view.
namespace Client::Data::Terrain
{
struct TerrainHeightsFile
{
    std::array<std::byte, TerrainHeightsPrefixSize> prefix{};
    std::array<std::byte, TerrainHeightsBitmapHeaderSize> bitmapHeader{};
    TerrainLayer<std::uint8_t> heights{};
    // The bytes after the heights, which the loader ignores.
    std::vector<std::byte> tail;
};

// Below TerrainHeightsSize: DataError::FileTooShort. The header is kept as it is, not read.
ENGINE_API Core::Expected<std::unique_ptr<TerrainHeightsFile>, DataError>
ReadTerrainHeightsFile(std::span<const std::byte> file);

[[nodiscard]] ENGINE_API std::vector<std::byte> WriteTerrainHeightsFile(const TerrainHeightsFile& heights);
} // namespace Client::Data::Terrain
