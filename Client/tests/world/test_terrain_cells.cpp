#include "Terrain/TerrainCells.h"
#include "Math/RoundedMath.h"
#include "Terrain/TerrainCulling.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "Terrain/TerrainLighting.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>
#include <set>
#include <vector>

// Client::Terrain::DrawTerrainCell with a hand-built terrain: the vertices of 0x5fac90, the layers 0x5fa830 picks by
// the alpha of the four corners, the UVs of 0x5fa5c0 and the light and alpha of each corner that 0x5fa260 and
// 0x5fa350 send (06 § Terreno).
namespace
{
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::TerrainAttributes;
using Client::Data::Terrain::TerrainHeights;
using Client::Data::Terrain::TerrainMapping;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawCommand;
using Client::Render::TextureSlot;
using Client::Terrain::DrawTerrain;
using Client::Terrain::DrawTerrainCell;
using Client::Terrain::TerrainLighting;
using Client::Terrain::TerrainRange;
using Client::Terrain::TerrainView;
using Client::Terrain::TerrainWater;
using Client::Terrain::UpdateWaves;
using Client::Terrain::WaterOffsetAt;
using namespace std::chrono_literals;
using Client::Math::RoundedSin;

constexpr std::uint8_t CellX = 7;
constexpr std::uint8_t CellY = 9;

// A terrain of 256 × 128 tiles, with layer 1 on tile 2 and layer 2 on tile 3, alpha at 0, different heights and
// lights on each cell and no attribute.
struct Terrain
{
    std::unique_ptr<TerrainMapping> mapping = std::make_unique<TerrainMapping>();
    std::unique_ptr<TerrainHeights> heights = std::make_unique<TerrainHeights>();
    std::unique_ptr<TerrainAttributes> attributes = std::make_unique<TerrainAttributes>();
    std::unique_ptr<TerrainLighting> lighting = std::make_unique<TerrainLighting>();
    TerrainView view;

    Terrain()
    {
        mapping->layer1.fill(2);
        mapping->layer2.fill(3);
        mapping->alpha.fill(0.0f);
        attributes->cells.fill(0);
        for (std::size_t cell = 0; cell < heights->cells.size(); ++cell)
        {
            heights->cells[cell] = static_cast<float>(cell % 251) * 3.0f;
            const float shade = static_cast<float>(cell % 13) / 13.0f;
            lighting->cells[cell] = {shade, 1.0f - shade, 0.5f};
        }
        view.mapping = mapping.get();
        view.heights = heights.get();
        view.attributes = attributes.get();
        view.lighting = lighting.get();
        view.tiles.fill({256.0f, 128.0f});
    }

    // The four corners of the cell, in the order of 0x5fac90: (x, y), (x + 1, y), (x + 1, y + 1) and (x, y + 1).
    static std::array<std::size_t, 4> Corners(std::uint8_t x, std::uint8_t y)
    {
        const std::size_t first = CellIndex(x, y);
        return {first, first + 1, first + 256 + 1, first + 256};
    }

