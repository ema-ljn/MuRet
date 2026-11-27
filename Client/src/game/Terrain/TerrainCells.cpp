#include "Terrain/TerrainCells.h"

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Math/RoundedMath.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Terrain
{
namespace
{
using Data::Terrain::TerrainCellCount;
using Data::Terrain::TerrainSide;

// A grid index of a corner. With row 255 the lower one leaves the layer: the original reads the memory after it;
// MuRet wraps around the layer (row 0). No scene draws it.
std::size_t Wrapped(std::size_t index)
{
    return index % TerrainCellCount;
}

// The cell's four corners: (x, y), (x + 1, y), (x + 1, y + 1) and (x, y + 1), by index (0x5fac90: DAT_078acbc4,
// DAT_078acbc8, DAT_078acbcc and DAT_078acbd0).
std::array<std::size_t, 4> CornersOf(std::uint8_t x, std::uint8_t y)
{
    const std::size_t first = (static_cast<std::size_t>(y) * TerrainSide) + x;
    const std::size_t below = first + TerrainSide;
    return {first, Wrapped(first + 1), Wrapped(below + 1), Wrapped(below)};
}

// TileWater01.
constexpr std::uint8_t WaterTile = 5;
// The map where the water runs backwards (0x5fa5c0).
constexpr std::uint8_t ReversedWaterMap = 0x1e;
// The wave on v: times 0.002 (0x6b89bc).
constexpr float WaveToV = 0.002f;
static_assert(std::bit_cast<std::uint32_t>(WaveToV) == 0x3B03126F, "la constante de 0x6b89bc");
// The 0x5fc260 offset: 5e-5 (0x6b8a28) every 20 s and, on map 8, 2.5e-5 (0x6ba710) every 40 s.
constexpr float WaterStep = std::bit_cast<float>(0x3851B717u);
constexpr float SlowWaterStep = std::bit_cast<float>(0x37D1B717u);
constexpr std::int64_t WaterPeriodMs = 20000;
constexpr std::int64_t SlowWaterPeriodMs = 40000;
constexpr std::uint8_t SlowWaterMap = 8;
// The wave of 0x5fbf60: sin(x · 5 (0x6b87f8; 50 on map 8, 0x6b86b0) + (ftol(T) % 720000) · 0.002) · 10 (0x6b87a8).
constexpr std::int64_t WavePeriodMs = 720000;
constexpr double WaveColumn = 5.0;
constexpr double SlowWaveColumn = 50.0;
constexpr double WaveHeight = 10.0;

// The grass pass (0x5fa9f6..0x5fac7b): u of 0.25 per cell (0x6b89e0), the top x 50 less (0x6b86b0) and v of 0 on top
// and 1 below; the Blood Castle maps do not draw it (0x5faa79).
constexpr float GrassStep = 0.25f;
static_assert(std::bit_cast<std::uint32_t>(GrassStep) == 0x3E800000, "la constante de 0x6b89e0");
constexpr float GrassLean = 50.0f;
static_assert(std::bit_cast<std::uint32_t>(GrassLean) == 0x42480000, "la constante de 0x6b86b0");
constexpr float GrassTop = 0.0f;
constexpr float GrassBottom = 1.0f;
constexpr std::uint8_t FirstWithoutGrassCellsMap = 11;
constexpr std::uint8_t LastWithoutGrassCellsMap = 17;
// The first slot 0x61a560 does not leave empty after the grass: the wt00..31 of 0x616ec0 (0x41..0x60).
constexpr std::uint16_t FirstUnknownGrassSlot = 0x41;

std::int64_t Milliseconds(Core::Duration clock)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock).count();
}

// A corner's height (0x5fac90): the fixed one with bit 0x40, the .OZB's otherwise.
float CornerHeight(const TerrainView& view, std::size_t corner)
{
    return (view.attributes->cells[corner] & FixedHeightBit) != 0 ? view.fixedHeight : view.heights->cells[corner];
}

