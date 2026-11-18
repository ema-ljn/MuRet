#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cstdint>

namespace
{
constexpr std::int32_t Blessing = 0x638;

TEST(SiegeBlessingParticleTest, CreationAndStepPreserveTheNativeJitterAndRandomOrder)
{
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(1);
    const auto created = particles.CreateChecked(Blessing, {100, 200, 300}, {}, {1, 2, 3}, 0, 1.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    auto& particle = particles.All()[*created.Value()];
    EXPECT_EQ(particle.life, 20);
    EXPECT_EQ(particle.position, (glm::vec3{96, 202, 540}));
    EXPECT_EQ(particle.light, (glm::vec3{1, 2, 3}));
    ASSERT_TRUE(Client::World::StepParticlesChecked(particles, random).HasValue());
    EXPECT_EQ(particle.life, 19);
    EXPECT_EQ(particle.position, (glm::vec3{96, 201, 542}));
    EXPECT_EQ(particle.scale, static_cast<float>(1.0 - static_cast<double>(0.01f)));
    EXPECT_EQ(particle.light, glm::vec3(static_cast<float>(19.0 * static_cast<double>(0.05f))));
    Client::Render::Models::CrtRandom expected(1);
    expected.Discard(6); // 2alta,2viento,2pasodelapartícula.
    EXPECT_EQ(random.State(), expected.State());
}

TEST(SiegeBlessingParticleTest, TheFinalTickStillRunsItsJitterAfterDeath)
{
    Client::World::Particles particles;
    auto& particle = particles.All().front();
    particle.alive = true;
    particle.type = Blessing;
    particle.life = 1;
    particle.scale = 1.0f;
    Client::Render::Models::CrtRandom random(1);
    ASSERT_TRUE(Client::World::StepParticlesChecked(particles, random).HasValue());
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(particle.position, (glm::vec3{-1, -1, 2}));
    EXPECT_EQ(particle.light, glm::vec3(0.0f));
    Client::Render::Models::CrtRandom expected(1);
    expected.Discard(4);
    EXPECT_EQ(random.State(), expected.State());
}
TEST(SiegeBlessingParticleTest, TheBlessingDrawsThroughTheCommonBillboardBranch)
{
    Client::World::Particle particle;
    particle.alive = true;
    particle.type = particle.texture = Blessing;
    particle.scale = 2.0f;
    particle.position = {100, 200, 300};
    particle.light = {0.3f, 0.4f, 0.5f};
    const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2{64, 32}, std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, Blessing);
    EXPECT_EQ(drawn.Value()->position, particle.position);
    EXPECT_EQ(drawn.Value()->light, particle.light);
    EXPECT_EQ(drawn.Value()->width, 128.0f);
    EXPECT_EQ(drawn.Value()->height, 64.0f);
}
} // namespace
