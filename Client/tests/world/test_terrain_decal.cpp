#include "World/TerrainDecal.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Render/ModelBatch.h"
#include "Render/ModelVertex.h"
#include "Render/Pass3D.h"
#include "Render/SpriteVertex.h"
#include "Render/TextureSlot.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

// Client::World::DrawTerrainDecal con un terreno armado a mano: el calco de 0x5fb2f0 sobre las celdas de
// 0x5faec0 (06 § Las marcas del suelo).
namespace
{
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::TerrainHeights;
using Client::Render::ModelBatch;
using Client::Render::ModelBlend;
using Client::Render::ModelDrawCommand;
using Client::Render::ModelDrawState;
using Client::Render::ModelVertex;
using Client::Render::TextureSlot;
using Client::World::DrawTerrainDecal;
using Client::World::TerrainDecal;

constexpr std::int32_t BloodSlot = 0x5e3;
// 0x549175: lo que sube el calco de una marca.
constexpr float Lift = 5.0f;
// Los seis vértices de una celda, de sus cuatro esquinas: el fan (0, 1, 2) y (0, 2, 3).
constexpr std::array<std::size_t, 6> FanCorners{0, 1, 2, 0, 2, 3};

// Alturas distintas en celdas vecinas, todas exactas en float.
std::unique_ptr<TerrainHeights> Heights()
{
    auto heights = std::make_unique<TerrainHeights>();
    for (std::size_t cell = 0; cell < heights->cells.size(); ++cell)
    {
        heights->cells[cell] = static_cast<float>(cell % 7) * 1.5f;
    }
    return heights;
}

// En el mundo (1000, 2000): el centro cae justo en la esquina de la celda (10, 20).
TerrainDecal Decal()
{
    return TerrainDecal{.slot = BloodSlot,
                        .x = 1000.0f,
                        .y = 2000.0f,
                        .width = 1.0f,
                        .height = 1.0f,
                        .color = glm::vec3(0.25f, 0.5f, 0.75f),
                        .angle = 0.0f,
                        .alpha = 0.5f,
                        .lift = Lift};
}

struct Drawn
{
    ModelBatch batch;
    std::optional<std::string> error;
};

// Con la mezcla normal y sin depth test, como lo deja 0x549130.
Drawn Draw(const TerrainHeights& heights, const TerrainDecal& decal)
{
    Drawn drawn;
    drawn.batch.Begin(Client::Render::Pass3D{});
    drawn.batch.SetState(ModelDrawState{.texture = std::nullopt, .blend = ModelBlend::Alpha, .depthTest = false});
    if (auto decaled = DrawTerrainDecal(drawn.batch, heights, decal); !decaled.HasValue())
    {
        drawn.error = decaled.Error();
    }
    return drawn;
}

// La esquina `corner` (0 a 3) de la celda `cell` del lote, del orden de dibujo.
const ModelVertex& Corner(const ModelBatch& batch, std::size_t cell, std::size_t corner)
{
    // La 0, la 1 y la 2 abren el primer triángulo; la 3 cierra el segundo.
    const std::size_t offset = corner == 3 ? 5 : corner;
    return batch.Vertices()[(cell * FanCorners.size()) + offset];
}

// La esquina `corner` de la celda (x, y) en el mundo, con su altura más lo que sube el calco.
glm::vec3 CornerPosition(const TerrainHeights& heights, std::uint8_t x, std::uint8_t y, std::size_t corner)
{
    // (x, y), (x + 1, y), (x + 1, y + 1) y (x, y + 1).
    constexpr std::array<int, 4> right{0, 1, 1, 0};
    constexpr std::array<int, 4> down{0, 0, 1, 1};
    const auto cornerX = static_cast<std::uint8_t>(x + right[corner]);
    const auto cornerY = static_cast<std::uint8_t>(y + down[corner]);
    return glm::vec3(static_cast<float>(cornerX) * 100.0f, static_cast<float>(cornerY) * 100.0f,
                     Lift + heights.cells[CellIndex(cornerX, cornerY)]);
}
} // namespace

