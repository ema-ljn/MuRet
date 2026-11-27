#include "Terrain/TerrainLighting.h"

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

// LoadTerrainLighting with the lab's Data\ (Client/tests/support/GoldenDir.h): the login's world, 55 (World56), and the
// light where 0x6349a0 places the logo and the character; and the shaded light of Lorencia, Devias and map 30 on sloped
// cells.
//
// The expected bits come from Client/docs/research/scripts/game/maps/terrain_light.py, a separate computation of the
// ASM's PC53 profile (0x5fd000, 0x5f8b10, 0x5f8d90 and 0x5f99e0) over the same files, sharing no code with MuRet. They
// are not the original's: the shaded light (0x7c0ccc4) was not dumped. The dumps of 0x078ecc10 (lor_light.bin and
// s31_light.bin) are the unshaded light, the one 0x5f8d90 reads, and that one matches MuRet's bit for bit
// (MemoryTerrainLight).
namespace
{
using Client::Data::Terrain::CellIndex;
using Client::Data::Terrain::TerrainLightColor;
using Client::Terrain::LightAt;
using Client::Terrain::LoadTerrainLighting;
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
    // The character and the logo (06 § El login, al entrar).
    ExpectALight(LightAt(*lighting.Value(), 10603.0f, 14112.0f));
    ExpectALight(LightAt(*lighting.Value(), 10680.0f, 15550.0f));
}

// Where the character goes the heights are not flat (579, 546 and 555), the shade gives 0.989 and the light 0.988;
// where the logo goes they are 0, and the light is the OZJ's (0.698, 0.678 and 0.655). With the height scale at 1.5
// instead of 3, or without the shade, the character would give 0.9996 or 1.
TEST_F(GoldenTerrainLighting, TheLightOfTheLoginMatchesAnIndependentComputation)
{
    const auto lighting = LoadTerrainLighting(m_dataRoot, LoginMap);
    ASSERT_TRUE(lighting.HasValue()) << lighting.Error();
    EXPECT_EQ(BitsOf(LightAt(*lighting.Value(), 10603.0f, 14112.0f)),
              (ChannelBits{0x3f7d0097U, 0x3f7d0097U, 0x3f7d0097U}));
    EXPECT_EQ(BitsOf(LightAt(*lighting.Value(), 10680.0f, 15550.0f)),
              (ChannelBits{0x3f32b2b3U, 0x3f2dadaeU, 0x3f27a7a8U}));
}

// On these cells each part of the profile changes the bits: with the normal in float (glm::cross and glm::length),
// with the shade in float, or with the shade stored as float before the products, one of the three channels would
// give another value.
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

// Map 30 shades with −1 and 1; at cell (146, 58) the computation gives less than 0 and the light stays at +0.
TEST_F(GoldenTerrainLighting, TheShadedLightOfMapThirtyKeepsTheBitsOfTheAsmProfile)
{
    ExpectTheShadedCells(m_dataRoot, BrightMap,
                         {{123, 63, {0x3e727665U, 0x3e81470eU, 0x3e7b0b94U}},
                          {108, 65, {0x3ec532f8U, 0x3ed62ec9U, 0x3ecadc3eU}},
                          {146, 58, {0U, 0U, 0U}}});
}
} // namespace
