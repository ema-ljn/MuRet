#include "World/TerrainCells.h"

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

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

// Client::World::DrawTerrainCell con un terreno armado a mano: los vértices de 0x5fac90, las capas
// que elige 0x5fa830 por el alfa de las cuatro esquinas, las UV de 0x5fa5c0 y la luz y el alfa de
// cada esquina que mandan 0x5fa260 y 0x5fa350 (06 § Terreno).
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
using Client::World::DrawTerrainCell;
using Client::World::TerrainLighting;
using Client::World::TerrainRange;
using Client::World::TerrainView;
using Client::World::TerrainWater;
using Client::World::UpdateWaves;
using Client::World::WaterOffsetAt;
using namespace std::chrono_literals;

constexpr std::uint8_t CellX = 7;
constexpr std::uint8_t CellY = 9;

// Un terreno de tiles de 256 × 128, con la capa 1 en el tile 2 y la 2 en el 3, el alfa en 0, alturas
// y luces distintas en cada celda y ningún atributo.
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

    // Las cuatro esquinas de la celda, en el orden de 0x5fac90: (x, y), (x + 1, y), (x + 1, y + 1) y
    // (x, y + 1).
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

// Los seis vértices de un pase, de su fan de cuatro: (0, 1, 2) y (0, 2, 3).
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
    // u = 64 / 256 por celda y v = 64 / 128 por celda (0x5fa5c0).
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

// Con las cuatro esquinas en alfa 255, una sola vez, opaca y con la capa 2 (la rama de 0x5fa830
// sin ninguna esquina por debajo de 1).
TEST(TerrainCellsTest, AnOpaqueCellIsDrawnOnceWithTheSecondLayer)
{
    Terrain terrain;
    terrain.SetAlpha(CellX, CellY, {1.0f, 1.0f, 1.0f, 1.0f});
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 1u);
    ExpectThePass(batch, batch.Commands()[0], terrain, 3, ModelBlend::Opaque, false);
}

// Sin alfa en ninguna esquina, una vez con la capa 1, y ningún pase de alfa.
TEST(TerrainCellsTest, ACellWithoutAlphaIsDrawnOnceWithTheFirstLayer)
{
    Terrain terrain;
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 1u);
    ExpectThePass(batch, batch.Commands()[0], terrain, 2, ModelBlend::Opaque, false);
}

// Con alguna esquina en alfa y alguna por debajo de 1, la capa 1 opaca y enseguida la 2 con la
// mezcla normal y el alfa de cada esquina.
TEST(TerrainCellsTest, ACellWithAlphaIsDrawnTwice)
{
    Terrain terrain;
    terrain.SetAlpha(CellX, CellY, {0.0f, 0.5f, 1.0f, 0.25f});
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 2u);
    ExpectThePass(batch, batch.Commands()[0], terrain, 2, ModelBlend::Opaque, false);
    ExpectThePass(batch, batch.Commands()[1], terrain, 3, ModelBlend::Alpha, true);
}

// Q-C19: una capa 2 de 255 no se trata como vacía: es el slot 0x122.
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

// El bit 0x08 de la celda (no la de las otras esquinas) la deja sin dibujar.
TEST(TerrainCellsTest, TheBit0x08HidesTheCell)
{
    Terrain terrain;
    terrain.attributes->cells[CellIndex(CellX, CellY)] = 0x08;
    EXPECT_TRUE(Draw(terrain).Commands().empty());
    terrain.attributes->cells[CellIndex(CellX, CellY)] = 0;
    terrain.attributes->cells[CellIndex(CellX + 1, CellY)] = 0x08;
    EXPECT_EQ(Draw(terrain).Commands().size(), 1u);
}

// Una esquina con el bit 0x40 va a la altura fija de DAT_006c9d5c, 1.200; las otras, a la suya.
TEST(TerrainCellsTest, TheBit0x40FixesTheHeightOfACorner)
{
    Terrain terrain;
    terrain.attributes->cells[CellIndex(CellX + 1, CellY + 1)] = 0x40;
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Vertices()[2].position.z, 1200.0f);
    EXPECT_EQ(batch.Vertices()[0].position.z, terrain.heights->cells[CellIndex(CellX, CellY)]);
}

