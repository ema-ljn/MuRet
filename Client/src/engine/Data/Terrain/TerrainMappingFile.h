#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// EncTerrain<N>.map whole, for writing it back (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo";
// read by 0x5f87b0, written by 0x5f8960). The values are the file's bytes; LoadTerrainMapping makes the game's view.
namespace Client::Data::Terrain
{
struct TerrainMappingFile
{
    std::uint8_t version = 0;
    std::uint8_t world = 0;
    TerrainLayer<std::uint8_t> layer1{};
    TerrainLayer<std::uint8_t> layer2{};
    TerrainLayer<std::uint8_t> alpha{};
    // The plain bytes after TerrainMappingSize, which the loader ignores.
    std::vector<std::byte> tail;
};

// Below TerrainMappingSize: DataError::FileTooShort. The world byte is kept, not checked.
ENGINE_API Core::Expected<std::unique_ptr<TerrainMappingFile>, DataError>
ReadTerrainMappingFile(std::span<const std::byte> file);

[[nodiscard]] ENGINE_API std::vector<std::byte> WriteTerrainMappingFile(const TerrainMappingFile& mapping);
} // namespace Client::Data::Terrain
