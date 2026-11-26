#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"
#include "EngineApi.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// TerrainLight.OZJ (Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo"; the loader at 0x63b490). An
// OZJ like the textures' (OzjTexture.h), decoded the same way with libjpeg's defaults, but not uploaded: each byte
// becomes a float from 0 to 1 in a 256 × 256 table of RGB colors (0x078ecc10) that the terrain uses (0x5f8d90). Unlike
// the texture, the table's row 0 is the JPEG's last row (0063b490-terrainlight-ozj-loader.md §1), and the JPEG's size
// and components are not checked (Q-C31); in the lab all 24 TerrainLight are 256 × 256 color.
namespace Client::Data::Terrain
{
// The constant at 0x6b88f8, 1/255 in float. 255 times it gives exactly 1, so the light goes from 0 to 1.
inline constexpr float TerrainLightScale = std::bit_cast<float>(std::uint32_t{0x3B808081});
static_assert(TerrainLightScale == 1.0f / 255.0f, "la constante de 0x6b88f8 es 1/255 (0063b490 §7)");
static_assert(255.0f * TerrainLightScale == 1.0f, "el byte más alto da una luz de 1 (0063b490 §7)");

// As they come out of the JPEG.
inline constexpr std::size_t TerrainLightChannels = 3;
using TerrainLightColor = std::array<float, TerrainLightChannels>;
static_assert(sizeof(TerrainLightColor) == TerrainLightChannels * sizeof(float),
              "los colores van seguidos, como en la tabla del original");

struct TerrainLight
{
    // 256 × 256 × 3 floats, the 0xC0000 bytes of the original's table.
    TerrainLayer<TerrainLightColor> cells;
};
static_assert(sizeof(TerrainLight) == 0xC0000, "el tamaño de la tabla del original (0063b490 §1)");

// The light takes 768 KB and goes on the heap, like the heights (TerrainHeights.h). A `file` no longer than the 24
// unread bytes is DataError::FileTooShort. If libjpeg stops before the last row, DataError::InvalidData: the original
// returns false without touching the table and its caller (0x5f8f40) ignores it, so the old light stays (Q-C32). In a
// single-scan JPEG what follows the last row is not read: when the original reads it, the table is already written.
// A JPEG that is not 256 × 256 color is DataError::InvalidData; the original does not check it and writes outside
// the table or reads outside the image (Q-C31).
ENGINE_API Core::Expected<std::unique_ptr<TerrainLight>, DataError> LoadTerrainLight(std::span<const std::byte> file);
} // namespace Client::Data::Terrain
