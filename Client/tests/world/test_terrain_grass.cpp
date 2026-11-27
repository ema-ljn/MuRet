#include "Terrain/TerrainCells.h"
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
#include <glm/ext/vector_int2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Client::Terrain::DrawGrassCell and DrawGrass with a hand-built terrain: the grass pass of 0x5fac90 with 0x5fa830
// and 0x7d0d31c at 2 (0x5fa9f6..0x5fac7b), walked like the terrain by 0x5fc190 (06 § El pasto).
namespace
{
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::TerrainAttributes;
using Client::Data::Terrain::TerrainCellCount;
using Client::Data::Terrain::TerrainHeights;
using Client::Data::Terrain::TerrainMapping;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawCommand;
using Client::Render::TextureSlot;
using Client::Terrain::DrawGrass;
using Client::Terrain::DrawGrassCell;
using Client::Terrain::TerrainGrass;
using Client::Terrain::TerrainLighting;
using Client::Terrain::TerrainPolygon;
using Client::Terrain::TerrainRange;
using Client::Terrain::TerrainView;
using Client::Terrain::TerrainWater;

constexpr std::uint8_t CellX = 7;
constexpr std::uint8_t CellY = 9;
// The offset of the cell's row: the u starts at 7 · 0.25 + 0.125.
constexpr float RowOffset = 0.125f;

// A terrain with layer 1 at 0 (the slot 0x32), alpha at 0, different heights and lights on each cell, no attribute
// and the three grass textures with buffers 64, 32 and 16 high.
struct Terrain
{
    std::unique_ptr<TerrainMapping> mapping = std::make_unique<TerrainMapping>();
    std::unique_ptr<TerrainHeights> heights = std::make_unique<TerrainHeights>();
    std::unique_ptr<TerrainAttributes> attributes = std::make_unique<TerrainAttributes>();
    std::unique_ptr<TerrainLighting> lighting = std::make_unique<TerrainLighting>();
    std::vector<float> offsets = std::vector<float>(TerrainCellCount, 0.0f);
    TerrainView view;
    TerrainGrass grass;

    Terrain()
    {
        mapping->layer1.fill(0);
        mapping->layer2.fill(3);
        mapping->alpha.fill(0.0f);
        attributes->cells.fill(0);
        for (std::size_t cell = 0; cell < heights->cells.size(); ++cell)
        {
            heights->cells[cell] = static_cast<float>(cell % 251) * 3.0f;
            const float shade = static_cast<float>(cell % 13) / 13.0f;
            lighting->cells[cell] = {shade, 1.0f - shade, 0.5f};
        }
        offsets[CellY] = RowOffset;
        view.mapping = mapping.get();
        view.heights = heights.get();
        view.attributes = attributes.get();
        view.lighting = lighting.get();
        view.tiles.fill({256.0f, 128.0f});
        grass.heights = {64.0f, 32.0f, 16.0f};
        grass.offsets = offsets;
    }