// A corner's wave (0x78acbd8): without waves, 0, as in the .bss.
float WaveAt(const TerrainWater& water, std::size_t corner)
{
    return water.waves.size() == TerrainCellCount ? water.waves[corner] : 0.0f;
}

// An fld of a float, an fadd of another and the fstp to a float: the sum in double, rounded once.
float StoredSum(float first, float second)
{
    return static_cast<float>(static_cast<double>(first) + static_cast<double>(second));
}

// The UVs of a tile's four corners in cell (x, y), as 0x5fa5c0 does: u advances 64 / width per cell and v, 64 /
// height. The water shifts u and adds to v the wave of corner (x, y) for the two on the left and that of (x + 1, y)
// for the two on the right.
std::array<glm::vec2, 4> TileUvs(const TerrainView& view, std::uint8_t tile, std::uint8_t x, std::uint8_t y,
                                 const std::array<std::size_t, 4>& corners, const TerrainWater* water)
{
    const TileSize& size = view.tiles[tile];
    const float du = TileSpan / size.width;
    const float dv = TileSpan / size.height;
    float u = du * static_cast<float>(x);
    const float v = dv * static_cast<float>(y);
    if (water == nullptr)
    {
        const float u1 = u + du;
        const float v1 = v + dv;
        return {glm::vec2(u, v), glm::vec2(u1, v), glm::vec2(u1, v1), glm::vec2(u, v1)};
    }
    u = view.map == ReversedWaterMap && tile == WaterTile ? u - water->offset : water->offset + u;
    // 0x5fa7ab..0x5fa7c9: corner 1's wave goes through a float before being added; corner 0's is added to v from the
    // x87, without rounding the product.
    const bool waves = water->waves.size() == TerrainCellCount;
    const float left =
        waves ? static_cast<float>((static_cast<double>(water->waves[corners[0]]) * static_cast<double>(WaveToV)) +
                                   static_cast<double>(v))
              : v;
    const float right =
        waves ? static_cast<float>(static_cast<double>(water->waves[corners[1]] * WaveToV) + static_cast<double>(v))
              : v;
    const float u1 = u + du;
    return {glm::vec2(u, left), glm::vec2(u1, right), glm::vec2(u1, right + dv), glm::vec2(u, left + dv)};
}

// 0x5fa830..0x5fa98e: on map 7, layer 2 at 5 with some corner in alpha goes with the water of 0x5fa4b0 (tile 0x1e +
// DAT_07d2d360, slot 0x41 + frame) instead of layer 2's alpha pass. 0x5fa5c0 with the 5th argument at 1 takes 16
// (0x6b8f00) instead of 64 and, with the 4th at 0, neither shifts nor waves.
constexpr std::uint8_t AnimatedWaterMap = 7;
constexpr std::uint8_t FirstAnimatedWaterTile = 0x1e;
constexpr float AnimatedWaterSpan = 16.0f;
static_assert(std::bit_cast<std::uint32_t>(AnimatedWaterSpan) == 0x41800000, "la constante de 0x6b8f00");

// 0x5fa4b0: 0x620a90 (additive), the tile's slot + 0x23 and a GL_TRIANGLE_FAN with glColor3f of each corner's alpha
// (0x7ccccc4), without the light. 0x7d2d35e, which would skip it, is only read: it stays at the .bss's 0.
void DrawAnimatedWater(Render::ModelBatch& batch, const TerrainView& view, std::uint8_t x, std::uint8_t y,
                       const std::array<glm::vec3, 4>& positions, const std::array<std::size_t, 4>& corners)
{
    const auto tile = static_cast<std::uint8_t>(FirstAnimatedWaterTile + view.waterFrame);
    const TileSize& size = view.tiles[tile];
    const float du = AnimatedWaterSpan / size.width;
    const float dv = AnimatedWaterSpan / size.height;
    const float u = du * static_cast<float>(x);
    const float v = dv * static_cast<float>(y);
    const std::array<glm::vec2, 4> uvs{glm::vec2(u, v), glm::vec2(u + du, v), glm::vec2(u + du, v + dv),
                                       glm::vec2(u, v + dv)};
    batch.SetState(
        Render::ModelDrawState{.texture = Render::TextureSlot{static_cast<std::uint16_t>(FirstTileSlot + tile)},
                               .blend = Render::ModelBlend::Additive});
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t k = 0; k < 4; ++k)
    {
        const float grey = view.mapping->alpha[corners[k]];
        vertices[k] =
            Render::ModelVertex{.position = positions[k], .uv = uvs[k], .color = Render::Color{grey, grey, grey, 1.0f}};
    }
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
}

