#include "World/SiegeVertexEffects.h"

#include "Render/Models/Cloth.h"
#include "World/GameModelEffects.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <memory>
#include <string>

namespace
{
TEST(SiegeVertexEffectsTest, TheGateConstructorConsumesEightDrawsInNativeOrder)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Render::Models::CrtRandom random(1), expected(1);
    const auto slot = Client::World::CreateSiegeVertexEffect({.type = 0x12f, .position = {2, 3, 4}}, *effects, random);
    ASSERT_TRUE(slot.has_value());
    const auto& effect = effects->Effects()[*slot];
    EXPECT_EQ(effect.life, 36);
    EXPECT_FLOAT_EQ(effect.transform.scale, 0.8f);
    EXPECT_EQ(effect.turnDirection, 10);
    EXPECT_EQ(effect.transform.position, (glm::vec3{2, 3, 54}));
    EXPECT_EQ(effect.transform.angles, (glm::vec3{244, 318, 198}));
    expected.Discard(8);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(SiegeVertexEffectsTest, TheStatueSubtypeAddsItsNinthDrawWhileAFullPoolConsumesNothing)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Render::Models::CrtRandom random(1), expected(1);
    const auto slot = Client::World::CreateSiegeVertexEffect({.type = 0x131}, *effects, random);
    ASSERT_TRUE(slot.has_value());
    EXPECT_EQ(effects->Effects()[*slot].subtype, 1);
    expected.Discard(9);
    EXPECT_EQ(random.State(), expected.State());
    for (auto& effect : effects->Effects())
        effect.alive = true;
    EXPECT_FALSE(Client::World::CreateSiegeVertexEffect({.type = 0x12f}, *effects, random).has_value());
    EXPECT_EQ(random.State(), expected.State());
}

TEST(SiegeVertexEffectsTest, TheWholeStepPreservesTheSecondRotatedMovementAndTheCommonLightDraw)
{
    Client::World::ModelEffect effect;
    effect.alive = true;
    effect.type = 0x12f;
    effect.life = 20;
    effect.transform.position = {0, 0, 100};
    effect.transform.scale = 0;
    effect.velocity = {10, 0, 0};
    effect.turnDirection = 8;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1), expected(1);
    const Client::World::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    const auto result = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(effect.transform.position, (glm::vec3{19, 0, 102}));
    EXPECT_EQ(effect.previousPosition, (glm::vec3{0, 0, 100}));
    EXPECT_EQ(effect.velocity, (glm::vec3{9, 0, 0}));
    EXPECT_EQ(effect.life, 19);
    EXPECT_EQ(effect.alpha, 2);
    expected.Discard(1);
    EXPECT_EQ(random.State(), expected.State());
}

TEST(SiegeVertexEffectsTest, IndeterminateDustCutsAfterThePrefixUnlessTheParticlePoolIsFull)
{
    Client::World::ModelEffect initial;
    initial.alive = true;
    initial.type = 0x131;
    initial.life = 20;
    initial.transform.position.z = 100;
    initial.transform.scale = 0;
    initial.velocity.x = 10;
    initial.turnDirection = 8;
    Client::World::Particles particles;
    const Client::World::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    Client::Render::Models::CrtRandom random(1);
    // El segundo sorteo de seed1 es18467; seed2 produce29216. Buscar el primer estado con polvo.
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        auto effect = initial;
        const auto before = random.State();
        const auto result =
            Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
        if (result.HasValue())
            continue;
        EXPECT_NE(result.Error().find("0x529520"), std::string::npos);
        EXPECT_EQ(effect.life, 20);
        EXPECT_EQ(effect.transform.position.x, 10);
        for (auto& particle : particles.All())
            particle.alive = true;
        random = Client::Render::Models::CrtRandom(before);
        effect = initial;
        const auto full = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
        ASSERT_TRUE(full.HasValue()) << full.Error();
        EXPECT_EQ(effect.life, 19);
        EXPECT_EQ(effect.transform.position.x, 19);
        return;
    }
    FAIL() << "la secuencia no alcanzó el polvo";
}

TEST(SiegeVertexEffectsTest, TheGuardDebrisAlsoCompletesItsTailWhenTheDustPoolIsFull)
{
    Client::World::ModelEffect initial;
    initial.alive = true;
    initial.type = 0x10a;
    initial.life = 20;
    initial.transform.position.z = 100;
    initial.transform.scale = 0;
    initial.velocity.x = 10;
    initial.turnDirection = 8;
    Client::World::Particles particles;
    const Client::World::ModelEffectStepServices services{.terrainHeight = [](float, float) { return 0; }};
    Client::Render::Models::CrtRandom random(8);
    auto effect = initial;
    const auto room = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_FALSE(room.HasValue());
    EXPECT_NE(room.Error().find("0x5252e6"), std::string::npos);
    EXPECT_EQ(effect.life, 20);
    for (auto& particle : particles.All())
        particle.alive = true;
    effect = initial;
    random = Client::Render::Models::CrtRandom(8);
    const auto full = Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, nullptr, services);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_EQ(effect.life, 19);
    EXPECT_EQ(effect.transform.position.x, 19);
}
} // namespace