    // The four corners of the cell, in the order of 0x5fac90: (x, y), (x + 1, y), (x + 1, y + 1) and (x, y + 1).
    static std::array<std::size_t, 4> Corners(std::uint8_t x, std::uint8_t y)
    {
        const std::size_t first = CellIndex(x, y);
        return {first, first + 1, first + 256 + 1, first + 256};
    }
};

struct Drawn
{
    ModelBatch batch;
    std::optional<std::string> error;
};

Drawn Draw(const Terrain& terrain, const TerrainWater& water = TerrainWater{})
{
    Drawn drawn;
    drawn.batch.Begin(Client::Render::Pass3D{});
    if (auto grown = DrawGrassCell(drawn.batch, terrain.view, water, terrain.grass, CellX, CellY); !grown.HasValue())
    {
        drawn.error = grown.Error();
    }
    return drawn;
}

// The six vertices of the quad, from its four: (0, 1, 2) and (0, 2, 3).
constexpr std::array<std::size_t, 6> QuadCorners{0, 1, 2, 0, 2, 3};

// Corner `corner` of the quad in the batch: the first three open the first triangle and 3 closes the second.
const Client::Render::ModelVertex& QuadVertex(const ModelBatch& batch, std::size_t corner)
{
    constexpr std::array<std::size_t, 4> Places{0, 1, 2, 5};
    const ModelDrawCommand& command = batch.Commands().front();
    return batch.Vertices()[command.firstVertex + Places[corner]];
}

// 0x5fab27..0x5fac6d: on top over (x, y) and over (x + 1, y + 1), 50 less in x and twice the buffer's height more
// in z; at the bottom on (x + 1, y + 1) and on (x, y). The u is x · 0.25 plus the row's offset, and the v is 0 on
// top and 1 at the bottom. The light of each corner in its order, with alpha 1, and the blend of 0x620a00(1).
TEST(TerrainGrassTest, TheGrassIsAQuadStandingOnTheDiagonalOfTheCell)
{
    const Terrain terrain;
    const Drawn drawn = Draw(terrain);
    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Commands().size(), 1u);
    const ModelDrawCommand& command = drawn.batch.Commands().front();
    ASSERT_EQ(command.vertexCount, 6u);
    ASSERT_TRUE(command.state.texture.has_value());
    EXPECT_EQ(*command.state.texture, (TextureSlot{0x32}));
    EXPECT_EQ(command.state.blend, ModelBlend::Alpha);
    EXPECT_TRUE(command.state.depthTest);
    const auto cells = Terrain::Corners(CellX, CellY);
    const float near = terrain.heights->cells[cells[0]];
    const float far = terrain.heights->cells[cells[2]];
    const std::array<glm::vec3, 4> positions{glm::vec3(650.0f, 900.0f, near + 128.0f),
                                             glm::vec3(750.0f, 1000.0f, far + 128.0f), glm::vec3(800.0f, 1000.0f, far),
                                             glm::vec3(700.0f, 900.0f, near)};
    const std::array<glm::vec2, 4> uvs{glm::vec2(1.875f, 0.0f), glm::vec2(2.125f, 0.0f), glm::vec2(2.125f, 1.0f),
                                       glm::vec2(1.875f, 1.0f)};
    for (std::size_t v = 0; v < 6; ++v)
    {
        const std::size_t corner = QuadCorners[v];
        const Client::Render::ModelVertex& vertex = drawn.batch.Vertices()[command.firstVertex + v];
        EXPECT_EQ(vertex.position, positions[corner]) << "vértice " << v;
        EXPECT_EQ(vertex.uv, uvs[corner]) << "vértice " << v;
        // The light goes by corner in order, not by position: that of vertex 1 is that of (x + 1, y).
        const auto& light = terrain.lighting->cells[cells[corner]];
        EXPECT_EQ(vertex.color, (Client::Render::Color{light[0], light[1], light[2], 1.0f})) << "vértice " << v;
    }
}

// 0x5faa8c: layer 1 at k is the slot 0x32 + k, and the height is that of the buffer of that texture.
TEST(TerrainGrassTest, TheFirstLayerPicksTheGrassAndItsHeight)
{
    Terrain terrain;
    terrain.mapping->layer1[CellIndex(CellX, CellY)] = 2;
    const Drawn drawn = Draw(terrain);
    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Commands().size(), 1u);
    EXPECT_EQ(*drawn.batch.Commands().front().state.texture, (TextureSlot{0x34}));
    const auto cells = Terrain::Corners(CellX, CellY);
    EXPECT_EQ(QuadVertex(drawn.batch, 0).position.z, terrain.heights->cells[cells[0]] + 32.0f);
}

// 0x5fa9f6..0x5faa73: a corner with alpha above 0 leaves the cell without grass.
TEST(TerrainGrassTest, ACornerWithAlphaLeavesTheCellWithoutGrass)
{
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
        Terrain terrain;
        terrain.mapping->alpha[Terrain::Corners(CellX, CellY)[corner]] = 1.0f / 255.0f;
        const Drawn drawn = Draw(terrain);
        EXPECT_FALSE(drawn.error.has_value());
        EXPECT_TRUE(drawn.batch.Commands().empty()) << "esquina " << corner;
    }
}

