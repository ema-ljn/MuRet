#include "World/SiegeGateEffects.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "Render/Models/Cloth.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>
namespace
{
TEST(SiegeGateEffectsTest, EveryGateUsesTheTwoTickConstructorAndOnlyTheCommonStepRandom)
{
    for (std::int16_t type = 0x3d; type <= 0x40; ++type)
    {
        auto effects = std::make_unique<Client::World::ModelEffects>();
        Client::World::Particles particles;
        Client::Render::Models::CrtRandom random(1);
        const auto created = Client::World::CreateCastleGateEffect({.type = type, .subtype = 1}, *effects);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        auto& effect = effects->Effects()[*created.Value()];
        EXPECT_EQ(effect.life, 2);
        EXPECT_EQ(effect.subtype, 1);
        EXPECT_EQ(random.State(), 1u);
        ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random).HasValue());
        EXPECT_TRUE(effect.alive);
        EXPECT_EQ(effect.life, 1);
        ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random).HasValue());
        EXPECT_FALSE(effect.alive);
        EXPECT_EQ(effect.life, 0);
        Client::Render::Models::CrtRandom expected(1);
        expected.Discard(2);
        EXPECT_EQ(random.State(), expected.State());
    }
}
} // namespace
