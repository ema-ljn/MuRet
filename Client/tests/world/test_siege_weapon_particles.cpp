#include "Particles/Particles.h"

#include "Crt/CrtRandom.h"
#include "Particles/BloodCastleParticleKernels.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>

namespace
{
TEST(SiegeWeaponParticlesTest, TheSmallImpactFlameKeepsAllSixConstructorDraws)
{
    Client::World::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto created =
        particles.CreateChecked(Client::World::TorchFlameParticle, {}, {}, {1, 1, 1}, 1, 2.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    auto& particle = particles.All()[*created.Value()];
    EXPECT_EQ(particle.life, 24);
    EXPECT_EQ(particle.scale, 1.0f);
    EXPECT_EQ(particle.rotation, 107.0f);
    EXPECT_EQ(particle.velocity, (glm::vec3{1.6f, 0.0f, static_cast<float>(9.0 * static_cast<double>(0.2f))}));
    Client::Crt::CrtRandom expected(1);
    expected.Discard(6);
    EXPECT_EQ(random.State(), expected.State());
    const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2{32, 32}, std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
}

TEST(SiegeWeaponParticlesTest, TheFlightFireUsesTheInputScaleAndItsRotatingAtlas)
{
    constexpr std::int32_t FlightFire = 5;
    Client::World::Particles particles;
    Client::Crt::CrtRandom random(1);
    const auto created =
        particles.CreateChecked(Client::World::FireParticle, {}, {}, {1, 2, 3}, FlightFire, 2.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    auto& particle = particles.All()[*created.Value()];
    const float scale = static_cast<float>((163.0 * 2.0) * static_cast<double>(0.01f));
    const float velocity = static_cast<float>(41.0 * static_cast<double>(-0.1f));
    EXPECT_EQ(particle.life, 24);
    EXPECT_EQ(particle.scale, scale);
    EXPECT_EQ(particle.rotation, 214.0f);
    EXPECT_EQ(particle.velocity, (glm::vec3{0, velocity, 0}));
    ASSERT_TRUE(Client::World::StepParticlesChecked(particles, random).HasValue());
    EXPECT_EQ(particle.life, 23);
    EXPECT_EQ(particle.scale, static_cast<float>(static_cast<double>(scale) - static_cast<double>(0.04f)));
    EXPECT_EQ(particle.rotation, 219.0f);
    EXPECT_EQ(particle.velocity.y, velocity);
    EXPECT_EQ(particle.light, (glm::vec3{1, 2, 3}));
    EXPECT_EQ(std::bit_cast<std::int32_t>(particle.field48), 0);
    Client::Crt::CrtRandom expected(1);
    expected.Discard(5); // 3alta y2viento, sinazarpropioen545842.
    EXPECT_EQ(random.State(), expected.State());
    const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2{128, 32}, std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->rotation, 219.0f);
    EXPECT_EQ(drawn.Value()->uvSize, (glm::vec2{0.25f, 1.0f}));
}
} // namespace