    void SetAlpha(std::uint8_t x, std::uint8_t y, const std::array<float, 4>& corners)
    {
        const auto cells = Corners(x, y);
        for (std::size_t k = 0; k < 4; ++k)
        {
            mapping->alpha[cells[k]] = corners[k];
        }
    }
};

ModelBatch Draw(const Terrain& terrain, std::uint8_t x = CellX, std::uint8_t y = CellY,
                const TerrainWater& water = TerrainWater{})
{
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawTerrainCell(batch, terrain.view, water, x, y);
    return batch;
}

// The six vertices of a pass, from its fan of four: (0, 1, 2) and (0, 2, 3).
constexpr std::array<std::size_t, 6> FanCorners{0, 1, 2, 0, 2, 3};

void ExpectThePass(const ModelBatch& batch, const ModelDrawCommand& command, const Terrain& terrain, std::uint8_t tile,
                   ModelBlend blend, bool withAlpha)
{
    ASSERT_EQ(command.vertexCount, 6u);
    ASSERT_TRUE(command.state.texture.has_value());
    EXPECT_EQ(*command.state.texture, (TextureSlot{static_cast<std::uint16_t>(tile + 0x23)}));
    EXPECT_EQ(command.state.blend, blend);
    EXPECT_TRUE(command.state.depthTest);
    const auto cells = Terrain::Corners(CellX, CellY);
    const std::array<glm::vec2, 4> places{glm::vec2(700.0f, 900.0f), glm::vec2(800.0f, 900.0f),
                                          glm::vec2(800.0f, 1000.0f), glm::vec2(700.0f, 1000.0f)};
    // u = 64 / 256 per cell and v = 64 / 128 per cell (0x5fa5c0).
    const std::array<glm::vec2, 4> uvs{glm::vec2(1.75f, 4.5f), glm::vec2(2.0f, 4.5f), glm::vec2(2.0f, 5.0f),
                                       glm::vec2(1.75f, 5.0f)};
    for (std::size_t v = 0; v < 6; ++v)
    {
        const std::size_t corner = FanCorners[v];
        const Client::Render::ModelVertex& vertex = batch.Vertices()[command.firstVertex + v];
        const std::size_t cell = cells[corner];
        EXPECT_EQ(vertex.position, glm::vec3(places[corner], terrain.heights->cells[cell])) << "vértice " << v;
        EXPECT_EQ(vertex.uv, uvs[corner]) << "vértice " << v;
        const auto& light = terrain.lighting->cells[cell];
        const float alpha = withAlpha ? terrain.mapping->alpha[cell] : 1.0f;
        EXPECT_EQ(vertex.color, (Client::Render::Color{light[0], light[1], light[2], alpha})) << "vértice " << v;
    }
}

// With all four corners at alpha 255, drawn once, opaque and with layer 2 (the branch of 0x5fa830 with no corner
// below 1).
TEST(TerrainCellsTest, AnOpaqueCellIsDrawnOnceWithTheSecondLayer)
{
    Terrain terrain;
    terrain.SetAlpha(CellX, CellY, {1.0f, 1.0f, 1.0f, 1.0f});
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 1u);
    ExpectThePass(batch, batch.Commands()[0], terrain, 3, ModelBlend::Opaque, false);
}

// With no alpha on any corner, drawn once with layer 1 and no alpha pass.
TEST(TerrainCellsTest, ACellWithoutAlphaIsDrawnOnceWithTheFirstLayer)
{
    Terrain terrain;
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 1u);
    ExpectThePass(batch, batch.Commands()[0], terrain, 2, ModelBlend::Opaque, false);
}

// With some corner in alpha and some below 1, layer 1 opaque and right after it layer 2 with the normal blend and
// the alpha of each corner.
TEST(TerrainCellsTest, ACellWithAlphaIsDrawnTwice)
{
    Terrain terrain;
    terrain.SetAlpha(CellX, CellY, {0.0f, 0.5f, 1.0f, 0.25f});
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 2u);
    ExpectThePass(batch, batch.Commands()[0], terrain, 2, ModelBlend::Opaque, false);
    ExpectThePass(batch, batch.Commands()[1], terrain, 3, ModelBlend::Alpha, true);
}

