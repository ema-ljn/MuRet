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
using Client::World::AddTerrainLight;
using Client::World::BrightShadingMap;
using Client::World::CopyTerrainLightWindow;
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

// Detecta perder el +3 inclusivo, recortar al borde o resetear la tabla completa.
TEST(TerrainLightingTest, CopiesOnlyTheInclusiveWrappedWindow)
{
    const auto source = std::make_unique<TerrainLighting>();
    auto destination = std::make_unique<TerrainLighting>();
    source->cells.fill({1.0f, 2.0f, 3.0f});
    destination->cells.fill({7.0f, 8.0f, 9.0f});
    CopyTerrainLightWindow(*source, *destination, -1, 0, 254, 255);
    for (const auto x : {255, 0, 1, 2, 3})
    {
        for (const auto y : {254, 255, 0, 1, 2})
        {
            EXPECT_EQ(destination->cells[CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y))],
                      (TerrainLightColor{1.0f, 2.0f, 3.0f}));
        }
    }
    EXPECT_EQ(destination->cells[CellIndex(4, 0)], (TerrainLightColor{7.0f, 8.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(0, 3)], (TerrainLightColor{7.0f, 8.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(100, 100)], (TerrainLightColor{7.0f, 8.0f, 9.0f}));
}

// Observa el wrap y los bits copiados. Recopiar índices ya visitados es idempotente, así que el
// estado final no distingue recorrer la ventana completa de limitarla a 256 columnas contiguas.
TEST(TerrainLightingTest, CopiesRepeatedWrapsAndPreservesNanPayloadsAndSignedZero)
{
    const auto source = std::make_unique<TerrainLighting>();
    auto destination = std::make_unique<TerrainLighting>();
    source->cells.fill({2.0f, 3.0f, 4.0f});
    source->cells[CellIndex(0, 255)] = {std::bit_cast<float>(std::uint32_t{0x7f812345}), -0.0f, 0.0f};
    CopyTerrainLightWindow(*source, *destination, -257, 256, -257, -257);
    const TerrainLightColor& copied = destination->cells[CellIndex(0, 255)];
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[0]), 0x7f812345u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[1]), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[2]), 0u);
    EXPECT_EQ(destination->cells[CellIndex(3, 2)], (TerrainLightColor{2.0f, 3.0f, 4.0f}));
    EXPECT_EQ(destination->cells[CellIndex(3, 3)], TerrainLightColor{});
    CopyTerrainLightWindow(*destination, *destination, -257, 256, -257, -257);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[0]), 0x7f812345u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(copied[1]), 0x80000000u);
}

