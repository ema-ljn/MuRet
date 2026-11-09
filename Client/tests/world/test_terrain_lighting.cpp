#include "World/TerrainLighting.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainLight.h"
#include "support/TemporaryFolder.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
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

// En la banda ordinaria, sin reducción low32: −100 exacto se rechaza; entre −100 y 0
// la celda es 0 y extrapola.
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
// Vectores analíticos del contrato 5f99e0 bajo el perfil portable enmascarado/PC53/RN,
// con tabla estable: no son capturas nuevas del original ni fijan su CW efectivo.
TEST(TerrainLightingTest, LargeFiniteCoordinatesUseSigned64Low32OnBothAxes)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t y)
                                     { return 1.0f + static_cast<float>(x) + (2.0f * static_cast<float>(y)); });
    // Tras el store F32, ±100·2^32 pasa a ±2^32: FTOL64 válido, low32=0.
    constexpr float LargeCoordinate = 0x1.9p38f;
    // Con V(x,y)=1+x+2·y, los pesos siguen siendo ±2^32, no su parte fraccionaria.
    // En PC53 queda ±2^32+1 en X, ±2^33+1 en Y y ±3·2^32+1 en la diagonal;
    // el store F32 pierde el +1 en los tres casos.
    for (const float sign : {1.0f, -1.0f})
    {
        const float coordinate = sign * LargeCoordinate;
        const glm::vec3 alongX = LightAt(*lighting, coordinate, 0.0f);
        EXPECT_EQ(alongX, glm::vec3(sign * 0x1p32f, sign * 0x1p31f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(alongX.z), 0U);
        const glm::vec3 alongY = LightAt(*lighting, 0.0f, coordinate);
        EXPECT_EQ(alongY, glm::vec3(sign * 0x1p33f, sign * 0x1p32f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(alongY.z), 0U);
        const glm::vec3 diagonal = LightAt(*lighting, coordinate, coordinate);
        EXPECT_EQ(diagonal, glm::vec3(sign * 0x1.8p33f, sign * 0x1.8p32f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(diagonal.z), 0U);
    }
}

TEST(TerrainLightingTest, Signed64BoundaryCoordinatesContinueWithLow32Zero)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t y)
                                     { return 1.0f + static_cast<float>(x) + (2.0f * static_cast<float>(y)); });
    // El store da ±2^63: −2^63 es válido; +2^63 es invalid enmascarada. Ambos low32 son 0.
    constexpr float BoundaryCoordinate = 0x1.9p69f;
    // Con V(x,y)=1+x+2·y, X solo da ±2^63 y Y solo ±2^64 tras redondear.
    // En la diagonal PC53 redondea L=±2^64+1 y R=±2^64+2 al mismo ±2^64:
    // R−L=0, así que el resultado no es ±3·2^63. El verde es la mitad.
    for (const float sign : {1.0f, -1.0f})
    {
        const float coordinate = sign * BoundaryCoordinate;
        const glm::vec3 alongX = LightAt(*lighting, coordinate, 0.0f);
        EXPECT_EQ(alongX, glm::vec3(sign * 0x1p63f, sign * 0x1p62f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(alongX.z), 0U);
        const glm::vec3 alongY = LightAt(*lighting, 0.0f, coordinate);
        EXPECT_EQ(alongY, glm::vec3(sign * 0x1p64f, sign * 0x1p63f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(alongY.z), 0U);
        const glm::vec3 diagonal = LightAt(*lighting, coordinate, coordinate);
        EXPECT_EQ(diagonal, glm::vec3(sign * 0x1p64f, sign * 0x1p63f, 0.0f));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(diagonal.z), 0U);
    }
}

TEST(TerrainLightingTest, SignedLow32ChecksAcceptZeroAnd254AndRejectMinusOneAnd255)
{
    const auto lighting = LightingOf([](std::size_t, std::size_t) { return 1.0f; });
    for (const float coordinate : {0.0f, 25400.0f})
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, coordinate, 0.0f).x), 0x3f800000U);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, 0.0f, coordinate).x), 0x3f800000U);
    }
    for (const float coordinate : {-100.0f, 25500.0f})
    {
        for (const glm::vec3 light : {LightAt(*lighting, coordinate, 0.0f), LightAt(*lighting, 0.0f, coordinate)})
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(light.x), 0U);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(light.y), 0U);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(light.z), 0U);
        }
    }
}

TEST(TerrainLightingTest, MaskedInvalidCoordinatesInterpolateInsteadOfReturningZero)
{
    const auto lighting = LightingOf([](std::size_t, std::size_t) { return 1.0f; });
    for (const float coordinate : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                   -std::numeric_limits<float>::infinity()})
    {
        for (const glm::vec3 light : {LightAt(*lighting, coordinate, 0.0f), LightAt(*lighting, 0.0f, coordinate)})
        {
            // No se fija signo/payload de NaN: 0 por un peso no finito sigue hasta interpolar.
            EXPECT_TRUE(std::isnan(light.x));
            EXPECT_TRUE(std::isnan(light.y));
            EXPECT_TRUE(std::isnan(light.z));
        }
        // El otro eje sí rechaza: low32=0 no saltea las cuatro guardas espaciales.
        EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, coordinate, -100.0f).x), 0U);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, 25500.0f, coordinate).x), 0U);
    }
}

TEST(TerrainLightingTest, BothScaledCoordinatesAreStoredAsFloatBeforeConversion)
{
    const auto lighting = LightingOf([](std::size_t x, std::size_t y) { return x == 1 && y == 1 ? 0.0f : 1.0f; });
    // 100·K en double es menor que 1; el store F32 da 1 exacto en ambos ejes.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(LightAt(*lighting, 100.0f, 100.0f).x), 0U);
}

TEST(TerrainLightingTest, Pc53ColumnsRetainTheBitUntilTheFinalFloatStore)
{
    auto lighting = std::make_unique<TerrainLighting>();
    lighting->cells.fill({0.0f, 0.0f, 0.0f});
    lighting->cells[CellIndex(0, 0)].fill(std::bit_cast<float>(0x3f000000U));
    lighting->cells[CellIndex(0, 1)].fill(std::bit_cast<float>(0x3f000001U));
    // X=1/4, Y=1/2; L=1/2+2^-25, R=0; store(3/8+3·2^-27) = 0x3ec00001.
    // Redondear L a F32 antes de interpolar X perdería el bit: 0x3ec00000.
    const glm::vec3 light = LightAt(*lighting, 25.0f, 50.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light.x), 0x3ec00001U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light.y), 0x3ec00001U);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(light.z), 0x3ec00001U);
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