// 0x5fa973..0x5fa9b1 and 0x5fa4b0: on map 7, with some corner in alpha and the first one's layer 2 at 5, the water
// goes after the base: tile 0x1e + the frame (DAT_07d2d360) in its slot, additive, with UVs of 16 / width and
// 16 / height per cell, without offset or waves, and the gray of each corner's alpha with alpha 1, without the
// light; the alpha pass of layer 2 does not go. With all four corners at 1 the base is layer 2 and the water goes
// the same; without alpha, only the base; on another map or with another layer 2, the usual alpha pass.
TEST(TerrainCellsTest, AtlansDrawsItsCausticWaterInsteadOfTheAlphaLayer)
{
    constexpr std::uint32_t Frame = 5;
    constexpr std::uint8_t Caustic = 0x1e + Frame;
    const auto expectTheWater = [](const ModelBatch& batch, const ModelDrawCommand& command, const Terrain& terrain)
    {
        ASSERT_EQ(command.vertexCount, 6u);
        ASSERT_TRUE(command.state.texture.has_value());
        EXPECT_EQ(*command.state.texture, (TextureSlot{static_cast<std::uint16_t>(Caustic + 0x23)}));
        EXPECT_EQ(command.state.blend, ModelBlend::Additive);
        const auto cells = Terrain::Corners(CellX, CellY);
        // 16 / 128 = 0.125 per cell in u and 16 / 64 = 0.25 in v (0x6b8f00).
        const std::array<glm::vec2, 4> uvs{glm::vec2(0.875f, 2.25f), glm::vec2(1.0f, 2.25f), glm::vec2(1.0f, 2.5f),
                                           glm::vec2(0.875f, 2.5f)};
        const std::array<glm::vec2, 4> places{glm::vec2(700.0f, 900.0f), glm::vec2(800.0f, 900.0f),
                                              glm::vec2(800.0f, 1000.0f), glm::vec2(700.0f, 1000.0f)};
        for (std::size_t v = 0; v < 6; ++v)
        {
            const std::size_t corner = FanCorners[v];
            const Client::Render::ModelVertex& vertex = batch.Vertices()[command.firstVertex + v];
            const std::size_t cell = cells[corner];
            const float grey = terrain.mapping->alpha[cell];
            EXPECT_EQ(vertex.position, glm::vec3(places[corner], terrain.heights->cells[cell])) << "vértice " << v;
            EXPECT_EQ(vertex.uv, uvs[corner]) << "vértice " << v;
            EXPECT_EQ(vertex.color, (Client::Render::Color{grey, grey, grey, 1.0f})) << "vértice " << v;
        }
    };
    Terrain terrain;
    terrain.view.map = 7;
    terrain.view.waterFrame = Frame;
    terrain.view.tiles[Caustic] = {128.0f, 64.0f};
    terrain.mapping->layer2.fill(5);

    terrain.SetAlpha(CellX, CellY, {0.0f, 0.5f, 1.0f, 0.25f});
    ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 2u);
    ExpectThePass(batch, batch.Commands()[0], terrain, 2, ModelBlend::Opaque, false);
    expectTheWater(batch, batch.Commands()[1], terrain);

    terrain.SetAlpha(CellX, CellY, {1.0f, 1.0f, 1.0f, 1.0f});
    batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 2u);
    ExpectThePass(batch, batch.Commands()[0], terrain, 5, ModelBlend::Opaque, false);
    expectTheWater(batch, batch.Commands()[1], terrain);

    terrain.SetAlpha(CellX, CellY, {0.0f, 0.0f, 0.0f, 0.0f});
    EXPECT_EQ(Draw(terrain).Commands().size(), 1u);

    terrain.SetAlpha(CellX, CellY, {0.0f, 0.5f, 1.0f, 0.25f});
    terrain.view.map = 0;
    batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 2u);
    EXPECT_EQ(batch.Commands()[1].state.blend, ModelBlend::Alpha);
    EXPECT_EQ(*batch.Commands()[1].state.texture, (TextureSlot{5 + 0x23}));

    terrain.view.map = 7;
    terrain.mapping->layer2.fill(3);
    batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 2u);
    ExpectThePass(batch, batch.Commands()[1], terrain, 3, ModelBlend::Alpha, true);
}

// Q-C19: a layer 2 of 255 is not treated as empty: it is the slot 0x122.
TEST(TerrainCellsTest, ALayerOf255IsTheSlot0x122)
{
    Terrain terrain;
    terrain.mapping->layer2[CellIndex(CellX, CellY)] = 255;
    terrain.SetAlpha(CellX, CellY, {0.0f, 0.2f, 0.0f, 0.0f});
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 2u);
    ASSERT_TRUE(batch.Commands()[1].state.texture.has_value());
    EXPECT_EQ(*batch.Commands()[1].state.texture, TextureSlot{0x122});
}

// The bit 0x08 of the cell (not of the other corners) leaves it undrawn.
TEST(TerrainCellsTest, TheBit0x08HidesTheCell)
{
    Terrain terrain;
    terrain.attributes->cells[CellIndex(CellX, CellY)] = 0x08;
    EXPECT_TRUE(Draw(terrain).Commands().empty());
    terrain.attributes->cells[CellIndex(CellX, CellY)] = 0;
    terrain.attributes->cells[CellIndex(CellX + 1, CellY)] = 0x08;
    EXPECT_EQ(Draw(terrain).Commands().size(), 1u);
}

// A corner with the bit 0x40 goes to the fixed height of DAT_006c9d5c, 1,200; the others, to their own.
TEST(TerrainCellsTest, TheBit0x40FixesTheHeightOfACorner)
{
    Terrain terrain;
    terrain.attributes->cells[CellIndex(CellX + 1, CellY + 1)] = 0x40;
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Vertices()[2].position.z, 1200.0f);
    EXPECT_EQ(batch.Vertices()[0].position.z, terrain.heights->cells[CellIndex(CellX, CellY)]);
}

