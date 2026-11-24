#include "World/SiegeStoneEffects.h"

#include "Crt/CrtRandom.h"
#include "World/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <memory>

namespace
{
using Client::Crt::CrtRandom;
using Client::World::ModelEffectArguments;
using Client::World::ModelEffects;
using Client::World::SiegeIceStoneEffect;
using Client::World::SiegeStoneEffect;

TEST(SiegeStoneEffectsTest, AFixedTargetStillConsumesItsDiscardedSpeedDraw)
{
    auto effects = std::make_unique<ModelEffects>();
    CrtRandom random(1);
    const ModelEffectArguments arguments{.type = SiegeStoneEffect,
                                         .position = {100, 200, 500},
                                         .angles = {300, 400, 100},
                                         .subtype = 1,
                                         .key = 1,
                                         .byte89 = 0x23,
                                         .byte88 = 0x81};
    const auto created = Client::World::CreateSiegeStone(arguments, *effects, {}, random);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_TRUE(created.Value().has_value());
    const auto& effect = effects->Effects()[*created.Value()];
    EXPECT_EQ(effect.life, 300);
    EXPECT_EQ(effect.speed, 29.0f);
    EXPECT_EQ(effect.transform.scale, 2.0f);
    EXPECT_EQ(effect.turnDirection, 2.0f);
    EXPECT_EQ(effect.siegeTarget, (glm::vec3{300, 400, 100}));
    EXPECT_EQ(effect.start, arguments.position);
    EXPECT_EQ(effect.transform.angles, glm::vec3(0.0f));
    EXPECT_EQ(effect.light, glm::vec3(1.0f));
    EXPECT_EQ(effect.velocity, (glm::vec3{0.2f, 0.2f, 5.0f}));
    EXPECT_EQ(effect.state, 0x81);
    EXPECT_EQ(effect.byte89, 0x23);
    EXPECT_EQ(random.State(), 0x29e2c0u);
}

TEST(SiegeStoneEffectsTest, AnAutomaticTargetReadsHeroXAndSelectsItsNativeRow)
{
    for (const auto type : {SiegeStoneEffect, SiegeIceStoneEffect})
    {
        auto effects = std::make_unique<ModelEffects>();
        CrtRandom random(1);
        const auto created = Client::World::CreateSiegeStone({.type = type, .position = {100, 200, 500}}, *effects,
                                                             {10000, 0, 0}, random);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_TRUE(created.Value().has_value());
        const auto& effect = effects->Effects()[*created.Value()];
        EXPECT_EQ(effect.siegeTarget, (glm::vec3{9500, type == SiegeStoneEffect ? 9900.0f : 8400.0f, 100}));
        CrtRandom expected(1);
        expected.Discard(type == SiegeStoneEffect ? 4 : 3);
        EXPECT_EQ(random.State(), expected.State());
    }
}

TEST(SiegeStoneEffectsTest, AFullModelPoolDoesNotConstructOrConsumeRandom)
{
    auto effects = std::make_unique<ModelEffects>();
    for (auto& effect : effects->Effects())
        effect.alive = true;
    CrtRandom random(19);
    const auto created = Client::World::CreateSiegeStone({.type = SiegeStoneEffect}, *effects, {}, random);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_FALSE(created.Value().has_value());
    EXPECT_EQ(random.State(), 19u);
}
} // namespace