// Sin giro y con lado 1: n = 2, las 25 celdas de (8, 18) a (12, 22), fila por fila. El centro cae en la esquina de
// la (10, 20), así que la u de una celda va de i + 0,5 a i + 1,5 y la v de j + 0,5 a j + 1,5: el calco entero es el
// cuadrado de una celda alrededor del centro y las demás quedan afuera de [0, 1]. Cada vértice, con la altura de su
// esquina más 5 y el color con el alfa.
TEST(TerrainDecalTest, AnUnturnedDecalCoversTheCellsAroundItsCenter)
{
    const auto heights = Heights();
    const Drawn drawn = Draw(*heights, Decal());

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Commands().size(), 1u);
    const ModelDrawCommand& command = drawn.batch.Commands()[0];
    EXPECT_EQ(command.state.texture, TextureSlot{static_cast<std::uint16_t>(BloodSlot)});
    EXPECT_EQ(command.state.blend, ModelBlend::Alpha);
    EXPECT_FALSE(command.state.depthTest);
    ASSERT_EQ(drawn.batch.Vertices().size(), 25u * FanCorners.size());
    std::size_t cell = 0;
    for (int j = -2; j <= 2; ++j)
    {
        for (int i = -2; i <= 2; ++i, ++cell)
        {
            SCOPED_TRACE(testing::Message() << i << ", " << j);
            const auto x = static_cast<std::uint8_t>(10 + i);
            const auto y = static_cast<std::uint8_t>(20 + j);
            const float left = static_cast<float>(i) + 0.5f;
            const float top = static_cast<float>(j) + 0.5f;
            const std::array<glm::vec2, 4> uvs{glm::vec2(left, top), glm::vec2(left + 1.0f, top),
                                               glm::vec2(left + 1.0f, top + 1.0f), glm::vec2(left, top + 1.0f)};
            for (std::size_t corner = 0; corner < 4; ++corner)
            {
                const ModelVertex& vertex = Corner(drawn.batch, cell, corner);
                EXPECT_EQ(vertex.position, CornerPosition(*heights, x, y, corner)) << corner;
                EXPECT_EQ(vertex.uv, uvs[corner]) << corner;
                EXPECT_EQ(vertex.color, (Client::Render::Color{0.25f, 0.5f, 0.75f, 0.5f})) << corner;
            }
        }
    }
}

// 0x5fb2f0 no cambia la mezcla ni la profundidad: liga la textura sobre lo que dejó quien lo llama.
TEST(TerrainDecalTest, KeepsTheBlendItWasGiven)
{
    const auto heights = Heights();
    ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    batch.SetState(ModelDrawState{.texture = TextureSlot{0x32}, .blend = ModelBlend::Additive, .depthTest = false});

    ASSERT_TRUE(DrawTerrainDecal(batch, *heights, Decal()).HasValue());

    ASSERT_EQ(batch.Commands().size(), 1u);
    EXPECT_EQ(batch.Commands()[0].state, (ModelDrawState{.texture = TextureSlot{static_cast<std::uint16_t>(BloodSlot)},
                                                         .blend = ModelBlend::Additive,
                                                         .depthTest = false}));
}

// 0x5faedb..0x5faefa: las celdas con x o y fuera de 0..254 no se dibujan. Con el centro en (0,5; 254,5) quedan las
// columnas 0 a 2 y las filas 252 a 254: la 255 tampoco, aunque esté en el mapa.
TEST(TerrainDecalTest, TheCellsOutsideZeroTo254AreNotDrawn)
{
    const auto heights = Heights();
    TerrainDecal decal = Decal();
    decal.x = 50.0f;
    decal.y = 25450.0f;

    const Drawn drawn = Draw(*heights, decal);

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Vertices().size(), 9u * FanCorners.size());
    std::size_t cell = 0;
    for (std::uint8_t y = 252; y <= 254; ++y)
    {
        for (std::uint8_t x = 0; x <= 2; ++x, ++cell)
        {
            EXPECT_EQ(Corner(drawn.batch, cell, 0).position, CornerPosition(*heights, x, y, 0));
            EXPECT_EQ(Corner(drawn.batch, cell, 2).position, CornerPosition(*heights, x, y, 2));
        }
    }
}

