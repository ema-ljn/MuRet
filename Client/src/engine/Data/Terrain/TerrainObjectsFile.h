#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainObjects.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// EncTerrain<N>.obj whole, for writing it back (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo";
// read by 0x607370). Written in the model's order with the real count: 0x607540 writes by sector and counts deleted
// objects too (Q-C1830). LoadTerrainObjects makes the game's view.
namespace Client::Data::Terrain
{
struct TerrainObjectsFile
{
    std::uint8_t version = 0;
    std::uint8_t world = 0;
    std::vector<TerrainObject> objects;
    // The plain bytes after the last object, which the loader ignores.
    std::vector<std::byte> tail;
};

// Shorter than the header or than its count of objects: DataError::FileTooShort. A negative count, which the loader
// reads as none: DataError::InvalidData, since it could not be written back the same. The world byte is kept, not
// checked; every float keeps its bits.
ENGINE_API Core::Expected<TerrainObjectsFile, DataError> ReadTerrainObjectsFile(std::span<const std::byte> file);

// More objects than an int16 count holds: DataError::InvalidData.
ENGINE_API Core::Expected<std::vector<std::byte>, DataError> WriteTerrainObjectsFile(const TerrainObjectsFile& objects);
} // namespace Client::Data::Terrain
