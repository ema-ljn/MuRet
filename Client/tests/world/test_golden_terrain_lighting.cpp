#include "World/TerrainLighting.h"

#include "support/GoldenDir.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

// LoadTerrainLighting con el Data\ del laboratorio (Client/tests/support/GoldenDir.h): el mundo del
// login, el 55 (World56), y la luz donde 0x6349a0 pone el logo y el personaje.
namespace
{
using Client::World::LightAt;
using Client::World::LoadTerrainLighting;
using GoldenTerrainLighting = Client::Tests::GoldenDataTest;

constexpr std::uint8_t LoginMap = 55;

void ExpectALight(const glm::vec3& light)
{
    for (const float channel : {light.x, light.y, light.z})
    {
        EXPECT_GE(channel, 0.0f);
        EXPECT_LE(channel, 1.0f);
    }
    EXPECT_GT(light.x + light.y + light.z, 0.0f);
}

TEST_F(GoldenTerrainLighting, TheLoginWorldHasLightWhereItsModelsGo)
{
    const auto lighting = LoadTerrainLighting(m_dataRoot, LoginMap);
    ASSERT_TRUE(lighting.HasValue()) << lighting.Error();
    // El personaje y el logo (06 § El login, al entrar).
    ExpectALight(LightAt(*lighting.Value(), 10603.0f, 14112.0f));
    ExpectALight(LightAt(*lighting.Value(), 10680.0f, 15550.0f));
}

// Contra un cálculo aparte, en Python sobre los mismos archivos (la revisión de la CL6.4): donde va el
// personaje las alturas no son planas (579, 546 y 555) y la sombra da 0,989; donde va el logo son 0, y
// la luz es la del OZJ. Con la escala de las alturas en 1,5 en vez de 3, o sin la sombra, el personaje
// daría 0,9996 o 1.
TEST_F(GoldenTerrainLighting, TheLightOfTheLoginMatchesAnIndependentComputation)
{
    const auto lighting = LoadTerrainLighting(m_dataRoot, LoginMap);
    ASSERT_TRUE(lighting.HasValue()) << lighting.Error();
    constexpr float Rounding = 2e-3f;
    const glm::vec3 character = LightAt(*lighting.Value(), 10603.0f, 14112.0f);
    EXPECT_NEAR(character.x, 0.988f, Rounding);
    const glm::vec3 logo = LightAt(*lighting.Value(), 10680.0f, 15550.0f);
    EXPECT_NEAR(logo.x, 0.698f, Rounding);
    EXPECT_NEAR(logo.y, 0.678f, Rounding);
    EXPECT_NEAR(logo.z, 0.655f, Rounding);
}
} // namespace
