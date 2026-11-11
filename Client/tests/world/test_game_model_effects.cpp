#include "World/GameModelEffects.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/Skeleton.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"

#include "support/PlayerActors.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>

namespace
{
TEST(GameModelEffectsTest, NoriaUpdatesLightAndPitchThenTheCommonLifeWithOneRandom)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 322;
    effect.life = 100;
    effect.turnDirection = 3.0f;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto result = Client::World::StepGameModelEffect(effect, nullptr, particles, 0.0f, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effect.light, glm::vec3(0.01f));
    EXPECT_EQ(effect.transform.angles.y, 7.0f);
    EXPECT_EQ(effect.life, 99);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(GameModelEffectsTest, MagicalTeleportCreatesEighteenSparksWithThreeAnglesEach)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::World::TeleportEffect;
    effect.life = 10;
    effect.transform.position = {100.0f, 200.0f, 300.0f};
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto result = Client::World::StepGameModelEffect(effect, nullptr, particles, 0.0f, random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effect.life, 9);
    ASSERT_TRUE(particles.All()[0].alive);
    EXPECT_EQ(particles.All()[0].position.z, 324.0f);
    EXPECT_EQ(particles.All()[0].scale, 12.0f);
    EXPECT_EQ(particles.All()[0].subtype, 1);
    EXPECT_EQ(particles.All()[0].life, 2);
    EXPECT_EQ(particles.All()[17].position.z, 732.0f);
    EXPECT_EQ(particles.All()[17].scale, 6.0f);
    EXPECT_FALSE(particles.All()[18].alive);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 55).State());
}
TEST(GameModelEffectsTest, AFullSparkPoolConsumesOnlyTheModelEffectPrefix)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::World::TeleportEffect;
    effect.life = 10;
    Client::World::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(effect.life, 9);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(GameModelEffectsTest, TeleportSparksUseTheirOwnShrinkRatherThanTheRavenFade)
{
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto created =
        particles.CreateChecked(Client::World::SparkParticle, {}, {}, {1.0f, 0.5f, 0.25f}, 1, 12.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 3).State());
    const auto step = Client::World::StepParticlesChecked(particles, random);
    ASSERT_TRUE(step.HasValue());
    EXPECT_EQ(particles.All()[0].scale, 10.0f);
    EXPECT_EQ(particles.All()[0].light, glm::vec3(1.0f, 0.5f, 0.25f));
}
TEST(GameModelEffectsTest, NoriaUsesTheStoredBlueArgumentAtANonzeroClock)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 324;
    effect.life = 100;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 1000.0f, random).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.x), 0x3e40c25fu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.y), 0x3e55553du);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(effect.light.z), 0x3e4f93a4u);
}
TEST(GameModelEffectsTest, TextureEffectsSkipTheNativeModelAnimationRange)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::World::ArrivalEffect;
    effect.life = 100;
    effect.speed = 1.0f;
    effect.animation.frame = 1.0f;
    Client::Render::Models::Skeleton skeleton;
    skeleton.actions.resize(1);
    skeleton.actions[0].keyCount = 4;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, &skeleton, particles, 1000.0f, random).HasValue());
    EXPECT_EQ(effect.animation.frame, 1.0f);
    EXPECT_EQ(effect.life, 99);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 1).State());
}
TEST(GameModelEffectsTest, SparseParticleSlotsPreserveTheirPayloadAndConsumeOnlyTheirThreeAngles)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = Client::World::TeleportEffect;
    effect.life = 10;
    Client::World::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    particles.All()[5].alive = false;
    particles.All()[7].alive = false;
    particles.All()[5].field38 = 0x12345678;
    particles.All()[5].rotationFactor = 23.0f;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0.0f, random).HasValue());
    EXPECT_EQ(particles.All()[5].scale, 12.0f);
    EXPECT_EQ(particles.All()[7].scale, 6.0f);
    EXPECT_EQ(particles.All()[5].position.z, 24.0f);
    EXPECT_EQ(particles.All()[7].position.z, 48.0f);
    EXPECT_EQ(particles.All()[5].field38, 0x12345678u);
    EXPECT_EQ(particles.All()[5].rotationFactor, 23.0f);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(1, 7).State());
}
} // namespace
