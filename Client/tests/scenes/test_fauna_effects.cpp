#include "Scenes/FaunaEffects.h"
#include "World/ModelEffects.h"
#include <gtest/gtest.h>
#include "Render/Models/Cloth.h"
#include "World/Particles.h"
#include "World/ParticleDrawParameters.h"
#include "World/Sprites.h"
#include "World/TerrainLighting.h"
#include <memory>

namespace
{
TEST(FaunaEffectsTest, LightningCreationPreservesUnwrittenScratchAndInitializesC2SubtypeThree)
{
    Client::World::ModelEffects effects;
    effects.Effects()[0].seen = true;
    effects.Effects()[0].reference = 91;
    auto created = Client::Scenes::CreateFaunaLightning(effects, {10, 20, 30});
    ASSERT_TRUE(created);
    const auto& effect = effects.Effects()[*created];
    EXPECT_TRUE(effect.alive);
    EXPECT_TRUE(effect.seen);
    EXPECT_EQ(effect.type, 0xc2);
    EXPECT_EQ(effect.subtype, 3);
    EXPECT_EQ(effect.life, 80);
    EXPECT_EQ(effect.transform.position, glm::vec3(10, 20, 30));
    EXPECT_FLOAT_EQ(effect.transform.scale, .3f);
    EXPECT_EQ(effect.velocity, glm::vec3(0, -12, 0));
    EXPECT_EQ(effect.objectState.blendMesh, 1);
    EXPECT_FLOAT_EQ(effect.reference, 91);
}
TEST(FaunaEffectsTest, AFullPoolDoesNotModifyAnySlot)
{
    Client::World::ModelEffects effects;
    for (auto& effect : effects.Effects())
    {
        effect.alive = true;
        effect.life = 17;
    }
    EXPECT_FALSE(Client::Scenes::CreateFaunaLightning(effects, {10, 20, 30}));
    for (const auto& effect : effects.Effects())
        EXPECT_EQ(effect.life, 17);
}
} // namespace

namespace
{
TEST(FaunaEffectsTest, LightningFireUsesTwoCreationDrawsAndItsSpecificRotationAndScaleStep)
{
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random;
    const auto created =
        particles.CreateChecked(Client::World::FireParticle, {10, 20, 30}, {0, 0, 0}, {1, 1, 1}, 8, .75f, 0, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value());
    Client::Render::Models::CrtRandom expected;
    static_cast<void>(expected.Next());
    const float rotation = static_cast<float>(expected.Next() % 360);
    EXPECT_EQ(random.State(), expected.State());
    auto& particle = particles.All()[*created.Value()];
    EXPECT_EQ(particle.life, 24);
    EXPECT_FLOAT_EQ(particle.scale, .75f);
    EXPECT_FLOAT_EQ(particle.rotation, rotation);
    ASSERT_FALSE(Client::World::StepParticles(particles, random));
    EXPECT_EQ(particle.life, 23);
    EXPECT_FLOAT_EQ(particle.scale, static_cast<float>(static_cast<double>(.75f) * .95f));
    EXPECT_FLOAT_EQ(particle.rotation, rotation + 5);
    const auto shape =
        Client::World::ParticleDrawParametersOf(particle, {40, 20}, Client::World::ParticleDrawKind::RotatedFire);
    EXPECT_FLOAT_EQ(shape.width, 10);
    EXPECT_FLOAT_EQ(shape.rotation, rotation + 5);
    EXPECT_EQ(shape.uvSize, glm::vec2(.25f, 1));
}
} // namespace

namespace
{
TEST(FaunaEffectsTest, ARealLightningStepPublishesItsLightParticleSpriteAndSharedRandomPrefix)
{
    Client::World::ModelEffects effects;
    ASSERT_TRUE(Client::Scenes::CreateFaunaLightning(effects, {1000, 1000, 300}));
    Client::World::Particles particles;
    Client::World::FrameSprites sprites;
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    Client::Render::Models::ClothWorld cloth;
    const auto result =
        Client::Scenes::StepFaunaLightning(effects.Effects()[0], 0, nullptr, particles, sprites, *lighting, cloth);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effects.Effects()[0].life, 79);
    EXPECT_EQ(effects.Effects()[0].transform.position, glm::vec3(1000, 988, 300));
    EXPECT_EQ(effects.Effects()[0].objectState.hiddenMesh, 0);
    EXPECT_FLOAT_EQ(effects.Effects()[0].objectState.light, 0);
    EXPECT_TRUE(particles.All()[0].alive);
    EXPECT_EQ(particles.All()[0].type, 0x5d9);
    EXPECT_EQ(particles.All()[0].subtype, 8);
    EXPECT_TRUE(sprites.Sprites()[0].alive);
    EXPECT_EQ(sprites.Sprites()[0].texture, 0x5fd);
    Client::Render::Models::CrtRandom expected;
    for (int i = 0; i < 8; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(cloth.random.State(), expected.State());
    EXPECT_GT(lighting->cells[10 * 256 + 10][0], 0);
}
} // namespace

namespace
{
TEST(FaunaEffectsTest, AFullParticlePoolAndAnOpenCounterKeepTheirNativeRandomSkips)
{
    Client::World::ModelEffects effects;
    ASSERT_TRUE(Client::Scenes::CreateFaunaLightning(effects, {1000, 1000, 300}));
    Client::World::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    Client::World::FrameSprites sprites;
    auto lighting = std::make_unique<Client::World::TerrainLighting>();
    Client::Render::Models::ClothWorld cloth;
    cloth.openCounterAccesses = 1;
    ASSERT_TRUE(
        Client::Scenes::StepFaunaLightning(effects.Effects()[0], 7, nullptr, particles, sprites, *lighting, cloth)
            .HasValue());
    Client::Render::Models::CrtRandom expected;
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    EXPECT_EQ(cloth.random.State(), expected.State());
    EXPECT_EQ(cloth.openCounterAccesses, 1);
    EXPECT_EQ(effects.Effects()[0].life, 79);
    EXPECT_TRUE(sprites.Sprites()[0].alive);
}
} // namespace
