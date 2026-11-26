#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "EngineApi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// EncTerrain<N>.obj, a world's trees, houses and rocks (Client/docs/research/05-formatos-de-datos.md, "Terreno de un
// mundo"; the loader at 0x607370). Under K16 end to end, one stream, no XOR3, like the .map. Byte 0 is not read
// (the version?). Each object is 30 bytes with no padding between them: the type (int16), the position (3 floats),
// the angle (3 floats) and the scale (1 float), all little-endian.
namespace Client::Data::Terrain
{
// The original's loader returns it (0x6074ad).
inline constexpr std::size_t TerrainObjectsWorldOffset = 1;
// The original reads it signed (`movsx` at 0x6074a9).
inline constexpr std::size_t TerrainObjectsCountOffset = 2;
// The unread byte, the world and the count; the objects follow (00607370-encterrain-obj.md §2).
inline constexpr std::size_t TerrainObjectsHeaderSize = TerrainObjectsCountOffset + sizeof(std::int16_t);
static_assert(TerrainObjectsHeaderSize == 4, "la cabecera que mide el research (00607370 §2)");
// An int16 and seven floats: the original steps 30 bytes (00607370-encterrain-obj.md §2).
inline constexpr std::size_t TerrainObjectSize = sizeof(std::int16_t) + 7 * sizeof(float);
static_assert(TerrainObjectSize == 30, "el registro que mide el research (00607370 §2)");

struct TerrainObject
{
    // 0 to 153 in real files; World7 has one at -515 (Q-C28).
    std::int16_t type = 0;
    // In world units, 100 per cell: x and y over the grid, z is the height.
    std::array<float, 3> position{};
    // Probably in degrees.
    std::array<float, 3> angle{};
    // 1.0f is the model's size. Real files have some negative or zero (Q-C28).
    float scale = 0.0f;
};

// In file order. Each float keeps the file's bits, even a NaN: Devias has two (Q-C28). Which objects are created and
// which dropped is decided later, like the original's 0x606660. A zero or negative count means no objects; the
// original still stores the negative number (0x6074b0) and the research found no reader (00607370 §1 and §7). A
// `file` short of the header or of the objects it declares is DataError::FileTooShort: the original ignores the size
// and would read past the buffer (Q-C24). Anything longer is ignored, as in the original. If byte 1 is not `world`,
// DataError::WrongWorld: the original creates the objects anyway, and its caller shows "%s file corrupted." and
// closes the game.
ENGINE_API Core::Expected<std::vector<TerrainObject>, DataError> LoadTerrainObjects(std::span<const std::byte> file,
                                                                                    std::uint8_t world);
} // namespace Client::Data::Terrain
