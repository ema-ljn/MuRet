#include "World/ModelEffects.h"
#include "World/KalimaWorld.h"
#include "World/KalimaWaves.h"
#include "World/GameModelEffects.h"
#include "World/Particles.h"
#include "Render/Models/Cloth.h"
#include <gtest/gtest.h>
#include <glm/ext/vector_float3.hpp>
#include <memory>

TEST(KalimaModelEffectTest, NativeNineUsesItsOwnRandomScaleAndTwoMovements)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    Client::Render::Models::CrtRandom random(7);
    const auto made = effects->CreateKalimaEffect({6400, 6400, 1000}, {}, {0.3f, 0.8f, 1}, random);
    ASSERT_EQ(made, 0u);
    auto& effect = effects->Effects()[0];
    EXPECT_EQ(effect.life, 30);
    EXPECT_EQ(effect.transform.scale, 0.7000000476837158f);
    EXPECT_EQ(effect.speed, 10);
    EXPECT_EQ(effect.velocity, glm::vec3(-0.1f, -0.1f, -0.5f));
    EXPECT_EQ(random.State(), 0x003d7aaeu);
    Client::World::Particles particles;
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random).HasValue());
    EXPECT_EQ(effect.life, 29);
    EXPECT_EQ(effect.speed, 13);
    EXPECT_EQ(effect.transform.angles.x, 65);
    EXPECT_FLOAT_EQ(effect.transform.position.x, 6398.9f);
    EXPECT_LT(effect.transform.position.z, 995);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(KalimaModelEffectTest, ImpactWritesTheNativePlaneOnceBeforeTheSecondMovement)
{
    auto effects = std::make_unique<Client::World::ModelEffects>();
    auto water = std::make_unique<Client::World::KalimaWorld>(24);
    Client::Render::Models::CrtRandom random(7);
    ASSERT_EQ(effects->CreateKalimaEffect({6400, 6400, 351}, {}, {0.3f, 0.8f, 1}, random), 0u);
    auto& effect = effects->Effects()[0];
    Client::World::Particles particles;
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, water.get()).HasValue());
    EXPECT_EQ(effect.disturbanceState, 1);
    EXPECT_EQ(water->Waves().planes[0][126 * 256 + 126], 0xffffffffu);
    EXPECT_EQ(water->Waves().planes[1][126 * 256 + 126], 0u);
    EXPECT_EQ(random.State(), 0xc40e66b9u); // radio positivo no sortea centros.
    const auto snapshot = water->CloneForFrame();
    ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, water.get()).HasValue());
    EXPECT_EQ(water->Waves().planes, snapshot->Waves().planes);
    EXPECT_EQ(random.State(), 0xbb6f8e98u);
    for (int tick = 2; tick < 30; ++tick)
        ASSERT_TRUE(Client::World::StepGameModelEffect(effect, nullptr, particles, 0, random, water.get()).HasValue());
    EXPECT_FALSE(effect.alive);
    EXPECT_EQ(effect.life, 0);
}
