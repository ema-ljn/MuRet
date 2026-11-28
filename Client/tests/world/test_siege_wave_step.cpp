#include "Crt/CrtRandom.h"
#include "Effects/GameModelEffects.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include <gtest/gtest.h>
#include <bit>
#include <memory>
namespace
{
TEST(SiegeWaveStepTest, NewChildrenInLaterSlotsAdvanceWithinTheSameAscendingPoolWalk)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::World::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto slot = effects->CreateCastleShockWave(
        {.type = Client::World::ShockWaveEffect, .light = {2, 4, 6}, .subtype = 6}, random);
    ASSERT_TRUE(slot.has_value());
    effects->Effects()[*slot].life = 48;
    const Client::World::ModelEffectStepServices services{.effects = effects.get()};
    for (auto& effect : effects->Effects())
        ASSERT_TRUE(
            Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services).HasValue());
    EXPECT_EQ(effects->Effects()[0].life, 47);
    EXPECT_EQ(effects->Effects()[1].subtype, 5);
    EXPECT_EQ(effects->Effects()[1].life, 19);
    EXPECT_EQ(effects->Effects()[1].light, (glm::vec3{2, 4, 6}));
    Client::Crt::CrtRandom expected(1);
    expected.Discard(2);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(SiegeWaveStepTest, TheShortImpactExpandsAndFadesRgbWithoutFadingAlpha)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::World::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto slot = effects->CreateCastleShockWave(
        {.type = Client::World::ShockWaveEffect, .light = {1, 2, 3}, .subtype = 7}, random);
    ASSERT_TRUE(slot.has_value());
    auto& effect = effects->Effects()[*slot];
    effect.life = 5;
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random).HasValue());
    EXPECT_EQ(effect.transform.scale, 3.5f);
    EXPECT_EQ(effect.alpha, 1);
    EXPECT_EQ(effect.life, 4);
    const float fade = std::bit_cast<float>(0x3f44ec4fu);
    EXPECT_EQ(effect.light.x, fade);
    EXPECT_EQ(effect.light.y, fade * 2);
    EXPECT_EQ(effect.light.z, static_cast<float>(static_cast<double>(fade) * 3));
}
} // namespace