// One pass of the cell: the four-vertex fan as two triangles, with the tile's texture, each corner's light and, in the
// alpha pass, its alpha (0x5fa260 and 0x5fa350).
void DrawPass(Render::ModelBatch& batch, const TerrainView& view, std::uint8_t tile, Render::ModelBlend blend,
              const std::array<glm::vec3, 4>& positions, const std::array<std::size_t, 4>& corners,
              const std::array<glm::vec2, 4>& uvs)
{
    batch.SetState(Render::ModelDrawState{
        .texture = Render::TextureSlot{static_cast<std::uint16_t>(FirstTileSlot + tile)}, .blend = blend});
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t k = 0; k < 4; ++k)
    {
        const auto& light = view.lighting->cells[corners[k]];
        const float alpha = blend == Render::ModelBlend::Alpha ? view.mapping->alpha[corners[k]] : 1.0f;
        vertices[k] = Render::ModelVertex{
            .position = positions[k], .uv = uvs[k], .color = Render::Color{light[0], light[1], light[2], alpha}};
    }
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
}

// 0x5fa9f6..0x5faa73: the grass goes on the cells with all four corners at alpha 0 or less (fcomp with 0 and test ah,
// 0x41) and with 0x7b4ccb4 at 0, which nobody writes (Q-C226); 0x5faa79, outside maps 11 to 17.
bool CellTakesGrass(const TerrainView& view, const std::array<std::size_t, 4>& corners)
{
    for (const std::size_t corner : corners)
    {
        if (view.mapping->alpha[corner] > 0.0f)
        {
            return false;
        }
    }
    return view.map < FirstWithoutGrassCellsMap || view.map > LastWithoutGrassCellsMap;
}

// 0x5faa8c..0x5faaab: slot 0x32 + layer 1 and its texture, if present (+0x34 other than 0). Nobody uploads 0x35 to
// 0x40 and 0x6070a0 releases them on each load (Q-C230); from 0x41 on MuRet does not know.
Core::Expected<std::optional<float>, std::string> GrassTextureHeight(const TerrainGrass& grass, std::uint16_t slot)
{
    if (slot >= FirstUnknownGrassSlot)
    {
        return Core::Unexpected(
            std::format("0x5faaa3: el pasto lee el slot {:#x} (0x32 + la capa 1), que MuRet no sube", slot));
    }
    const std::size_t grassTexture = slot - FirstGrassSlot;
    if (grassTexture >= grass.heights.size())
    {
        return std::optional<float>{};
    }
    return grass.heights[grassTexture];
}