// El lado mayor fija las celdas, n = ftol(2) + 1 = 3, y 1 / 2 el paso de las UV; el cociente, 2, estira la u
// alrededor de 0,5 después del giro: u = 2 · ((i + 1) · 0,5 − 0,5) + 0,5 = i + 0,5 a la izquierda e i + 1,5 a la
// derecha, y la v queda (j + 1) · 0,5 arriba y (j + 2) · 0,5 abajo.
TEST(TerrainDecalTest, AWiderDecalCoversMoreCellsAndStretchesTheU)
{
    const auto heights = Heights();
    TerrainDecal decal = Decal();
    decal.width = 2.0f;

    const Drawn drawn = Draw(*heights, decal);

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Vertices().size(), 49u * FanCorners.size());
    std::size_t cell = 0;
    for (int j = -3; j <= 3; ++j)
    {
        for (int i = -3; i <= 3; ++i, ++cell)
        {
            const ModelVertex& first = Corner(drawn.batch, cell, 0);
            EXPECT_EQ(first.uv, glm::vec2(static_cast<float>(i) + 0.5f, static_cast<float>(j + 1) * 0.5f))
                << i << ", " << j;
            EXPECT_EQ(Corner(drawn.batch, cell, 2).uv,
                      glm::vec2(static_cast<float>(i) + 1.5f, static_cast<float>(j + 2) * 0.5f));
        }
    }
}

// 0x5fb388..0x5fb39d: con el ancho en NaN, el lado es el alto (fcomp y C0): 25 celdas, como con lado 1.
TEST(TerrainDecalTest, WithANotANumberWidthTheHeightDecides)
{
    const auto heights = Heights();
    TerrainDecal decal = Decal();
    decal.width = std::numeric_limits<float>::quiet_NaN();

    const Drawn drawn = Draw(*heights, decal);

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    EXPECT_EQ(drawn.batch.Vertices().size(), 25u * FanCorners.size());
}

// El giro de 0x5fc8f0 con (0, 0, 90): cada UV gira alrededor de (0,5; 0,5), u' = 1 − v y v' = u, con el seno y
// el coseno de la x87 (a menos de 1e-6).
TEST(TerrainDecalTest, ATurnedDecalTurnsItsUvsAroundTheMiddle)
{
    const auto heights = Heights();
    TerrainDecal decal = Decal();
    decal.angle = 90.0f;

    const Drawn drawn = Draw(*heights, decal);

    ASSERT_FALSE(drawn.error.has_value()) << *drawn.error;
    ASSERT_EQ(drawn.batch.Vertices().size(), 25u * FanCorners.size());
    // La celda del centro, la 12: u de 0,5 a 1,5 y v de 0,5 a 1,5 sin girar.
    const std::array<glm::vec2, 4> unturned{glm::vec2(0.5f, 0.5f), glm::vec2(1.5f, 0.5f), glm::vec2(1.5f, 1.5f),
                                            glm::vec2(0.5f, 1.5f)};
    for (std::size_t corner = 0; corner < 4; ++corner)
    {
        const glm::vec2 uv = Corner(drawn.batch, 12, corner).uv;
        EXPECT_NEAR(uv.x, 1.0f - unturned[corner].y, 1e-6f) << corner;
        EXPECT_NEAR(uv.y, unturned[corner].x, 1e-6f) << corner;
    }
}

// 0x620780 con un slot fuera de los 1.845 de la tabla, y un centro que el ftol de 0x5fb378 no lleva a un entero de
// 64 bits: error, sin un vértice.
TEST(TerrainDecalTest, ASlotOutsideTheTableOrAnUnreachableCenterStops)
{
    struct Case
    {
        TerrainDecal decal;
        std::string_view address;
    };
    const auto heights = Heights();
    Case outside{Decal(), "0x5fb355"};
    outside.decal.slot = static_cast<std::int32_t>(Client::Render::OriginalTextureSlotCount);
    Case negative{Decal(), "0x5fb355"};
    negative.decal.slot = -1;
    Case far{Decal(), "0x5fb378"};
    far.decal.x = 1e30f;

    for (const Case& probe : {outside, negative, far})
    {
        const Drawn drawn = Draw(*heights, probe.decal);
        ASSERT_TRUE(drawn.error.has_value());
        EXPECT_NE(drawn.error->find(probe.address), std::string::npos) << *drawn.error;
        EXPECT_TRUE(drawn.batch.Vertices().empty());
    }
}
