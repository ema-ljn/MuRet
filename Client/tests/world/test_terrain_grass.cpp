#include "World/TerrainCells.h"
#include "World/TerrainCulling.h"

#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"
#include "World/TerrainLighting.h"

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

// Client::World::DrawGrassCell y DrawGrass con un terreno armado a mano: el pase del pasto de 0x5fac90
// con 0x5fa830 y 0x7d0d31c en 2 (0x5fa9f6..0x5fac7b), recorrido como el terreno por 0x5fc190
// (06 § El pasto).
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
using Client::World::DrawGrass;
using Client::World::DrawGrassCell;
using Client::World::TerrainGrass;
using Client::World::TerrainLighting;
using Client::World::TerrainPolygon;
using Client::World::TerrainRange;
using Client::World::TerrainView;
using Client::World::TerrainWater;

constexpr std::uint8_t CellX = 7;
constexpr std::uint8_t CellY = 9;
// El corrimiento de la fila de la celda: la u arranca en 7 · 0,25 + 0,125.
constexpr float RowOffset = 0.125f;

// Un terreno con la capa 1 en 0 (el slot 0x32), el alfa en 0, alturas y luces distintas en cada
// celda, ningún atributo y las tres texturas del pasto con buffers de 64, 32 y 16 de alto.
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

    // Las cuatro esquinas de la celda, en el orden de 0x5fac90: (x, y), (x + 1, y), (x + 1, y + 1) y
    // (x, y + 1).
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

// Los seis vértices del quad, de sus cuatro: (0, 1, 2) y (0, 2, 3).
constexpr std::array<std::size_t, 6> QuadCorners{0, 1, 2, 0, 2, 3};

// La esquina `corner` del quad en el lote: las tres primeras abren el primer triángulo y la 3 cierra
// el segundo.
const Client::Render::ModelVertex& QuadVertex(const ModelBatch& batch, std::size_t corner)
{
    constexpr std::array<std::size_t, 4> Places{0, 1, 2, 5};
    const ModelDrawCommand& command = batch.Commands().front();
    return batch.Vertices()[command.firstVertex + Places[corner]];
}

// 0x5fab27..0x5fac6d: arriba sobre (x, y) y sobre (x + 1, y + 1), 50 menos en x y el doble del alto del
// buffer más en z; abajo en (x + 1, y + 1) y en (x, y). La u, x · 0,25 más el corrimiento de la fila, y
// la v de 0 arriba y 1 abajo. La luz de cada esquina en su orden, con alfa 1, y la mezcla de 0x620a00(1).
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
        // La luz va por esquina en orden, no por la posición: la del vértice 1 es la de (x + 1, y).
        const auto& light = terrain.lighting->cells[cells[corner]];
        EXPECT_EQ(vertex.color, (Client::Render::Color{light[0], light[1], light[2], 1.0f})) << "vértice " << v;
    }
}

// 0x5faa8c: la capa 1 en k es el slot 0x32 + k, y el alto es el del buffer de esa textura.
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

// 0x5fa9f6..0x5faa73: una esquina con alfa por encima de 0 deja la celda sin pasto.
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

// 0x5facb6..0x5facce: el bit 0x08 de la esquina (x, y) esconde también el pasto.
TEST(TerrainGrassTest, TheBit0x08HidesTheGrass)
{
    Terrain terrain;
    terrain.attributes->cells[CellIndex(CellX, CellY)] = 0x08;
    const Drawn drawn = Draw(terrain);
    EXPECT_FALSE(drawn.error.has_value());
    EXPECT_TRUE(drawn.batch.Commands().empty());
}

// 0x5faa79: los mapas 11 a 17 no dibujan el pasto.
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

// 0x5faaa3: la textura que no se subió (+0x34 en 0) deja la celda sin pasto, y no avisa.
TEST(TerrainGrassTest, AGrassThatWasNotUploadedIsSkipped)
{
    Terrain terrain;
    terrain.grass.heights[0].reset();
    const Drawn drawn = Draw(terrain);
    EXPECT_FALSE(drawn.error.has_value());
    EXPECT_TRUE(drawn.batch.Commands().empty());
}

// Del 0x35 al 0x40 (la capa 1 de 3 a 14) no los sube nadie y 0x6070a0 los suelta en cada carga.
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

// Del 0x41 en adelante (la capa 1 de 15 o más) están las texturas que sube después 0x61a560 y las de
// otras cargas, que MuRet no tiene: corta, no saltea.
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

// 0x5faad3: el pase lee el corrimiento de la fila; sin las 256 filas de 0x5f8300 corta.
TEST(TerrainGrassTest, WithoutTheRowOffsetsTheGrassStops)
{
    Terrain terrain;
    terrain.grass.offsets = {};
    const Drawn drawn = Draw(terrain);
    ASSERT_TRUE(drawn.error.has_value());
    EXPECT_TRUE(drawn.error->starts_with("0x5faad3")) << *drawn.error;
    EXPECT_TRUE(drawn.batch.Commands().empty());
}

// 0x5fac90: el bit 0x40 fija la altura de la esquina, con y sin el alto del pasto encima.
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

// 0x5fab27..0x5fabbc (Q-C227): las de arriba suman en y la ola de la esquina 0 y la de la 1, aunque la
// segunda está parada sobre la esquina 2; las de abajo no llevan ola.
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

// Las celdas de los pases del pasto, por su esquina de abajo (x, y).
std::vector<glm::ivec2> CellsOf(const ModelBatch& batch)
{
    std::vector<glm::ivec2> cells;
    for (const ModelDrawCommand& command : batch.Commands())
    {
        for (std::size_t first = 0; first < command.vertexCount; first += 6)
        {
            // El vértice 5 es la esquina 3, abajo en (x, y).
            const glm::vec3 origin = batch.Vertices()[command.firstVertex + first + 5].position;
            cells.emplace_back(static_cast<int>(origin.x / 100.0f), static_cast<int>(origin.y / 100.0f));
        }
    }
    return cells;
}

// 0x5fc190 no mira 0x7d0d31c: el pasto va por los mismos bloques y celdas que el terreno.
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

// El pase corta en la primera celda que corta, con lo dibujado antes en el lote.
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
