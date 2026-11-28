#include "Effects/ModelEffects.h"

#include "Crt/CrtRandom.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace
{
TEST(CrywolfEffectsTest, TheCraterVariantsKeepTheNativeSidesAndRetainedThirdComponent)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    const std::array<int, 3> lives{60, 30, 40};
    const std::array<float, 3> sides{4.5f, 2.5f, 3};
    for (int subtype = 0; subtype < 3; ++subtype)
    {
        effects->Effects()[0].alive = false;
        effects->Effects()[0].start.z = 73;
        const auto created = effects->CreateCraterEffect(
            {.type = 0x636, .position = {100, 200, 300}, .angles = {1, 2, 3}, .light = {2, 3, 4}, .subtype = subtype});
        ASSERT_EQ(created, 0u);
        const auto& crater = effects->Effects()[0];
        EXPECT_EQ(crater.life, lives[static_cast<std::size_t>(subtype)]);
        EXPECT_EQ(crater.start,
                  (glm::vec3{sides[static_cast<std::size_t>(subtype)], sides[static_cast<std::size_t>(subtype)], 73}));
        EXPECT_EQ(crater.light, glm::vec3(1));
    }
}

// 0x51f4ad..0x51f576: la /0 y un subtipo sin rama (9) quedan en vida 30 y escala 20; la /1 y la /2, vida 20 y (rand() %
// 10 + 10) · 0,1f; la /3, 15 y 1; la /4, 10 y (rand() % 6 + 6) · 0,1f. Llena, ningún rand().
TEST(CrywolfEffectsTest, TheLowShockWavesTakeTheirLifeAndScale)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    for (const std::int32_t subtype : {0, 1, 2, 3, 4, 9})
    {
        SCOPED_TRACE(subtype);
        effects->Effects()[0].alive = false;
        Client::Crt::CrtRandom random(7);
        ASSERT_EQ(effects->CreateCastleShockWave({.type = 0x6eb, .subtype = subtype}, random), 0u);
        Client::Crt::CrtRandom replay(7);
        std::int32_t life = 30;
        float scale = 20.0f;
        const double tenth = static_cast<double>(0.1f);
        if (subtype == 1 || subtype == 2)
        {
            life = 20;
            scale = static_cast<float>((replay.Next() % 10 + 10.0) * tenth);
        }
        else if (subtype == 3)
        {
            life = 15;
            scale = 1.0f;
        }
        else if (subtype == 4)
        {
            life = 10;
            scale = static_cast<float>((replay.Next() % 6 + 6.0) * tenth);
        }
        EXPECT_EQ(effects->Effects()[0].life, life);
        EXPECT_EQ(effects->Effects()[0].transform.scale, scale);
        EXPECT_EQ(random.State(), replay.State());
    }
    for (auto& effect : effects->Effects())
        effect.alive = true;
    Client::Crt::CrtRandom random(7);
    EXPECT_FALSE(effects->CreateCastleShockWave({.type = 0x6eb, .subtype = 4}, random));
    EXPECT_EQ(random.State(), 7u);
}

TEST(CrywolfEffectsTest, TheCastleWavesSortOnlyTheAllocatedEighthVariant)
{
    auto effects = std::make_unique<Client::Effects::ModelEffects>();
    const std::array<int, 4> lives{20, 50, 10, 10};
    const std::array<float, 4> scales{9, 9, 1, 2.2f};
    for (int subtype = 5; subtype <= 8; ++subtype)
    {
        effects->Effects()[0].alive = false;
        Client::Crt::CrtRandom random(7);
        const auto made =
            effects->CreateCastleShockWave({.type = 0x6eb, .light = {1, 0.2f, 0.4f}, .subtype = subtype}, random);
        ASSERT_EQ(made, 0u);
        EXPECT_EQ(effects->Effects()[0].life, lives[static_cast<std::size_t>(subtype - 5)]);
        EXPECT_FLOAT_EQ(effects->Effects()[0].transform.scale, scales[static_cast<std::size_t>(subtype - 5)]);
        EXPECT_EQ(effects->Effects()[0].light, (glm::vec3{1, 0.2f, 0.4f}));
        EXPECT_EQ(random.State(), subtype == 8 ? 0x003d7aaeu : 7u);
    }
    for (auto& effect : effects->Effects())
        effect.alive = true;
    Client::Crt::CrtRandom random(7);
    EXPECT_FALSE(effects->CreateCastleShockWave({.type = 0x6eb, .subtype = 8}, random));
    EXPECT_EQ(random.State(), 7u);
}
} // namespace
