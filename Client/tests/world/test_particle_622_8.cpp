#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float2.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t CloudType = 0x622;
constexpr std::int32_t CloudSubtype = 8;

TEST(Particle622Subtype8Test, AscendingPhaseAddsTheBinary64ConstantAndRaisesZ)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = CloudType,
                        .subtype = CloudSubtype,
                        .position = {0, 0, 4},
                        .field38 = std::bit_cast<std::uint32_t>(0.5f),
                        .life = 152};
    CrtRandom random(7);

    const auto stepped = Client::World::StepParticlesChecked(particles, random);

    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(particle.life, 151);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(std::bit_cast<float>(particle.field38)), 0x3f0a3d71u);
    EXPECT_EQ(particle.position.z, 5.0f);
}

TEST(Particle622Subtype8Test, DescendingPhaseSubtractsOnlyAboveTheStrictFloatThreshold)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = CloudType,
                        .subtype = CloudSubtype,
                        .position = {0, 0, 4},
                        .field38 = std::bit_cast<std::uint32_t>(0.2f),
                        .life = 151};
    CrtRandom random(7);

    const auto stepped = Client::World::StepParticlesChecked(particles, random);

    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(particle.life, 150);
    EXPECT_EQ(std::bit_cast<float>(particle.field38), 0.17499999701976776f);
    EXPECT_EQ(particle.position.z, 3.5f);
}

TEST(Particle622Subtype8Test, EqualityAtEitherThresholdLeavesTheRawScaleWordUntouched)
{
    for (const std::int32_t initialLife : {152, 151})
    {
        Particles particles;
        Particle& particle = particles.All()[0];
        const float q = initialLife == 152 ? 1.0f : std::bit_cast<float>(0x3dcccccdu);
        particle = Particle{.alive = true,
                            .type = CloudType,
                            .subtype = CloudSubtype,
                            .position = {0, 0, 4},
                            .field38 = std::bit_cast<std::uint32_t>(q),
                            .life = initialLife};
        CrtRandom random(7);
        const std::uint32_t originalWord = particle.field38;

        const auto stepped = Client::World::StepParticlesChecked(particles, random);

        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(particle.field38, originalWord);
        EXPECT_EQ(particle.position.z, initialLife == 152 ? 5.0f : 3.5f);
    }
}

TEST(Particle622Subtype8Test, ExpirationSkipsTheSubtypePhaseButStillRunsTheCommonPrefix)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = CloudType,
                        .subtype = CloudSubtype,
                        .position = {0, 0, 4},
                        .field38 = 0x3f000000u,
                        .life = 1};
    CrtRandom random(7);

    const auto stepped = Client::World::StepParticlesChecked(particles, random);

    ASSERT_TRUE(stepped.HasValue());
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(particle.field38, 0x3f000000u);
    EXPECT_EQ(particle.position.z, 4.0f);
}

TEST(Particle622Subtype8Test, UnorderedScaleTakesTheNativeEarlyComparisonButNotTheLateOne)
{
    for (const std::int32_t initialLife : {152, 151})
    {
        Particles particles;
        Particle& particle = particles.All()[0];
        particle = Particle{.alive = true,
                            .type = CloudType,
                            .subtype = CloudSubtype,
                            .position = {0, 0, 4},
                            .field38 = 0x7fc12345u,
                            .life = initialLife};
        CrtRandom random(7);

        const auto stepped = Client::World::StepParticlesChecked(particles, random);

        ASSERT_TRUE(stepped.HasValue());
        EXPECT_TRUE(std::isnan(std::bit_cast<float>(particle.field38)));
        EXPECT_EQ(particle.position.z, initialLife == 152 ? 5.0f : 3.5f);
        if (initialLife == 151)
        {
            EXPECT_EQ(particle.field38, 0x7fc12345u);
        }
    }
}

TEST(Particle622Subtype8Test, CreationAndDrawUseTheirNewExplicitBindings)
{
    Particles particles;
    CrtRandom random(7);
    const auto created = particles.CreateChecked(CloudType, {}, {}, {}, CloudSubtype, 0.4f, 0, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    EXPECT_EQ(random.State(), 0x75c8bedcu);

    const auto draw =
        Client::World::ParticleDrawChecked(particles.All()[*created.Value()], glm::vec2(64), std::uint8_t{3});
    ASSERT_TRUE(draw.HasValue());
    EXPECT_TRUE(draw.Value().has_value());
}
} // namespace