// Una ventana vacía no modifica el destino (el bloque escalar de 0x5fbf60 queda fuera de esta API).
TEST(TerrainLightingTest, AnEmptyLightWindowPreservesTheDestination)
{
    const auto source = std::make_unique<TerrainLighting>();
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells.fill({1.0f, 2.0f, 3.0f});
    CopyTerrainLightWindow(*source, *destination, 5, 1, 0, 0);
    CopyTerrainLightWindow(*source, *destination, 0, 0, 5, 1);
    // Normalizar los extremos invertidos también dejaría (0,0) intacta: se observa toda la tabla.
    for (const TerrainLightColor& cell : destination->cells)
    {
        ASSERT_EQ(cell, (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    }
}

// first > last todavía copia cuando alcanza la cola +3. Cada celda tiene un color distinto;
// salir antes por extremos invertidos, normalizarlos o escribir fuera de la ventana debe fallar.
TEST(TerrainLightingTest, CopiesTheRemainingTailWhenWindowEndpointsAreInverted)
{
    const auto source = std::make_unique<TerrainLighting>();
    auto destination = std::make_unique<TerrainLighting>();
    for (std::size_t y = 0; y < TerrainSide; ++y)
    {
        for (std::size_t x = 0; x < TerrainSide; ++x)
        {
            source->cells[(y * TerrainSide) + x] = {static_cast<float>(x), static_cast<float>(y), 0.25f};
        }
    }
    const TerrainLightColor untouched{-1.0f, -2.0f, -3.0f};
    for (std::int32_t gap = 1; gap <= 3; ++gap)
    {
        destination->cells.fill(untouched);
        CopyTerrainLightWindow(*source, *destination, 5, 5 - gap, 7, 7 - gap);
        const std::size_t lastX = 8 - static_cast<std::size_t>(gap);
        const std::size_t lastY = 10 - static_cast<std::size_t>(gap);
        for (std::size_t y = 0; y < TerrainSide; ++y)
        {
            for (std::size_t x = 0; x < TerrainSide; ++x)
            {
                const std::size_t index = (y * TerrainSide) + x;
                const bool copied = x >= 5 && x <= lastX && y >= 7 && y <= lastY;
                ASSERT_EQ(destination->cells[index], copied ? source->cells[index] : untouched)
                    << "gap=" << gap << ", x=" << x << ", y=" << y;
            }
        }
    }
}

// Pesos exactos: centro 1, cardinal a distancia 1 de radio 2 = 1/2, borde = 0.
// Detecta otro radio fijo, techo en 1, falta del piso y suma fuera del círculo.
TEST(TerrainLightingTest, AddsRadialLightWithoutAnUpperClamp)
{
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells.fill({0.25f, 0.25f, 0.25f});
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {2.0f, -2.0f, 0.5f}, 2);
    EXPECT_EQ(destination->cells[CellIndex(10, 10)], (TerrainLightColor{2.25f, 0.0f, 0.75f}));
    EXPECT_EQ(destination->cells[CellIndex(11, 10)], (TerrainLightColor{1.25f, 0.0f, 0.5f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 9)], (TerrainLightColor{1.25f, 0.0f, 0.5f}));
    // Diagonal: peso F32((2 - sqrt(2)) / 2) = 0x3e95f61a. Con 2 y base 1/4: 0x3f55f61a.
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(11, 11)][0]), 0x3f55f61au);
    EXPECT_EQ(destination->cells[CellIndex(12, 10)], (TerrainLightColor{0.25f, 0.25f, 0.25f}));
    EXPECT_EQ(destination->cells[CellIndex(12, 12)], (TerrainLightColor{0.25f, 0.25f, 0.25f}));
}

// En el borde exacto el peso ancho es cero y no hay store ni piso: un destino negativo sobrevive.
// Procesar también ese cero cambiaría el rojo a +0, aunque el aporte de luz fuera cero.
TEST(TerrainLightingTest, SkipsZeroWeightBoundaryCellsWithoutClampingTheirDestination)
{
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells[CellIndex(12, 10)] = {-1.0f, -2.0f, -3.0f};
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {1.0f, 2.0f, 3.0f}, 2);
    EXPECT_EQ(destination->cells[CellIndex(12, 10)], (TerrainLightColor{-1.0f, -2.0f, -3.0f}));
}

// 100 * float(0,01) queda justo en 1 después del store F32. La distancia usa ese valor almacenado.
TEST(TerrainLightingTest, StoresScaledCoordinatesBeforeTheDistanceCalculation)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 100.0f, -100.0f, {1.0f, 2.0f, 3.0f}, 1);
    EXPECT_EQ(destination->cells[CellIndex(1, 255)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(0, 0)], TerrainLightColor{});
    AddTerrainLight(*destination, -50.0f, 0.0f, {4.0f, 5.0f, 6.0f}, 1);
    EXPECT_EQ(destination->cells[CellIndex(0, 0)], (TerrainLightColor{2.0f, 2.5f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(255, 0)], (TerrainLightColor{2.0f, 2.5f, 3.0f}));
}

