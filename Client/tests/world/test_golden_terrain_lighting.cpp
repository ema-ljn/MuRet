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
} // namespace
