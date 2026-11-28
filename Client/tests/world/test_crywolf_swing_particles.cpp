#include "Crt/CrtRandom.h"
#include "Particles/Particles.h"
#include "support/ParticleDrawRecorder.h"
#include "support/PlayerActors.h"
#include <glm/ext/vector_float2.hpp>
#include <gtest/gtest.h>
#include <cstdint>
#include <memory>

namespace
{
TEST(CrywolfSwingParticlesTest, TheSwordGlowKeepsItsThreeDrawsAndItsOwnTexture)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
    const auto made = particles->CreateChecked(0x640, {100, 200, 300}, {}, {1, 2, 3}, 0, 1, 0, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), 0u);
    auto& p = particles->All()[0];
    EXPECT_EQ(p.life, 30);
    EXPECT_EQ(p.texture, 0x640);
    EXPECT_EQ(p.position, (glm::vec3{100, 200, 300}));
    EXPECT_EQ(p.scale, 1.25f);
    EXPECT_EQ(p.rotation, 142);
    EXPECT_FLOAT_EQ(p.verticalSpeed, 7.5f);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 3).State());
    ASSERT_TRUE(Client::World::StepParticlesChecked(*particles, random).HasValue());
    EXPECT_EQ(p.life, 29);
    EXPECT_FLOAT_EQ(p.scale, 1.3f);
    EXPECT_EQ(p.position.z, 300);
    EXPECT_EQ(p.light, glm::vec3(1.45f));
    const auto drawn = Client::Tests::DrawOneParticle(p, glm::vec2(32), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value());
    EXPECT_EQ(drawn.Value()->texture, 0x640);
}
TEST(CrywolfSwingParticlesTest, TheMagicSparkDiscardsItsFirstLifeRollAndKeepsTheSecondYRoll)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
    const auto made = particles->CreateChecked(0x5ad, {100, 200, 300}, {}, {1, 2, 3}, 2, 0.8f, 0, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), 0u);
    auto& p = particles->All()[0];
    EXPECT_EQ(p.life, 19);
    EXPECT_EQ(p.position, (glm::vec3{100, 192, 300}));
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 2).State());
    ASSERT_TRUE(Client::World::StepParticlesChecked(*particles, random).HasValue());
    EXPECT_EQ(p.life, 18);
    EXPECT_FLOAT_EQ(p.scale, 0.736f);
    EXPECT_EQ(p.light, (glm::vec3{1, 2, 3}));
    const auto drawn = Client::Tests::DrawOneParticle(p, glm::vec2(32), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value());
    EXPECT_EQ(drawn.Value()->texture, 0x5ad);
}
TEST(CrywolfSwingParticlesTest, TheHandMistUsesChrome03AndFadesBeforeShrinking)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
    const auto made = particles->CreateChecked(0x5f2, {100, 200, 300}, {}, {0.2f, 0.2f, 0.7f}, 27, 1, 0, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), 0u);
    auto& p = particles->All()[0];
    EXPECT_EQ(p.life, 30);
    EXPECT_EQ(p.texture, 0x6b1);
    EXPECT_EQ(p.rotation, 61);
    EXPECT_EQ(random.State(), Client::Tests::AfterDraws(7, 1).State());
    ASSERT_TRUE(Client::World::StepParticlesChecked(*particles, random).HasValue());
    EXPECT_EQ(p.life, 29);
    EXPECT_FLOAT_EQ(p.scale, 0.95f);
    EXPECT_EQ(p.position, (glm::vec3{100, 200, 300}));
    EXPECT_FLOAT_EQ(p.light.r, 0.184f);
    EXPECT_FLOAT_EQ(p.light.b, 0.644f);
    const auto drawn = Client::Tests::DrawOneParticle(p, glm::vec2(32), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value());
    EXPECT_EQ(drawn.Value()->texture, 0x6b1);
}
} // namespace
