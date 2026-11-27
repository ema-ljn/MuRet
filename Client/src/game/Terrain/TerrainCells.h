#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Render/ModelBatch.h"
#include "Terrain/TerrainLighting.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// A world's terrain as ModelBatch triangles: what the 0x5fc260 chain draws, checked cell by cell against the Lorencia
// trace (06 § Terreno). Each cell is the four-vertex fan of 0x5fac90 as two triangles, with the layers 0x5fa830 picks
// by its corners' alpha, the UVs of 0x5fa5c0 and the light and alpha of each corner from 0x5fa260 and 0x5fa350. The
// grass pass is the same cell with 0x7d0d31c at 2 (06 § El pasto): only the game draws it (on map 55 it draws nothing,
// 06 § El terreno del login). The water of map 7 (0x5fa4b0, with the 5th argument of 0x5fa5c0 at 1) and the backwards
// shift of 0x1e are here; the 0x698 bubbles of 0x22 (0x5fa5c0, with rand()) are not.
namespace Client::Terrain
{
// Tile i is slot i + 0x23 (0x5fa260 and 0x5fa350).
inline constexpr std::uint16_t FirstTileSlot = 0x23;
// A corner's height with bit 0x40 of the attribute (DAT_006c9d5c in the .data).
inline constexpr float FixedCornerHeight = 1200.0f;
static_assert(std::bit_cast<std::uint32_t>(FixedCornerHeight) == 0x44960000, "el valor de DAT_006c9d5c");
// How many times a tile repeats every 64 cells: u advances 64 / width per cell (0x6ba6b0).
inline constexpr float TileSpan = 64.0f;
static_assert(std::bit_cast<std::uint32_t>(TileSpan) == 0x42800000, "la constante de 0x6ba6b0");
// The bits of a cell's attribute the terrain checks (0x5fac90).
inline constexpr std::uint16_t HiddenCellBit = 0x08;
inline constexpr std::uint16_t FixedHeightBit = 0x40;

// The width and height of a tile texture's buffer (+0x20 and +0x24 of its slot): the power of 2.
struct TileSize
{
    float width = 1.0f;
    float height = 1.0f;
};

// What the terrain reads from a world. The light is the shaded one (0x7b4ccc4, which 0x5fbf60 copies from 0x7c0ccc4
// on each step, 06 § Terreno), without dynamic lights.
struct TerrainView
{
    const Data::Terrain::TerrainMapping* mapping = nullptr;
    const Data::Terrain::TerrainHeights* heights = nullptr;
    const Data::Terrain::TerrainAttributes* attributes = nullptr;
    const TerrainLighting* lighting = nullptr;
    std::array<TileSize, 256> tiles{};
    // DAT_006c9dd0: on 0x1e the water runs backwards.
    std::uint8_t map = 0;
    // DAT_006c9d5c, shared with the camera: 1,200, or 1,201 after the first 0x40.
    float fixedHeight = FixedCornerHeight;
    // DAT_07d2d360, the water frame from 0 to 31 that the step advances (ClothWorld::waterTextureFrame): on map 7 the
    // water goes with caustic 0x41 + frame.
    std::uint32_t waterFrame = 0;
};

// The cells of the blocks the camera sees (0x5fbb00): from firstX to lastX and from firstY to lastY, the starts of the
// 4 × 4 blocks (DAT_07d2d364, DAT_006c9d68, DAT_07d2d368 and DAT_006c9d6c).
struct TerrainRange
{
    int firstX = 0;
    int lastX = 0;
    int firstY = 0;
    int lastY = 0;
};

// The water: this frame's u offset (0x78ecbec) and each cell's wave from the last step (0x78acbd8, by grid index).
// Without waves, the water stays still.
struct TerrainWater
{
    float offset = 0.0f;
    std::vector<float> waves;
};

// 0x5fc260, on each frame: (ftol(T) % 20000) · 5e-5, with T the clock in ms; on map 8, (ftol(T) % 40000) · 2.5e-5.
[[nodiscard]] float WaterOffsetAt(Core::Duration clock, std::uint8_t map);

// 0x5fbf60, on each step: the wave of each cell of the window (firstX to lastX + 3 and firstY to lastY + 3) is
// sin(x · 5 + (ftol(T) % 720000) · 0.002) · 10, with the cell's x (x · 50 on map 8); the rest stay as they were. The
// waves start at 0.
void UpdateWaves(TerrainWater& water, Core::Duration clock, const TerrainRange& range, std::uint8_t map);

// Cell (x, y), as 0x5fac90 with 0x5fa830: nothing with bit 0x08; otherwise the opaque lower layer (layer 2 if all four
// corners are at alpha 1, layer 1 if not) and, with some corner above 0: on map 7 with layer 2 at 5, the water of
// 0x5fa4b0 (caustic 0x41 + frame, additive, with UVs of 16 / width per cell and the gray of each corner's alpha,
// without light); otherwise, with some corner below 1, layer 2 with the normal blend and each corner's alpha. The lower
// one is water (the shift and the wave) when it is layer 1 at 5, and the alpha one when layer 2 is 5 too. The corners
// go by index, (x, y), (x + 1, y), (x + 1, y + 1) and (x, y + 1) in the 256 grid: the last cell of a row takes those of
// the next row, as the original does.
void DrawTerrainCell(Render::ModelBatch& batch, const TerrainView& view, const TerrainWater& water, std::uint8_t x,
                     std::uint8_t y);

// The grass of layer 1 at k goes in slot 0x32 + k (0x5faa8c). 0x61a560 uploads TileGrass01..03.tga of World<N>\ into
// 0x32..0x34 (0x63bd00, GL_NEAREST and repeating, without warning if missing).
inline constexpr std::uint16_t FirstGrassSlot = 0x32;
inline constexpr std::size_t GrassTextureCount = 3;
// The rows of the grass offset the pass reads: 0x5faabf takes the row & 0xff (Q-C146).
inline constexpr std::size_t GrassOffsetRows = 256;

// What the grass pass reads besides the terrain (0x5fa830 with 0x7d0d31c at 2).
struct TerrainGrass
{
    // The buffer height (+0x24 of the slot) of each uploaded grass texture, from 0x32 to 0x34; no value for one that is
    // not there (+0x34 at 0): 0x6070a0 releases slots 0x23..0x67 on each load and 0x63bd00 does not upload one that is
    // missing or unreadable.
    std::array<std::optional<float>, GrassTextureCount> heights{};
    // Each row's u offset (0x7b0cca4, which 0x5f8300 fills when the map loads).
    std::span<const float> offsets{};
};

// Cell (x, y) in the grass pass, as 0x5fac90 with 0x5fa830 and 0x7d0d31c at 2: nothing with bit 0x08, with some corner
// at alpha above 0, on maps 11 to 17 or with slot 0x32 + layer 1 without a texture. Otherwise a GL_QUADS with the blend
// of 0x620a00(1) standing on the cell's diagonal: below, corners (x + 1, y + 1) and (x, y) with their height; above,
// the same, 50 less in x and twice the texture buffer's height more in z, with the wave of corner (x, y) and that of (x
// + 1, y) in y (Q-C227). The u is x · 0.25 plus the row's offset and the color is each corner's light in order, with
// alpha 1 (glColor3fv). It fails if the offset does not have the 256 rows or if the slot is 0x41 or more, which MuRet
// does not upload (0x5faaa3: the wt of 0x616ec0, the leaves…); nobody uploads 0x35 to 0x40.
[[nodiscard]] Core::Expected<void, std::string> DrawGrassCell(Render::ModelBatch& batch, const TerrainView& view,
                                                              const TerrainWater& water, const TerrainGrass& grass,
                                                              std::uint8_t x, std::uint8_t y);

} // namespace Client::Terrain
