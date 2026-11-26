#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "EngineApi.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// EncTerrain<N>.att (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo"; the loader at 0x5f8390).
// Two worlds take another number by game state (0x61a6ea, 0x61a710): World31\EncTerrain312.att and
// World35\EncTerrain351.att or 352; their header says 31 or 35, the folder's N. The original keeps the low byte of
// a WORD cell. The whole file is XOR3 under K16: remove K16 first. The original has its own copy of the .bmd tables'
// XOR3 for the .att (0x5f8650), with the same key. It also checks one fixed cell of World1 to World5 against an
// expected value, a defense against modified files that MuRet does not port (Q-C26 in
// Client/docs/research/07-quirks-y-bugs.md).
namespace Client::Data::Terrain
{
// 00, the world's number, FF, FF.
inline constexpr std::size_t TerrainAttributesHeaderSize = 4;
// The original's loader returns it.
inline constexpr std::size_t TerrainAttributesWorldOffset = 1;
// The two sizes the original accepts: the header and one byte per cell, or two.
inline constexpr std::size_t TerrainAttributesSizeWithByteCells = TerrainAttributesHeaderSize + TerrainCellCount;
inline constexpr std::size_t TerrainAttributesSizeWithWordCells = TerrainAttributesHeaderSize + 2 * TerrainCellCount;
static_assert(TerrainAttributesSizeWithByteCells == 0x10004, "el tamaño que mide el research (005f8390)");
static_assert(TerrainAttributesSizeWithWordCells == 0x20004, "el tamaño que mide el research (005f8390)");
// 0x5f85fd compares each cell with 0x80.
inline constexpr std::uint16_t TerrainAttributeLimit = 0x80;

struct TerrainAttributes
{
    // A WORD per cell, as in the original's memory (0x07D0D348): the game changes bits later. Just loaded, none passes
    // 0x7F.
    TerrainLayer<std::uint16_t> cells;
};

// The attributes go on the heap, like the mapping (TerrainMapping.h). Neither size: DataError::UnexpectedSize; the
// original returns -1 silently, its caller does not report the -1 either, and the world keeps the previous map's
// attributes (Q-C25). A header that is not 00 N FF FF or an attribute at TerrainAttributeLimit:
// DataError::InvalidData, the original's "Data error". A header of another world: DataError::WrongWorld; in the
// original the caller shows "%s file corrupted." and closes the game.
ENGINE_API Core::Expected<std::unique_ptr<TerrainAttributes>, DataError>
LoadTerrainAttributes(std::span<const std::byte> file, std::uint8_t world);

// Without comparing the header's world: what Crywolf's BD 00 does, which ignores what the loader returns (0x42ffe8).
ENGINE_API Core::Expected<std::unique_ptr<TerrainAttributes>, DataError>
LoadTerrainAttributesOfAnyWorld(std::span<const std::byte> file);
} // namespace Client::Data::Terrain
