#include "World/TerrainLighting.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Data/Terrain/TerrainLight.h"
#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <initializer_list>

// LoadTerrainLighting con el Data\ del laboratorio (Client/tests/support/GoldenDir.h): el mundo del
// login, el 55 (World56), y la luz donde 0x6349a0 pone el logo y el personaje; y la luz sombreada de
// Lorencia, Devias y el mapa 30 en celdas con pendiente.
//
// Los bits esperados salen de Client/docs/research/scripts/game/maps/terrain_light.py, un cálculo aparte
// del perfil PC53 del ASM (0x5fd000, 0x5f8b10, 0x5f8d90 y 0x5f99e0) sobre los mismos archivos, que no
// comparte código con MuRet. No son del original: la luz sombreada (0x7c0ccc4) no se volcó. Los volcados
// de 0x078ecc10 (lor_light.bin y s31_light.bin) son la luz sin sombrear, la que 0x5f8d90 lee, y esa es
// la de MuRet bit a bit (MemoryTerrainLight).
namespace
{
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::TerrainLightColor;
using Client::World::LightAt;
using Client::World::LoadTerrainLighting;
using GoldenTerrainLighting = Client::Tests::GoldenDataTest;

using ChannelBits = std::array<std::uint32_t, 3>;

constexpr std::uint8_t LoginMap = 55;
constexpr std::uint8_t LorenciaMap = 0;
constexpr std::uint8_t DeviasMap = 2;
constexpr std::uint8_t BrightMap = 30;

struct ShadedCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
    ChannelBits bits{};
};

ChannelBits BitsOf(const glm::vec3& light)
{
    return {std::bit_cast<std::uint32_t>(light.x), std::bit_cast<std::uint32_t>(light.y),
            std::bit_cast<std::uint32_t>(light.z)};
}

ChannelBits BitsOf(const TerrainLightColor& light)
{
    return {std::bit_cast<std::uint32_t>(light[0]), std::bit_cast<std::uint32_t>(light[1]),
            std::bit_cast<std::uint32_t>(light[2])};
}

void ExpectALight(const glm::vec3& light)
{
    for (const float channel : {light.x, light.y, light.z})
    {
        EXPECT_GE(channel, 0.0f);
        EXPECT_LE(channel, 1.0f);
    }
    EXPECT_GT(light.x + light.y + light.z, 0.0f);
}

void ExpectTheShadedCells(const std::filesystem::path& dataRoot, std::uint8_t map,
                          std::initializer_list<ShadedCell> cells)
{
    const auto lighting = LoadTerrainLighting(dataRoot, map);
    ASSERT_TRUE(lighting.HasValue()) << lighting.Error();
    for (const ShadedCell& cell : cells)
    {
        EXPECT_EQ(BitsOf(lighting.Value()->cells[CellIndex(cell.x, cell.y)]), cell.bits)
            << "mapa " << int{map} << ", celda (" << int{cell.x} << ", " << int{cell.y} << ")";
    }
}

TEST_F(GoldenTerrainLighting, TheLoginWorldHasLightWhereItsModelsGo)
{
    const auto lighting = LoadTerrainLighting(m_dataRoot, LoginMap);
    ASSERT_TRUE(lighting.HasValue()) << lighting.Error();
    // El personaje y el logo (06 § El login, al entrar).
    ExpectALight(LightAt(*lighting.Value(), 10603.0f, 14112.0f));
    ExpectALight(LightAt(*lighting.Value(), 10680.0f, 15550.0f));
}

// Donde va el personaje las alturas no son planas (579, 546 y 555), la sombra da 0,989 y la luz 0,988;
// donde va el logo son 0, y la luz es la del OZJ (0,698; 0,678 y 0,655). Con la escala de las alturas
// en 1,5 en vez de 3, o sin la sombra, el personaje daría 0,9996 o 1.
TEST_F(GoldenTerrainLighting, TheLightOfTheLoginMatchesAnIndependentComputation)
{
    const auto lighting = LoadTerrainLighting(m_dataRoot, LoginMap);
    ASSERT_TRUE(lighting.HasValue()) << lighting.Error();
    EXPECT_EQ(BitsOf(LightAt(*lighting.Value(), 10603.0f, 14112.0f)),
              (ChannelBits{0x3f7d0097U, 0x3f7d0097U, 0x3f7d0097U}));
    EXPECT_EQ(BitsOf(LightAt(*lighting.Value(), 10680.0f, 15550.0f)),
              (ChannelBits{0x3f32b2b3U, 0x3f2dadaeU, 0x3f27a7a8U}));
}

// En estas celdas cada parte del perfil cambia los bits: con la normal en float (glm::cross y
// glm::length), con la sombra en float, o con la sombra guardada en float antes de los productos,
// alguno de los tres canales daría otro valor.
TEST_F(GoldenTerrainLighting, TheShadedLightOfLorenciaKeepsTheBitsOfTheAsmProfile)
{
    ExpectTheShadedCells(m_dataRoot, LorenciaMap,
                         {{104, 101, {0x3e77beb5U, 0x3e77beb5U, 0x3e77beb5U}},
                          {157, 120, {0x3eccc728U, 0x3ef8cbafU, 0x3ed658d6U}},
                          {143, 165, {0x3ea21d9cU, 0x3e679803U, 0x3e679803U}}});
}

TEST_F(GoldenTerrainLighting, TheShadedLightOfDeviasKeepsTheBitsOfTheAsmProfile)
{
    ExpectTheShadedCells(m_dataRoot, DeviasMap,
                         {{147, 21, {0x3e25eb2eU, 0x3e85a81eU, 0x3ed7146bU}},
                          {142, 22, {0x3e65c93aU, 0x3ea3e9ffU, 0x3f0b674dU}},
                          {136, 25, {0x3f1516c9U, 0x3f31889bU, 0x3f7a1dceU}}});
}

// El mapa 30 sombrea con −1 y 1; en la celda (146, 58) la cuenta da menos de 0 y la luz queda en +0.
TEST_F(GoldenTerrainLighting, TheShadedLightOfMapThirtyKeepsTheBitsOfTheAsmProfile)
{
    ExpectTheShadedCells(m_dataRoot, BrightMap,
                         {{123, 63, {0x3e727665U, 0x3e81470eU, 0x3e7b0b94U}},
                          {108, 65, {0x3ec532f8U, 0x3ed62ec9U, 0x3ecadc3eU}},
                          {146, 58, {0U, 0U, 0U}}});
}
} // namespace