// 005f97f6..005f97ff resta la coordenada de celda al X escalado float32, sin perder su fracción.
// Centro X=10,5, radio2: distancias0,5 y1,5 dan pesos exactos3/4 y1/4.
TEST(TerrainLightingTest, MeasuresRadialDistanceFromTheFractionalX)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 1050.0f, 2000.0f, {4.0f, 8.0f, 12.0f}, 2);
    EXPECT_EQ(destination->cells[CellIndex(10, 20)], (TerrainLightColor{3.0f, 6.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(11, 20)], (TerrainLightColor{3.0f, 6.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(9, 20)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(12, 20)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(8, 20)], TerrainLightColor{});
}

// 005f97ed..005f97f4 conserva también la fracción Y. El wrap afecta el índice, no la distancia.
TEST(TerrainLightingTest, MeasuresRadialDistanceFromTheFractionalNegativeY)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 1000.0f, -1050.0f, {4.0f, 8.0f, 12.0f}, 2);
    EXPECT_EQ(destination->cells[CellIndex(10, 245)], (TerrainLightColor{3.0f, 6.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 246)], (TerrainLightColor{3.0f, 6.0f, 9.0f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 244)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 247)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 248)], TerrainLightColor{});
}

// El radio 8 tiene peso 1/4 a distancia 6 y envuelve x e y negativos.
TEST(TerrainLightingTest, UsesTheSuppliedRadiusAndWrapsBothAxes)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 0.0f, 0.0f, {4.0f, 8.0f, 12.0f}, 8);
    EXPECT_EQ(destination->cells[CellIndex(250, 0)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(0, 250)], (TerrainLightColor{1.0f, 2.0f, 3.0f}));
    EXPECT_EQ(destination->cells[CellIndex(248, 0)], TerrainLightColor{});
}

// sqrt(10) se conserva en double: (4 - sqrt(10))/4 redondea a 0x3e5674f9.
// Redondear primero la raíz a float daría 0x3e5674f8 (un bit menos).
TEST(TerrainLightingTest, KeepsTheDistanceWideUntilTheWeightStore)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {1.0f, 0.0f, 0.0f}, 4);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(13, 11)][0]), 0x3e5674f9u);
}

// PC53/RN: F32((5 - sqrt(2))/5) = 0x3f37979f. Guardar antes la diferencia en F32 o dividir en
// float produce 0x3f37979e: el radio no es potencia de dos y la distancia es irracional.
TEST(TerrainLightingTest, KeepsTheDifferenceAndDivisionWideUntilTheWeightStore)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {1.0f, 0.0f, 0.0f}, 5);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(11, 11)][0]), 0x3f37979fu);
}

// El peso guardado de la diagonal es el mismo literal que el rojo inicial: -pesoF32 + pesoF32 = +0.
// Usar el peso ancho en el producto deja un positivo que el piso no borra.
TEST(TerrainLightingTest, MultipliesTheColorByTheStoredFloatWeight)
{
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells[CellIndex(11, 11)][0] = std::bit_cast<float>(std::uint32_t{0x3e95f61a});
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {-1.0f, 0.0f, 0.0f}, 2);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(11, 11)][0]), 0u);
}

// Radio 257 visita (0,0) cinco veces: centro y cuatro cardinales a 256, con peso F32 1/257.
// La suma secuencial almacenada da bits 0x3f81fe01; no se deduplica ni se capa el radio a 256.
TEST(TerrainLightingTest, ALongRadiusAccumulatesRepeatedVisitsToTheSameCell)
{
    auto destination = std::make_unique<TerrainLighting>();
    AddTerrainLight(*destination, 0.0f, 0.0f, {1.0f, 0.0f, 0.0f}, 257);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(0, 0)][0]), 0x3f81fe01u);
}