// The corners go by index, without wrapping on the row: the last cell of a row takes the first ones of the next
// row as its right corners (0x5fac90).
TEST(TerrainCellsTest, TheLastCellOfARowTakesTheNextRow)
{
    Terrain terrain;
    const ModelBatch batch = Draw(terrain, 255, CellY);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Vertices()[1].position,
              glm::vec3(25600.0f, 900.0f, terrain.heights->cells[CellIndex(0, CellY + 1)]));
}

// The tiles with their texture: u advances 64 / width per cell and v, 64 / height.
TEST(TerrainCellsTest, TheUvFollowTheSizeOfTheTile)
{
    Terrain terrain;
    terrain.view.tiles[2] = {64.0f, 32.0f};
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Vertices()[0].uv, glm::vec2(7.0f, 18.0f));
    EXPECT_EQ(batch.Vertices()[2].uv, glm::vec2(8.0f, 20.0f));
}
// 0x5fc260: the water's u offset is (ftol(T) % 20000) · 5e-5 (0x6b8a28), with T the clock in ms; on map 8,
// (ftol(T) % 40000) · 2.5e-5 (0x6ba710).
TEST(TerrainCellsTest, TheWaterMovesWithTheClock)
{
    constexpr float Step = std::bit_cast<float>(0x3851B717u);
    EXPECT_EQ(WaterOffsetAt(0ms, 55), 0.0f);
    EXPECT_EQ(WaterOffsetAt(10'000ms, 55), 10000.0f * Step);
    EXPECT_EQ(WaterOffsetAt(20'000ms, 55), 0.0f);
    EXPECT_EQ(WaterOffsetAt(20'001ms, 55), Step);
    EXPECT_EQ(WaterOffsetAt(10'000ms, 8), 10000.0f * std::bit_cast<float>(0x37D1B717u));
}

// 0x5fbf60: the wave of each cell of the window (the range and 3 more) is sin(x · 5 + (ftol(T) % 720000) · 0.002) ·
// 10, with the cell's x and the arithmetic on the x87.
TEST(TerrainCellsTest, TheWavesFollowTheColumn)
{
    TerrainWater water;
    UpdateWaves(water, 1'234'567ms, TerrainRange{.firstX = 4, .lastX = 8, .firstY = 12, .lastY = 16}, 55);
    ASSERT_EQ(water.waves.size(), 65536u);
    const double phase = static_cast<double>(1234567 % 720000) * static_cast<double>(std::bit_cast<float>(0x3B03126Fu));
    for (const std::uint8_t x : {std::uint8_t{4}, std::uint8_t{9}, std::uint8_t{11}})
    {
        const auto expected = static_cast<float>(RoundedSin((static_cast<double>(x) * 5.0) + phase) * 10.0);
        EXPECT_EQ(water.waves[CellIndex(x, 13)], expected) << static_cast<int>(x);
        EXPECT_EQ(water.waves[CellIndex(x, 19)], expected) << static_cast<int>(x);
    }
    // Nothing is written outside the window.
    EXPECT_EQ(water.waves[CellIndex(3, 13)], 0.0f);
    EXPECT_EQ(water.waves[CellIndex(12, 13)], 0.0f);
    EXPECT_EQ(water.waves[CellIndex(5, 20)], 0.0f);
}

// 0x5fa830 and 0x5fa5c0: with some corner below 1 and layer 1 on the water (5), the bottom one shifts the u and
// carries the wave in the v: that of corner (x, y) on the two left ones and that of (x + 1, y) on the two right
// ones, times 0.002.
TEST(TerrainCellsTest, TheWaterCellCarriesTheOffsetAndTheWaves)
{
    Terrain terrain;
    terrain.mapping->layer1[CellIndex(CellX, CellY)] = 5;
    terrain.SetAlpha(CellX, CellY, {0.0f, 0.0f, 0.0f, 0.0f});
    TerrainWater water;
    water.offset = 0.375f;
    water.waves.assign(65536, 0.0f);
    water.waves[CellIndex(CellX, CellY)] = 4.0f;
    water.waves[CellIndex(CellX + 1, CellY)] = -2.0f;
    const ModelBatch batch = Draw(terrain, CellX, CellY, water);
    ASSERT_EQ(batch.Commands().size(), 1u);
    constexpr float Wave = std::bit_cast<float>(0x3B03126Fu);
    const float v = 0.5f * static_cast<float>(CellY);
    const float left = v + (4.0f * Wave);
    const float right = v + (-2.0f * Wave);
    const auto uv = [&batch](std::size_t k) { return batch.Vertices()[k].uv; };
    EXPECT_EQ(uv(0), glm::vec2(1.75f + 0.375f, left));
    EXPECT_EQ(uv(1), glm::vec2(1.75f + 0.375f + 0.25f, right));
    EXPECT_EQ(uv(2), glm::vec2(1.75f + 0.375f + 0.25f, right + 0.5f));
    EXPECT_EQ(uv(5), glm::vec2(1.75f + 0.375f, left + 0.5f));
}

// 0x5fa7ab..0x5fa7c9: the wave of corner 1 goes through a float before it is added to the v, and that of corner 0 is
// added from the x87 without rounding the product. With the wave −9.7147… (0xc11b6f81) on a v of 4.5, the two
// results differ in the last bit: corner 0 gives 0x408f60d5 and corner 1, 0x408f60d6.
TEST(TerrainCellsTest, TheWaveOfTheLeftCornerIsNotRoundedFirst)
{
    Terrain terrain;
    terrain.mapping->layer1[CellIndex(CellX, CellY)] = 5;
    terrain.SetAlpha(CellX, CellY, {0.0f, 0.0f, 0.0f, 0.0f});
    TerrainWater water;
    water.waves.assign(65536, 0.0f);
    water.waves[CellIndex(CellX, CellY)] = std::bit_cast<float>(0xC11B6F81u);
    water.waves[CellIndex(CellX + 1, CellY)] = std::bit_cast<float>(0xC11B6F81u);
    const ModelBatch batch = Draw(terrain, CellX, CellY, water);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(batch.Vertices()[0].uv.y), 0x408F60D5u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(batch.Vertices()[1].uv.y), 0x408F60D6u);
}

// Only what 0x5fa830 marks is water: layer 1 at 5 with some corner below 1. With all four at 1 the bottom one is
// layer 2, still even if it is water; the alpha pass goes with water only if both layers are 5.
TEST(TerrainCellsTest, OnlyLayer5BelowIsWater)
{
    TerrainWater water;
    water.offset = 0.375f;
    water.waves.assign(65536, 1.0f);
    {
        Terrain terrain;
        terrain.mapping->layer2[CellIndex(CellX, CellY)] = 5;
        terrain.SetAlpha(CellX, CellY, {1.0f, 1.0f, 1.0f, 1.0f});
        const ModelBatch batch = Draw(terrain, CellX, CellY, water);
        EXPECT_EQ(batch.Vertices()[0].uv, glm::vec2(1.75f, 4.5f));
    }
    {
        Terrain terrain;
        terrain.mapping->layer2[CellIndex(CellX, CellY)] = 5;
        terrain.SetAlpha(CellX, CellY, {0.0f, 0.5f, 0.0f, 0.0f});
        const ModelBatch batch = Draw(terrain, CellX, CellY, water);
        ASSERT_EQ(batch.Commands().size(), 2u);
        EXPECT_EQ(batch.Vertices()[batch.Commands()[1].firstVertex].uv, glm::vec2(1.75f, 4.5f));
    }
    {
        Terrain terrain;
        terrain.mapping->layer1[CellIndex(CellX, CellY)] = 5;
        terrain.mapping->layer2[CellIndex(CellX, CellY)] = 5;
        terrain.SetAlpha(CellX, CellY, {0.0f, 0.5f, 0.0f, 0.0f});
        const ModelBatch batch = Draw(terrain, CellX, CellY, water);
        ASSERT_EQ(batch.Commands().size(), 2u);
        EXPECT_EQ(batch.Vertices()[batch.Commands()[1].firstVertex].uv.x, 1.75f + 0.375f);
    }
}
// 0x5fc190 and 0x5fc0e0: the 4 × 4 blocks by block row and then by column, and in each one the 16 cells by row and
// then by column, each one with its alpha pass right after.
TEST(TerrainCellsTest, TheCellsGoByBlockAndThenByRow)
{
    Terrain terrain;
    terrain.SetAlpha(9, 13, {0.0f, 0.5f, 0.0f, 0.0f});
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawTerrain(batch, terrain.view, TerrainRange{.firstX = 4, .lastX = 8, .firstY = 12, .lastY = 16},
                Client::Terrain::TerrainPolygon{.everything = true}, TerrainWater{});
    // Four blocks (12 and 16 in y, 4 and 8 in x), of 16 cells. The alpha of corner (10, 13) is shared by the four
    // cells around it, which carry an alpha pass. Each pass is six vertices, and the batch joins consecutive ones with
    // the same state into one span.
    std::vector<std::pair<int, int>> cells;
    std::size_t alphaPasses = 0;
    for (const ModelDrawCommand& command : batch.Commands())
    {
        for (std::size_t first = 0; first < command.vertexCount; first += 6)
        {
            const glm::vec3 origin = batch.Vertices()[command.firstVertex + first].position;
            const std::pair<int, int> cell{static_cast<int>(origin.x / 100.0f), static_cast<int>(origin.y / 100.0f)};
            if (command.state.blend == ModelBlend::Alpha)
            {
                ++alphaPasses;
                EXPECT_TRUE(cell.first == 9 || cell.first == 10) << cell.first;
                EXPECT_TRUE(cell.second == 12 || cell.second == 13) << cell.second;
                EXPECT_EQ(cells.back(), cell) << "el pase de alfa va enseguida del opaco de su celda";
                continue;
            }
            cells.push_back(cell);
        }
    }
    EXPECT_EQ(alphaPasses, 4u);
    ASSERT_EQ(cells.size(), 64u);
    EXPECT_EQ(cells[0], std::make_pair(4, 12));
    EXPECT_EQ(cells[1], std::make_pair(5, 12));
    EXPECT_EQ(cells[4], std::make_pair(4, 13));
    EXPECT_EQ(cells[15], std::make_pair(7, 15));
    EXPECT_EQ(cells[16], std::make_pair(8, 12));
    EXPECT_EQ(cells[32], std::make_pair(4, 16));
    EXPECT_EQ(cells[63], std::make_pair(11, 19));
}
// The game's trapezoid (0x5fb570 at 45° around the hero of the Lorencia trace) cuts by block with the margin of −40
// at its center and then by cell with 0 (0x5fc190 and 0x5fc0e0): it leaves out most of the range, and some cells
// are drawn even though the center of their block does not pass with margin 0.
TEST(TerrainCellsTest, TheGamePolygonCutsByBlockWithItsMarginAndThenByCell)
{
    Terrain terrain;
    const auto polygon =
        Client::Terrain::TerrainPolygonOf(Client::Terrain::CullingScene::Game, 0, {14350.0f, 12050.0f, 165.0f}, -45.0f);
    const TerrainRange range{.firstX = 120, .lastX = 164, .firstY = 100, .lastY = 140};
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    DrawTerrain(batch, terrain.view, range, polygon, TerrainWater{});
    std::set<std::pair<int, int>> drawn;
    for (const ModelDrawCommand& command : batch.Commands())
    {
        for (std::size_t first = 0; first < command.vertexCount; first += 6)
        {
            const glm::vec3 origin = batch.Vertices()[command.firstVertex + first].position;
            drawn.emplace(static_cast<int>(origin.x / 100.0f), static_cast<int>(origin.y / 100.0f));
        }
    }
    std::set<std::pair<int, int>> expected;
    bool savedByTheMargin = false;
    for (int by = range.firstY; by <= range.lastY; by += 4)
    {
        for (int bx = range.firstX; bx <= range.lastX; bx += 4)
        {
            const bool block = Client::Terrain::PolygonContains(polygon, bx + 2.0, by + 2.0, -40.0);
            const bool blockWithoutMargin = Client::Terrain::PolygonContains(polygon, bx + 2.0, by + 2.0, 0.0);
            for (int y = by; y < by + 4; ++y)
            {
                for (int x = bx; x < bx + 4; ++x)
                {
                    const bool cell = Client::Terrain::PolygonContains(polygon, x + 0.5, y + 0.5, 0.0);
                    if (block && cell)
                    {
                        expected.emplace(x, y);
                        savedByTheMargin = savedByTheMargin || !blockWithoutMargin;
                    }
                }
            }
        }
    }
    EXPECT_FALSE(drawn.empty());
    EXPECT_EQ(drawn, expected);
    EXPECT_TRUE(savedByTheMargin) << "una celda que se dibuja por el margen de su bloque";
    EXPECT_LT(drawn.size(), 45u * 41u / 2u) << "el trapecio deja afuera la mayor parte del rango";
}
} // namespace
