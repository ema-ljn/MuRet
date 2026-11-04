#include "World/TerrainLighting.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainLight.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

// Client::World::ShadeTerrainLight y LightAt, con alturas y luces armadas a mano: la sombra de una
// celda sale de las alturas de sus vecinas (0x5f8b10 y 0x5f8d90), y la luz en un punto, de las cuatro
// celdas de alrededor (0x5f99e0).
namespace
{
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::TerrainHeights;
using Client::Data::Terrain::TerrainLight;
using Client::Data::Terrain::TerrainLightColor;
using Client::Data::Terrain::TerrainSide;
using Client::World::BrightShadingMap;
using Client::World::LightAt;
using Client::World::LoadTerrainLighting;
using Client::World::ShadeTerrainLight;
using Client::World::TerrainLighting;

constexpr std::uint8_t Lorencia = 0;
constexpr TerrainLightColor Base{0.8f, 0.6f, 0.4f};
constexpr float Rounding = 1e-5f;

std::unique_ptr<TerrainHeights> HeightsOf(const std::function<float(std::size_t, std::size_t)>& height)
{
    auto heights = std::make_unique<TerrainHeights>();
    for (std::size_t y = 0; y < TerrainSide; ++y)
    {
        for (std::size_t x = 0; x < TerrainSide; ++x)
        {
            heights->cells[(y * TerrainSide) + x] = height(x, y);
        }
    }
    return heights;
}

std::unique_ptr<TerrainLight> EvenLight()
{
    auto light = std::make_unique<TerrainLight>();
    light->cells.fill(Base);
    return light;
}

// La sombra que quedó en la celda (x, y): la luz de la celda sobre la de base, en el rojo.
float ShadeAt(const TerrainLighting& lighting, std::uint8_t x, std::uint8_t y)
{
    return lighting.cells[CellIndex(x, y)][0] / Base[0];
}

// Sin pendiente, la normal es (0, 0, 1) y la sombra 0,5 + 0,5: la luz queda como está.
TEST(TerrainLightingTest, AFlatTerrainKeepsItsLight)
{
    const auto lighting =
        ShadeTerrainLight(*EvenLight(), *HeightsOf([](std::size_t, std::size_t) { return 165.0f; }), Lorencia);
    for (const std::size_t cell : {CellIndex(0, 0), CellIndex(128, 77), CellIndex(254, 254)})
    {
        EXPECT_FLOAT_EQ(lighting->cells[cell][0], Base[0]);
        EXPECT_FLOAT_EQ(lighting->cells[cell][1], Base[1]);
        EXPECT_FLOAT_EQ(lighting->cells[cell][2], Base[2]);
    }
}

// Las alturas que suben 100 por celda en x: la normal es (−1, 0, 1) / √2, y la sombra 0,5. Cada
// canal va por ella.
TEST(TerrainLightingTest, ASlopeInXShadesHalf)
{
    const auto lighting = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t x, std::size_t) { return 100.0f * static_cast<float>(x); }), Lorencia);
    EXPECT_NEAR(ShadeAt(*lighting, 10, 20), 0.5f, Rounding);
    EXPECT_NEAR(lighting->cells[CellIndex(10, 20)][1], Base[1] * 0.5f, Rounding);
    EXPECT_NEAR(lighting->cells[CellIndex(10, 20)][2], Base[2] * 0.5f, Rounding);
}

// En y la cuenta da −0,5 · n.y: la pendiente que sube hacia +y da 1,207 y queda en 1, y la que baja,
// 0,5.
TEST(TerrainLightingTest, TheShadeGoesFromZeroToOne)
{
    const auto up = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t, std::size_t y) { return 100.0f * static_cast<float>(y); }), Lorencia);
    EXPECT_FLOAT_EQ(ShadeAt(*up, 30, 40), 1.0f);
    const auto down = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t, std::size_t y) { return -100.0f * static_cast<float>(y); }), Lorencia);
    EXPECT_NEAR(ShadeAt(*down, 30, 40), 0.5f, Rounding);
    // Con x − y por 1000, la normal va a (−1, 1, 0,1) y la cuenta da −0,17: queda en 0.
    const auto steep =
        ShadeTerrainLight(*EvenLight(),
                          *HeightsOf([](std::size_t x, std::size_t y)
                                     { return 1000.0f * (static_cast<float>(x) - static_cast<float>(y)); }),
                          Lorencia);
    EXPECT_FLOAT_EQ(ShadeAt(*steep, 30, 40), 0.0f);
}

// El mapa 30 sombrea con −1 y 1: la pendiente en x da 0,5 · (−0,707) + 0,707 + 0,5 = 0,854.
TEST(TerrainLightingTest, MapThirtyShadesBrighter)
{
    const auto heights = HeightsOf([](std::size_t x, std::size_t) { return 100.0f * static_cast<float>(x); });
    const auto lighting = ShadeTerrainLight(*EvenLight(), *heights, BrightShadingMap);
    const float expected = (-0.5f * std::sqrt(0.5f)) + std::sqrt(0.5f) + 0.5f;
    EXPECT_NEAR(ShadeAt(*lighting, 10, 20), expected, Rounding);
}