// El piso se aplica después de cada luz: invertirlas cambia el resultado.
TEST(TerrainLightingTest, SerialLightsPreserveTheirOrderAcrossNegativeClamps)
{
    auto forward = std::make_unique<TerrainLighting>();
    auto reverse = std::make_unique<TerrainLighting>();
    AddTerrainLight(*forward, 0.0f, 0.0f, {-2.0f, 0.0f, 0.0f}, 1);
    AddTerrainLight(*forward, 0.0f, 0.0f, {1.0f, 0.0f, 0.0f}, 1);
    AddTerrainLight(*reverse, 0.0f, 0.0f, {1.0f, 0.0f, 0.0f}, 1);
    AddTerrainLight(*reverse, 0.0f, 0.0f, {-2.0f, 0.0f, 0.0f}, 1);
    EXPECT_EQ(forward->cells[CellIndex(0, 0)][0], 1.0f);
    EXPECT_EQ(reverse->cells[CellIndex(0, 0)][0], 0.0f);
}

// En fila 9 el color fuente sube de 1 a 1,5; fila 10 debe leer ese 1,5, no una copia inicial.
TEST(TerrainLightingTest, AnAliasedColorIsReadLiveInRowOrder)
{
    auto destination = std::make_unique<TerrainLighting>();
    TerrainLightColor& color = destination->cells[CellIndex(10, 9)];
    color = {1.0f, 1.0f, 1.0f};
    AddTerrainLight(*destination, 1000.0f, 1000.0f, color, 2);
    EXPECT_EQ(color, (TerrainLightColor{1.5f, 1.5f, 1.5f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 10)], (TerrainLightColor{1.5f, 1.5f, 1.5f}));
}

// La fuente en columna 9 sube de 1 a 1,5 antes del centro en la misma fila: copiar por fila falla.
// Recorrer las columnas en orden descendente también dejaría el centro en 1.
TEST(TerrainLightingTest, AnAliasedColorIsReadLiveWithinTheSameRow)
{
    auto destination = std::make_unique<TerrainLighting>();
    TerrainLightColor& color = destination->cells[CellIndex(9, 10)];
    color = {1.0f, 1.0f, 1.0f};
    AddTerrainLight(*destination, 1000.0f, 1000.0f, color, 2);
    EXPECT_EQ(color, (TerrainLightColor{1.5f, 1.5f, 1.5f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 10)], (TerrainLightColor{1.5f, 1.5f, 1.5f}));
}

// (13,6) está a distancia 5 del centro y gana peso 1/2. Por filas cambia antes que el centro;
// por columnas cambia después y el centro recibiría 1 en vez de 1,5.
TEST(TerrainLightingTest, VisitsRowsBeforeColumnsWhenTheColorAliasesTheDestination)
{
    auto destination = std::make_unique<TerrainLighting>();
    TerrainLightColor& color = destination->cells[CellIndex(13, 6)];
    color = {1.0f, 1.0f, 1.0f};
    AddTerrainLight(*destination, 1000.0f, 1000.0f, color, 10);
    EXPECT_EQ(color, (TerrainLightColor{1.5f, 1.5f, 1.5f}));
    EXPECT_EQ(destination->cells[CellIndex(10, 10)], (TerrainLightColor{1.5f, 1.5f, 1.5f}));
}

// -denorm_min * 1/4 + -0 es negativo en double pero almacena -0 en float: comparar el store falla.
// En cambio -0 * 1 + -0 es igual a cero y debe preservar el signo, sin clamp a +0.
TEST(TerrainLightingTest, ClampsTheWideNegativeSumBeforeReloadingItsNegativeZeroStore)
{
    auto destination = std::make_unique<TerrainLighting>();
    destination->cells.fill({-0.0f, -0.0f, -0.0f});
    AddTerrainLight(*destination, 1000.0f, 1000.0f, {-std::numeric_limits<float>::denorm_min(), -0.0f, 0.0f}, 8);
    const TerrainLightColor& quarter = destination->cells[CellIndex(16, 10)];
    EXPECT_EQ(std::bit_cast<std::uint32_t>(quarter[0]), 0u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(quarter[1]), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(10, 10)][1]), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(destination->cells[CellIndex(18, 10)][0]), 0x80000000u);
}
} // namespace