// 0x5faab1..0x5fac7b: the quad of cell (x, y) with the texture of `slot` and its buffer's height `height`, in the
// order of the four glVertex3fv: top over (x, y), top over (x + 1, y + 1), bottom at (x + 1, y + 1) and bottom at
// (x, y), each with the light of corner 0, 1, 2 and 3 (Q-C227).
void DrawGrassQuad(Render::ModelBatch& batch, const TerrainView& view, const TerrainWater& water,
                   const TerrainGrass& grass, std::uint16_t slot, float height, std::uint8_t x, std::uint8_t y,
                   const std::array<std::size_t, 4>& corners)
{
    // 0x5faab7..0x5faac7: twice the height, stored in a float.
    const float stem = StoredSum(height, height);
    // 0x5faaca..0x5faada: x · 0.25 from the x87 plus the row's offset, stored in a float.
    const auto u = static_cast<float>((static_cast<double>(x) * static_cast<double>(GrassStep)) +
                                      static_cast<double>(grass.offsets[y]));
    const float u1 = StoredSum(u, GrassStep);
    // 0x5fac90 leaves the corners at 0x79ecc30..0x79ecc5c; 0x5fab27..0x5fabbc raises the top ones.
    const float left = static_cast<float>(x) * TerrainCellSize;
    const float top = static_cast<float>(y) * TerrainCellSize;
    const float right = left + TerrainCellSize;
    const float bottom = top + TerrainCellSize;
    const float below = CornerHeight(view, corners[0]);
    const float across = CornerHeight(view, corners[2]);
    const std::array<glm::vec3, 4> positions{
        glm::vec3(left - GrassLean, StoredSum(top, WaveAt(water, corners[0])), StoredSum(below, stem)),
        glm::vec3(right - GrassLean, StoredSum(bottom, WaveAt(water, corners[1])), StoredSum(across, stem)),
        glm::vec3(right, bottom, across), glm::vec3(left, top, below)};
    const std::array<glm::vec2, 4> uvs{glm::vec2(u, GrassTop), glm::vec2(u1, GrassTop), glm::vec2(u1, GrassBottom),
                                       glm::vec2(u, GrassBottom)};
    batch.SetState(Render::ModelDrawState{.texture = Render::TextureSlot{slot}, .blend = Render::ModelBlend::Alpha});
    std::array<Render::ModelVertex, 4> vertices{};
    for (std::size_t k = 0; k < vertices.size(); ++k)
    {
        const auto& light = view.lighting->cells[corners[k]];
        vertices[k] = Render::ModelVertex{
            .position = positions[k], .uv = uvs[k], .color = Render::Color{light[0], light[1], light[2], 1.0f}};
    }
    batch.Triangle({vertices[0], vertices[1], vertices[2]});
    batch.Triangle({vertices[0], vertices[2], vertices[3]});
}
} // namespace

float WaterOffsetAt(Core::Duration clock, std::uint8_t map)
{
    const std::int64_t ms = Milliseconds(clock);
    if (map == SlowWaterMap)
    {
        return static_cast<float>(ms % SlowWaterPeriodMs) * SlowWaterStep;
    }
    return static_cast<float>(ms % WaterPeriodMs) * WaterStep;
}

void UpdateWaves(TerrainWater& water, Core::Duration clock, const TerrainRange& range, std::uint8_t map)
{
    if (water.waves.size() != TerrainCellCount)
    {
        water.waves.assign(TerrainCellCount, 0.0f);
    }
    const double phase = static_cast<double>(Milliseconds(clock) % WavePeriodMs) * static_cast<double>(WaveToV);
    const double column = map == SlowWaterMap ? SlowWaveColumn : WaveColumn;
    // The window of 0x5fbf60: three cells more than the range in x and in y.
    constexpr int Window = 3;
    for (int y = range.firstY; y <= range.lastY + Window; ++y)
    {
        for (int x = range.firstX; x <= range.lastX + Window; ++x)
        {
            const auto cell = static_cast<std::size_t>((y * static_cast<int>(TerrainSide)) + x);
            if (cell < water.waves.size())
            {
                water.waves[cell] =
                    static_cast<float>(Math::RoundedSin((static_cast<double>(x) * column) + phase) * WaveHeight);
            }
        }
    }
}