// La columna 255 busca la altura de la x + 1 en un byte, la 0, pero la posición sigue en 25.600: con
// las alturas que bajan 100 por celda en x, la de al lado sube 25.500 en 100 unidades, la normal se va
// casi a −x y la sombra a 0,002. Con la altura de la 255, la celda sería plana, y la sombra 1. Lo
// mismo en la fila 255 con las que suben en y.
TEST(TerrainLightingTest, TheLastColumnAndRowTakeTheHeightsOfTheFirst)
{
    const float wrapped = (-0.5f * 2550000.0f / std::sqrt((2550000.0f * 2550000.0f) + (10000.0f * 10000.0f))) +
                          (0.5f * 10000.0f / std::sqrt((2550000.0f * 2550000.0f) + (10000.0f * 10000.0f))) + 0.5f;
    const auto alongX = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t x, std::size_t) { return -100.0f * static_cast<float>(x); }), Lorencia);
    EXPECT_FLOAT_EQ(ShadeAt(*alongX, 254, 20), 1.0f);
    EXPECT_NEAR(ShadeAt(*alongX, 255, 20), wrapped, Rounding);
    const auto alongY = ShadeTerrainLight(
        *EvenLight(), *HeightsOf([](std::size_t, std::size_t y) { return 100.0f * static_cast<float>(y); }), Lorencia);
    EXPECT_FLOAT_EQ(ShadeAt(*alongY, 20, 254), 1.0f);
    EXPECT_NEAR(ShadeAt(*alongY, 20, 255), wrapped, Rounding);
}

std::unique_ptr<TerrainLighting> LightingOf(const std::function<float(std::size_t, std::size_t)>& red)
{
    auto lighting = std::make_unique<TerrainLighting>();
    for (std::size_t y = 0; y < TerrainSide; ++y)
    {
        for (std::size_t x = 0; x < TerrainSide; ++x)
        {
            const float value = red(x, y);
            lighting->cells[(y * TerrainSide) + x] = {value, value * 0.5f, 0.0f};
        }
    }
    return lighting;
}

// Entre las cuatro celdas: con la luz x + 10 · y en el rojo, en (150, 250) da 1,5 + 25.
TEST(TerrainLightingTest, TheLightAtAPointIsBetweenItsFourCells)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t y)
                                     { return static_cast<float>(x) + (10.0f * static_cast<float>(y)); });
    const glm::vec3 light = LightAt(*lighting, 150.0f, 250.0f);
    EXPECT_NEAR(light.x, 26.5f, Rounding);
    EXPECT_NEAR(light.y, 13.25f, Rounding);
    EXPECT_FLOAT_EQ(light.z, 0.0f);
    // Con otro valor en cada celda, el bilineal no es el plano: (1, 1) en 1 y las demás en 0.
    const auto peak = LightingOf([](std::size_t x, std::size_t y) { return x == 1 && y == 1 ? 1.0f : 0.0f; });
    EXPECT_NEAR(LightAt(*peak, 125.0f, 175.0f).x, 0.25f * 0.75f, Rounding);
}

// Fuera de las celdas 0 a 254 no hay luz. x · 0,01 se trunca: de −100 a 0 todavía es la celda 0, y
// extrapola.
TEST(TerrainLightingTest, OutsideTheGridThereIsNoLight)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t) { return 1.0f + static_cast<float>(x); });
    EXPECT_EQ(LightAt(*lighting, -150.0f, 500.0f), glm::vec3(0.0f));
    EXPECT_EQ(LightAt(*lighting, 500.0f, -150.0f), glm::vec3(0.0f));
    EXPECT_EQ(LightAt(*lighting, 25500.0f, 500.0f), glm::vec3(0.0f));
    EXPECT_EQ(LightAt(*lighting, 500.0f, 25500.0f), glm::vec3(0.0f));
    EXPECT_NEAR(LightAt(*lighting, 25499.0f, 500.0f).x, 255.99f, 1e-3f);
    EXPECT_NEAR(LightAt(*lighting, -50.0f, 500.0f).x, 0.5f, Rounding);
}
// Sin la carpeta del mundo, el error nombra el primer archivo que falta, en la carpeta del mapa (el 55
// es World56).
TEST(TerrainLightingTest, LoadingAWorldWithoutItsFilesNamesWhatIsMissing)
{
    const Client::Tests::TemporaryFolder folder("terrain-lighting");
    const auto loaded = LoadTerrainLighting(folder.Path(), 55);
    ASSERT_FALSE(loaded.HasValue());
    EXPECT_NE(loaded.Error().find("World56\\TerrainHeight.OZB"), std::string::npos) << loaded.Error();
}
} // namespace