// 0x5facb6..0x5facce: the bit 0x08 of corner (x, y) hides the grass too.
TEST(TerrainGrassTest, TheBit0x08HidesTheGrass)
{
    Terrain terrain;
    terrain.attributes->cells[CellIndex(CellX, CellY)] = 0x08;
    const Drawn drawn = Draw(terrain);
    EXPECT_FALSE(drawn.error.has_value());
    EXPECT_TRUE(drawn.batch.Commands().empty());
}

// 0x5faa79: maps 11 to 17 draw no grass.
TEST(TerrainGrassTest, TheMaps11To17HaveNoGrass)
{
    for (const int map : {10, 11, 14, 17, 18})
    {
        Terrain terrain;
        terrain.view.map = static_cast<std::uint8_t>(map);
        const Drawn drawn = Draw(terrain);
        EXPECT_FALSE(drawn.error.has_value());
        EXPECT_EQ(drawn.batch.Commands().empty(), map >= 11 && map <= 17) << "mapa " << map;
    }
}

// 0x5faaa3: a texture that was not uploaded (+0x34 at 0) leaves the cell without grass, without a warning.
TEST(TerrainGrassTest, AGrassThatWasNotUploadedIsSkipped)
{
    Terrain terrain;
    terrain.grass.heights[0].reset();
    const Drawn drawn = Draw(terrain);
    EXPECT_FALSE(drawn.error.has_value());
    EXPECT_TRUE(drawn.batch.Commands().empty());
}

// Nobody uploads 0x35 to 0x40 (layer 1 from 3 to 14) and 0x6070a0 frees them on every load.
TEST(TerrainGrassTest, TheSlotsAfterTheGrassAreEmpty)
{
    for (const std::uint8_t layer : {std::uint8_t{3}, std::uint8_t{9}, std::uint8_t{14}})
    {
        Terrain terrain;
        terrain.mapping->layer1[CellIndex(CellX, CellY)] = layer;
        const Drawn drawn = Draw(terrain);
        EXPECT_FALSE(drawn.error.has_value()) << static_cast<int>(layer);
        EXPECT_TRUE(drawn.batch.Commands().empty()) << static_cast<int>(layer);
    }
}

// From 0x41 on (layer 1 of 15 or more) are the textures 0x61a560 uploads later and those of other loads, which MuRet
// does not have: it stops, it does not skip.
TEST(TerrainGrassTest, ALayerThatReachesTheNextSlotsStops)
{
    for (const std::uint8_t layer : {std::uint8_t{15}, std::uint8_t{255}})
    {
        Terrain terrain;
        terrain.mapping->layer1[CellIndex(CellX, CellY)] = layer;
        const Drawn drawn = Draw(terrain);
        ASSERT_TRUE(drawn.error.has_value()) << static_cast<int>(layer);
        EXPECT_TRUE(drawn.error->starts_with("0x5faaa3")) << *drawn.error;
        EXPECT_TRUE(drawn.batch.Commands().empty());
    }
}

// 0x5faad3: the pass reads the row's offset; without the 256 rows of 0x5f8300 it stops.
TEST(TerrainGrassTest, WithoutTheRowOffsetsTheGrassStops)
{
    Terrain terrain;
    terrain.grass.offsets = {};
    const Drawn drawn = Draw(terrain);
    ASSERT_TRUE(drawn.error.has_value());
    EXPECT_TRUE(drawn.error->starts_with("0x5faad3")) << *drawn.error;
    EXPECT_TRUE(drawn.batch.Commands().empty());
}

// 0x5fac90: the bit 0x40 fixes the height of the corner, with and without the grass's height on top.
TEST(TerrainGrassTest, TheBit0x40FixesTheHeightOfTheGrassToo)
{
    Terrain terrain;
    terrain.view.fixedHeight = 1201.0f;
    terrain.attributes->cells[CellIndex(CellX, CellY)] = 0x40;
    const Drawn drawn = Draw(terrain);
    ASSERT_EQ(drawn.batch.Commands().size(), 1u);
    EXPECT_EQ(QuadVertex(drawn.batch, 0).position.z, 1201.0f + 128.0f);
    EXPECT_EQ(QuadVertex(drawn.batch, 3).position.z, 1201.0f);
}