void DrawTerrainCell(Render::ModelBatch& batch, const TerrainView& view, const TerrainWater& water, std::uint8_t x,
                     std::uint8_t y)
{
    const std::array<std::size_t, 4> corners = CornersOf(x, y);
    if ((view.attributes->cells[corners[0]] & HiddenCellBit) != 0)
    {
        return;
    }
    // 0x5fac90: x · 100 and y · 100, the right and bottom ones 100 more, and each corner's height (the fixed one with
    // bit 0x40).
    const float left = static_cast<float>(x) * TerrainCellSize;
    const float top = static_cast<float>(y) * TerrainCellSize;
    const float right = left + TerrainCellSize;
    const float bottom = top + TerrainCellSize;
    std::array<float, 4> heights{};
    for (std::size_t k = 0; k < 4; ++k)
    {
        heights[k] = CornerHeight(view, corners[k]);
    }
    const std::array<glm::vec3, 4> positions{glm::vec3(left, top, heights[0]), glm::vec3(right, top, heights[1]),
                                             glm::vec3(right, bottom, heights[2]), glm::vec3(left, bottom, heights[3])};

    // 0x5fa830: with some corner below 1, layer 1 below and layer 2 above; otherwise layer 2 alone.
    bool someBelowOne = false;
    bool someAboveZero = false;
    for (const std::size_t corner : corners)
    {
        someBelowOne = someBelowOne || view.mapping->alpha[corner] < 1.0f;
        someAboveZero = someAboveZero || view.mapping->alpha[corner] > 0.0f;
    }
    const std::uint8_t base = someBelowOne ? view.mapping->layer1[corners[0]] : view.mapping->layer2[corners[0]];
    // local_8 of 0x5fa830: the lower one is water when it is layer 1 at 5; the alpha one, if layer 2 is 5 too.
    const bool baseIsWater = someBelowOne && base == WaterTile;
    DrawPass(batch, view, base, Render::ModelBlend::Opaque, positions, corners,
             TileUvs(view, base, x, y, corners, baseIsWater ? &water : nullptr));
    // 0x5fa973..0x5fa9b1: map 7 checks layer 2 (0x7aacc8c) of the first corner, with the base already drawn.
    if (someAboveZero && view.map == AnimatedWaterMap && view.mapping->layer2[corners[0]] == WaterTile)
    {
        DrawAnimatedWater(batch, view, x, y, positions, corners);
        return;
    }
    if (someBelowOne && someAboveZero)
    {
        const std::uint8_t upper = view.mapping->layer2[corners[0]];
        const bool upperIsWater = baseIsWater && upper == WaterTile;
        DrawPass(batch, view, upper, Render::ModelBlend::Alpha, positions, corners,
                 TileUvs(view, upper, x, y, corners, upperIsWater ? &water : nullptr));
    }
}

Core::Expected<void, std::string> DrawGrassCell(Render::ModelBatch& batch, const TerrainView& view,
                                                const TerrainWater& water, const TerrainGrass& grass, std::uint8_t x,
                                                std::uint8_t y)
{
    if (grass.offsets.size() < GrassOffsetRows)
    {
        return Core::Unexpected(std::string("0x5faad3: el corrimiento del pasto (0x7b0cca4, de 0x5f8300) no está"));
    }
    const std::array<std::size_t, 4> corners = CornersOf(x, y);
    // 0x5facb6..0x5facce, in 0x5fac90 before 0x5fa830 (which calls 0x405e00, without rand() outside maps 18 to 23,
    // which do not have this pass).
    if ((view.attributes->cells[corners[0]] & HiddenCellBit) != 0 || !CellTakesGrass(view, corners))
    {
        return {};
    }
    const auto slot = static_cast<std::uint16_t>(FirstGrassSlot + view.mapping->layer1[corners[0]]);
    const auto height = GrassTextureHeight(grass, slot);
    if (!height.HasValue())
    {
        return Core::Unexpected(height.Error());
    }
    if (height.Value().has_value())
    {
        DrawGrassQuad(batch, view, water, grass, slot, *height.Value(), x, y, corners);
    }
    return {};
}

} // namespace Client::Terrain
