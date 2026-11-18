#include "World/Particles.h"
#include "Render/Models/Cloth.h"
#include "support/ParticleDrawRecorder.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>

namespace
{
TEST(CrywolfMissileParticlesTest, TheMissileFireKeepsItsSixDrawsAndFinalRotation)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(7);
    const auto made = particles->CreateChecked(0x5da, {100, 200, 300}, {}, {1, 2, 3}, 8, 0.6f, 0, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), 0u);
    auto& p = particles->All()[0];
    EXPECT_EQ(p.life, 32);
    EXPECT_EQ(p.position, (glm::vec3{91, 192, 305}));
    EXPECT_EQ(p.rotation, 36);
    EXPECT_FLOAT_EQ(p.verticalSpeed, 1.2f);
    EXPECT_EQ(random.State(), 0x9974714du);
    p.life = 1;
    p.velocity = {1, 2, 3};
    ASSERT_TRUE(Client::World::StepParticlesChecked(*particles, random).HasValue());
    EXPECT_FALSE(p.alive);
    EXPECT_EQ(p.position, (glm::vec3{92, 194, 308}));
    EXPECT_FLOAT_EQ(p.rotation, 37.2f);
    EXPECT_FLOAT_EQ(p.scale, 0.73f);
    EXPECT_FLOAT_EQ(p.light.r, 0.8849557638168335f);
    p.alive = true;
    const auto drawn = Client::Tests::DrawOneParticle(p, glm::vec2(32), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value());
    EXPECT_EQ(drawn.Value()->texture, 0x5da);
}

TEST(CrywolfMissileParticlesTest, TheSecondFireKeepsItsThreeDrawsAndItsAlternateTexture)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(7);
    const auto made = particles->CreateChecked(0x5da, {100, 200, 300}, {}, {1, 2, 3}, 9, 0.6f, 0, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), 0u);
    const auto& p = particles->All()[0];
    EXPECT_EQ(p.life, 32);
    EXPECT_EQ(p.position, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(p.texture, 0x5ae);
    EXPECT_EQ(p.rotation, 95);
    EXPECT_FLOAT_EQ(p.verticalSpeed, 0.7f);
    EXPECT_EQ(random.State(), 0xbb6f8e98u);
}

TEST(CrywolfMissileParticlesTest, TheMissileSmokeUsesTheWaterfallTextureAndFadesWithoutOwnerReads)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Render::Models::CrtRandom random(7);
    const auto made = particles->CreateChecked(0x5f2, {100, 200, 300}, {}, {1, 2, 3}, 38, 1.7f, 0, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), 0u);
    auto& p = particles->All()[0];
    EXPECT_EQ(p.life, 35);
    EXPECT_EQ(p.texture, 0x6f1);
    EXPECT_EQ(p.rotation, 61);
    EXPECT_EQ(random.State(), 0x003d7aaeu);
    p.life = 1;
    ASSERT_TRUE(Client::World::StepParticlesChecked(*particles, random).HasValue());
    EXPECT_FALSE(p.alive);
    EXPECT_FLOAT_EQ(p.scale, 1.79f);
    EXPECT_FLOAT_EQ(p.light.r, 0.9615384936332703f);
    p.alive = true;
    const auto drawn = Client::Tests::DrawOneParticle(p, glm::vec2(32), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value());
    EXPECT_EQ(drawn.Value()->texture, 0x6f1);
}
} // namespace