// 0x5fab27..0x5fabbc (Q-C227): the top ones add in y the wave of corner 0 and that of corner 1, even though the
// second stands over corner 2; the bottom ones carry no wave.
TEST(TerrainGrassTest, TheTopCornersTakeTheWavesOfTheFirstTwoCorners)
{
    const Terrain terrain;
    TerrainWater water;
    water.waves.assign(TerrainCellCount, 0.0f);
    const auto cells = Terrain::Corners(CellX, CellY);
    water.waves[cells[0]] = 2.5f;
    water.waves[cells[1]] = -4.0f;
    water.waves[cells[2]] = 7.0f;
    water.waves[cells[3]] = 9.0f;
    const Drawn drawn = Draw(terrain, water);
    ASSERT_EQ(drawn.batch.Commands().size(), 1u);
    EXPECT_EQ(QuadVertex(drawn.batch, 0).position.y, 902.5f);
    EXPECT_EQ(QuadVertex(drawn.batch, 1).position.y, 996.0f);
    EXPECT_EQ(QuadVertex(drawn.batch, 2).position.y, 1000.0f);
    EXPECT_EQ(QuadVertex(drawn.batch, 3).position.y, 900.0f);
}

// The cells of the grass passes, by their bottom corner (x, y).
std::vector<glm::ivec2> CellsOf(const ModelBatch& batch)
{
    std::vector<glm::ivec2> cells;
    for (const ModelDrawCommand& command : batch.Commands())
    {
        for (std::size_t first = 0; first < command.vertexCount; first += 6)
        {
            // Vertex 5 is corner 3, at the bottom on (x, y).
            const glm::vec3 origin = batch.Vertices()[command.firstVertex + first + 5].position;
            cells.emplace_back(static_cast<int>(origin.x / 100.0f), static_cast<int>(origin.y / 100.0f));
        }
    }
    return cells;
}

// 0x5fc190 does not look at 0x7d0d31c: the grass goes by the same blocks and cells as the terrain.
TEST(TerrainGrassTest, TheGrassGoesByBlockAndThenByRow)
{
    Terrain terrain;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto grown = DrawGrass(batch, terrain.view, TerrainRange{.firstX = 4, .lastX = 8, .firstY = 12, .lastY = 16},
                                 TerrainPolygon{.everything = true}, TerrainWater{}, terrain.grass);
    ASSERT_TRUE(grown.HasValue()) << grown.Error();
    const std::vector<glm::ivec2> cells = CellsOf(batch);
    ASSERT_EQ(cells.size(), 64u);
    EXPECT_EQ(cells[0], glm::ivec2(4, 12));
    EXPECT_EQ(cells[1], glm::ivec2(5, 12));
    EXPECT_EQ(cells[4], glm::ivec2(4, 13));
    EXPECT_EQ(cells[15], glm::ivec2(7, 15));
    EXPECT_EQ(cells[16], glm::ivec2(8, 12));
    EXPECT_EQ(cells[32], glm::ivec2(4, 16));
    EXPECT_EQ(cells[63], glm::ivec2(11, 19));
}

// The pass stops at the first cell that stops, with what was drawn before it in the batch.
TEST(TerrainGrassTest, TheGrassStopsAtTheFirstCellThatStops)
{
    Terrain terrain;
    terrain.mapping->layer1[CellIndex(6, 12)] = 15;
    terrain.mapping->layer1[CellIndex(5, 12)] = 4;
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    const auto grown = DrawGrass(batch, terrain.view, TerrainRange{.firstX = 4, .lastX = 8, .firstY = 12, .lastY = 16},
                                 TerrainPolygon{.everything = true}, TerrainWater{}, terrain.grass);
    ASSERT_FALSE(grown.HasValue());
    EXPECT_TRUE(grown.Error().starts_with("0x5faaa3")) << grown.Error();
    const std::vector<glm::ivec2> cells = CellsOf(batch);
    ASSERT_EQ(cells.size(), 1u);
    EXPECT_EQ(cells[0], glm::ivec2(4, 12));
}
} // namespace
