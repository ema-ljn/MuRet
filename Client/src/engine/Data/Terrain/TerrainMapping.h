#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "EngineApi.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// EncTerrain<N>.map (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo"; the loader at 0x5f87b0):
// 0x30002 bytes under K16 end to end, one stream, no XOR3. Byte 0 is the version, which the original does not read
// (its exporter writes 0). What each layer is comes from what its readers do with it; the research calls it probable.
// A missing file still loads the world with defaults, silently (L1 = 0, L2 = 0xFF, alpha = 0; Q-C27): that belongs
// to whoever opens the file, like the rand() table the same 0x5f8300 fills.
namespace Client::Data::Terrain
{
// The original's loader returns it (0x5f88b5).
inline constexpr std::size_t TerrainMappingWorldOffset = 1;
// One after the other, after the 2 header bytes (005f87b0-encterrain-map.md §3). Encryption does not move them.
inline constexpr std::size_t TerrainMappingLayer1Offset = 2;
inline constexpr std::size_t TerrainMappingLayer2Offset = TerrainMappingLayer1Offset + TerrainCellCount;
inline constexpr std::size_t TerrainMappingAlphaOffset = TerrainMappingLayer2Offset + TerrainCellCount;
static_assert(TerrainMappingLayer2Offset == 0x10002, "el offset que mide el research (005f87b0 §3)");
static_assert(TerrainMappingAlphaOffset == 0x20002, "el offset que mide el research (005f87b0 §3)");
inline constexpr std::size_t TerrainMappingSize = TerrainMappingAlphaOffset + TerrainCellCount;
static_assert(TerrainMappingSize == 0x30002, "el tamaño que mide el research (05, Terreno de un mundo)");

// The original's constant (0x6b88f8), 1/255 rounded to float. Multiplying by it is not always dividing by 255: with
// 51 one gives 0x3E4CCCCE and the other 0.2f (0x3E4CCCCD). 255 gives exactly 1.0f, which matters: the renderer
// compares the alpha with 0.0f and 1.0f (0x5fa830).
inline constexpr float TerrainAlphaScale = 1.0f / 255.0f;
static_assert(std::bit_cast<std::uint32_t>(TerrainAlphaScale) == 0x3B808081, "la constante de 0x6b88f8");

struct TerrainMapping
{
    // The original gives it the texture `tile + 0x23`; 5 is water.
    TerrainLayer<std::uint8_t> layer1;
    // Blended over layer 1; 0xFF is none, though with alpha the original draws it anyway, with a texture that is not a
    // tile (Q-C19).
    TerrainLayer<std::uint8_t> layer2;
    // From 0.0f to 1.0f: the file's byte times TerrainAlphaScale. Real files have cells with alpha and layer 2 at 0xFF:
    // one does not imply the other.
    TerrainLayer<float> alpha;
};

// The mapping takes 384 KB and goes on the heap: it does not fit the stack comfortably, and an Expected would copy it,
// since moving a std::array moves its bytes and floats one by one. A shorter `file` is DataError::FileTooShort: the
// original ignores the size and would read past the buffer (Q-C24 in Client/docs/research/07-quirks-y-bugs.md).
// Anything longer is ignored, as in the original. If byte 1 is not `world`, DataError::WrongWorld: the original loads
// the layers anyway, and its caller shows "%s file corrupted." and closes the game.
ENGINE_API Core::Expected<std::unique_ptr<TerrainMapping>, DataError>
LoadTerrainMapping(std::span<const std::byte> file, std::uint8_t world);
} // namespace Client::Data::Terrain
