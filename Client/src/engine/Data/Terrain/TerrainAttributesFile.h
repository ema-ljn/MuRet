#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

// EncTerrain<N>.att whole, for writing it back (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo";
// read by 0x5f8390). Written in the layout the loader reads, the header 00 N FF FF and the cells, under XOR3 and then
// K16: 0x5f8690 writes the header and the first 0x10000 bytes of its WORD table, unencrypted (Q-C1831).
// LoadTerrainAttributes makes the game's view.
namespace Client::Data::Terrain
{
struct TerrainAttributesFile
{
    std::uint8_t world = 0;
    // The byte the game reads: the low one of a WORD cell.
    TerrainLayer<std::uint8_t> cells{};
    // Only in a file with two bytes per cell (0x20004); the original drops them.
    std::optional<TerrainLayer<std::uint8_t>> highBytes;
};

// Neither size: DataError::UnexpectedSize. A header that is not 00 N FF FF: DataError::InvalidData. Neither the world
// nor the cells' values are checked.
ENGINE_API Core::Expected<std::unique_ptr<TerrainAttributesFile>, DataError>
ReadTerrainAttributesFile(std::span<const std::byte> file);

[[nodiscard]] ENGINE_API std::vector<std::byte> WriteTerrainAttributesFile(const TerrainAttributesFile& attributes);
} // namespace Client::Data::Terrain