// Las esquinas van por índice, sin dar la vuelta en la fila: la última celda de una fila toma como
// esquinas de la derecha las primeras de la fila siguiente (0x5fac90).
TEST(TerrainCellsTest, TheLastCellOfARowTakesTheNextRow)
{
    Terrain terrain;
    const ModelBatch batch = Draw(terrain, 255, CellY);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Vertices()[1].position,
              glm::vec3(25600.0f, 900.0f, terrain.heights->cells[CellIndex(0, CellY + 1)]));
}

// Los tiles con su textura: la u avanza 64 / ancho por celda y la v, 64 / alto.
TEST(TerrainCellsTest, TheUvFollowTheSizeOfTheTile)
{
    Terrain terrain;
    terrain.view.tiles[2] = {64.0f, 32.0f};
    const ModelBatch batch = Draw(terrain);
    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Vertices()[0].uv, glm::vec2(7.0f, 18.0f));
    EXPECT_EQ(batch.Vertices()[2].uv, glm::vec2(8.0f, 20.0f));
}
// 0x5fc260: el corrimiento de la u del agua es (ftol(T) % 20000) · 5e-5 (0x6b8a28), con T el reloj en
// ms; en el mapa 8, (ftol(T) % 40000) · 2,5e-5 (0x6ba710).
TEST(TerrainCellsTest, TheWaterMovesWithTheClock)
{
    constexpr float Step = std::bit_cast<float>(0x3851B717u);
    EXPECT_EQ(WaterOffsetAt(0ms, 55), 0.0f);
    EXPECT_EQ(WaterOffsetAt(10'000ms, 55), 10000.0f * Step);
    EXPECT_EQ(WaterOffsetAt(20'000ms, 55), 0.0f);
    EXPECT_EQ(WaterOffsetAt(20'001ms, 55), Step);
    EXPECT_EQ(WaterOffsetAt(10'000ms, 8), 10000.0f * std::bit_cast<float>(0x37D1B717u));
}

// 0x5fbf60: la ola de cada celda de la ventana (el rango y 3 más) es sin(x · 5 + (ftol(T) % 720000) ·
// 0,002) · 10, con la x de la celda y la cuenta en la x87.
TEST(TerrainCellsTest, TheWavesFollowTheColumn)
{
    TerrainWater water;
    UpdateWaves(water, 1'234'567ms, TerrainRange{.firstX = 4, .lastX = 8, .firstY = 12, .lastY = 16}, 55);
    ASSERT_EQ(water.waves.size(), 65536u);
    const double phase = static_cast<double>(1234567 % 720000) * static_cast<double>(std::bit_cast<float>(0x3B03126Fu));
    for (const std::uint8_t x : {std::uint8_t{4}, std::uint8_t{9}, std::uint8_t{11}})
    {
        const auto expected = static_cast<float>(std::sin((static_cast<double>(x) * 5.0) + phase) * 10.0);
        EXPECT_EQ(water.waves[CellIndex(x, 13)], expected) << static_cast<int>(x);
        EXPECT_EQ(water.waves[CellIndex(x, 19)], expected) << static_cast<int>(x);
    }
    // Fuera de la ventana no se escribe.
    EXPECT_EQ(water.waves[CellIndex(3, 13)], 0.0f);
    EXPECT_EQ(water.waves[CellIndex(12, 13)], 0.0f);
    EXPECT_EQ(water.waves[CellIndex(5, 20)], 0.0f);
}

// 0x5fa830 y 0x5fa5c0: con alguna esquina por debajo de 1 y la capa 1 en el agua (5), la de abajo
// corre la u y lleva la ola en la v: la de la esquina (x, y) en las dos de la izquierda y la de
// (x + 1, y) en las dos de la derecha, por 0,002.
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

// Solo es agua lo que 0x5fa830 marca: la capa 1 en 5 con alguna esquina por debajo de 1. Con las cuatro
// en 1 la de abajo es la capa 2, quieta aunque sea agua; el pase de alfa va con agua solo si las dos
// capas son 5.
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
} // namespace
