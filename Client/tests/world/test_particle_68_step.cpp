#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <initializer_list>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t EffectType = 0x68;
constexpr std::uint32_t MapBlue = 0x3f333333u;

TEST(Particle68StepTest, Map34SetsBlueFirstAndThenRedAndGreenAfterScaleGrowth)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{
        .alive = true, .type = EffectType, .subtype = 0, .scale = 1.0f, .light = {2, 3, 4}, .life = 2, .moves = false};
    const std::int32_t map = 34;
    CrtRandom random(7);

    const auto stepped = Client::World::StepParticlesChecked(particles, random, &map);

    ASSERT_TRUE(stepped.HasValue());
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(particle.life, 1);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0x3f83d70au);
    EXPECT_EQ(particle.light.x, 1.0f);
    EXPECT_EQ(particle.light.y, 1.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.light.z), MapBlue);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(Particle68StepTest, OtherRawMapsSubtractEachColorWithoutClamping)
{
    for (const std::int32_t map : {0, 35, 0x10000022, -1})
    {
        Particles particles;
        Particle& particle = particles.All()[0];
        particle = Particle{.alive = true,
                            .type = EffectType,
                            .subtype = 0,
                            .scale = 1.0f,
                            .light = {0, -1, 1},
                            .life = 2,
                            .moves = false};
        CrtRandom random(7);

        const auto stepped = Client::World::StepParticlesChecked(particles, random, &map);

        ASSERT_TRUE(stepped.HasValue()) << map;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.light.x), 0xbd4ccccdu) << map;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.light.y), 0xbf866666u) << map;
        EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.light.z), 0x3f733333u) << map;
        EXPECT_EQ(random.State(), 0xc40e66b9u) << map;
    }
}

TEST(Particle68StepTest, ExpirationStillMovesGrowsQueriesAndChangesColor)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = EffectType,
                        .subtype = 0,
                        .scale = 1.0f,
                        .position = {1, 2, 3},
                        .light = {2, 3, 4},
                        .life = 1,
                        .moves = true,
                        .velocity = {4, 5, 6}};
    const std::int32_t map = 34;
    CrtRandom random(7);

    const auto stepped = Client::World::StepParticlesChecked(particles, random, &map);

    ASSERT_TRUE(stepped.HasValue());
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(particle.position, glm::vec3(5, 7, 9));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0x3f83d70au);
    EXPECT_EQ(particle.light, glm::vec3(1.0f, 1.0f, std::bit_cast<float>(MapBlue)));
}

TEST(Particle68StepTest, MissingMapReportsAfterLifeAndScaleStores)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{
        .alive = true, .type = EffectType, .subtype = 0, .scale = 1.0f, .light = {2, 3, 4}, .life = 1, .moves = false};
    CrtRandom random(7);

    const auto stepped = Client::World::StepParticlesChecked(particles, random);

    ASSERT_FALSE(stepped.HasValue());
    EXPECT_EQ(stepped.Error(), Client::World::ParticleError::MissingMap);
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.scale), 0x3f83d70au);
    EXPECT_EQ(particle.light, glm::vec3(2, 3, 4));
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(Particle68StepTest, APostHelperSubtypeOneUsesTheStoredAccumulator)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = EffectType,
                        .subtype = 1,
                        .scale = 1.0f,
                        .life = 2,
                        .moves = false,
                        .verticalSpeed = 0.25f};
    const std::int32_t map = 34;
    CrtRandom random(7);

    const auto stepped = Client::World::StepParticlesChecked(particles, random, &map);

    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(particle.scale, 1.25f);
    EXPECT_EQ(particle.verticalSpeed, 0.25f);
}
} // namespace
