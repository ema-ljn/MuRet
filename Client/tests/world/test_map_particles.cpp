#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>

// Oráculos del contrato de extensión 53fe70/5446e0/548550: 607/0, sin ejecutar el PE.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t Drop = 0x607;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

TEST(MapParticlesTest, DropCreationUsesTwoRandomValuesAndReplacesOnlyItsColorScaleAndLife)
{
    Particles particles;
    particles.All()[0].rotationFactor = -7;
    CrtRandom random(7); // R1=61 -> vida31; R2=17422 -> (4+4)*K0.03.
    const auto slot = particles.Create(Drop, {100, 200, 300}, {10, 20, 30}, {2, 3, 4}, 0, 9, 999, random);
    ASSERT_EQ(slot, 0u);
    const Particle& particle = particles.All()[0];
    EXPECT_EQ(particle.life, 31);
    EXPECT_EQ(Bits(particle.scale), 0x3e75c28fu);
    EXPECT_EQ(particle.position, glm::vec3(100, 200, 300));
    EXPECT_EQ(particle.start, particle.position);
    EXPECT_EQ(particle.angles, glm::vec3(10, 20, 30));
    EXPECT_EQ(particle.light, glm::vec3(1));
    EXPECT_EQ(particle.rotation, 0);
    EXPECT_EQ(Bits(particle.field48), 0u);
    EXPECT_EQ(particle.verticalSpeed, 0);
    EXPECT_EQ(particle.velocity, glm::vec3(0));
    EXPECT_EQ(particle.rotationFactor, -7);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(MapParticlesTest, ExpiringDropStillIncrementsItsFrameAndConsumesThreeOrderedAxisDraws)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true, .type = Drop, .subtype = 0, .scale = 0.25f, .life = 1};
    CrtRandom random(7); // Viento:61,17422; ejes:15215,7040,15521 -> 5,-10,11.
    Client::World::StepParticles(particles, random);
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(Bits(particle.field48), 1u);
    EXPECT_EQ(particle.position, glm::vec3(3.125f, -6.25f, 6.875f));
    EXPECT_EQ(particle.scale, 0.25f);
    EXPECT_EQ(random.State(), 0xbca151d2u);
}

TEST(MapParticlesTest, DropFrameIncrementWrapsTheRawWord)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle =
        Particle{.alive = true, .type = Drop, .subtype = 0, .life = 2, .field48 = std::bit_cast<float>(0x7fffffffu)};
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(Bits(particle.field48), 0x80000000u);
    particle.field48 = std::bit_cast<float>(0xffffffffu);
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(Bits(particle.field48), 0u);
}

TEST(MapParticlesTest, DropMovesBeforeItsRandomWalkAndKeepsTheThreeAxisScalesUnclamped)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = Drop,
                        .subtype = 0,
                        .scale = -0.25f,
                        .position = {100, 200, 300},
                        .life = 2,
                        .moves = true,
                        .velocity = {4, 5, 6}};
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(particle.position, glm::vec3(100.875f, 211.25f, 299.125f));
    EXPECT_EQ(particle.velocity, glm::vec3(4, 5, 6));
    EXPECT_EQ(particle.scale, -0.25f);
}

TEST(MapParticlesTest, DropAtlasKeepsSignedRemaindersAndQuarterStepsWithTheNativeInset)
{
    struct Case
    {
        std::int32_t frame;
        std::uint32_t u;
        std::uint32_t v;
    };
    constexpr std::array cases{Case{0, 0x3ba3d70au, 0x3ba3d70au},
                               Case{1, 0x3e828f5cu, 0x3ba3d70au},
                               Case{2, 0x3f0147aeu, 0x3ba3d70au},
                               Case{3, 0x3ba3d70au, 0x3e828f5cu},
                               Case{4, 0x3e828f5cu, 0x3e828f5cu},
                               Case{5, 0x3f0147aeu, 0x3e828f5cu},
                               Case{6, 0x3ba3d70au, 0x3f0147aeu},
                               Case{7, 0x3e828f5cu, 0x3f0147aeu},
                               Case{8, 0x3f0147aeu, 0x3f0147aeu},
                               Case{-1, 0xbe7ae148u, 0x3ba3d70au},
                               Case{-8, 0xbefd70a4u, 0xbefd70a4u},
                               Case{-9, 0x3ba3d70au, 0x3ba3d70au},
                               Case{std::numeric_limits<std::int32_t>::min(), 0xbefd70a4u, 0x3ba3d70au},
                               Case{std::numeric_limits<std::int32_t>::max(), 0x3e828f5cu, 0x3ba3d70au}};
    for (const Case& value : cases)
    {
        Particle particle{.alive = true,
                          .type = Drop,
                          .texture = 17,
                          .subtype = 0,
                          .scale = 1.5f,
                          .angles = {17, 22, 33},
                          .rotation = 95,
                          .field48 = std::bit_cast<float>(value.frame)};
        const auto draw = Client::World::ParticleDrawOf(particle, {64, 32}, 3);
        ASSERT_TRUE(draw.has_value());
        EXPECT_EQ(draw->texture, 17);
        EXPECT_EQ(draw->width, 96);
        EXPECT_EQ(draw->height, 48);
        EXPECT_EQ(draw->rotation, 0);
        EXPECT_EQ(Bits(draw->uv.x), value.u) << value.frame;
        EXPECT_EQ(Bits(draw->uv.y), value.v) << value.frame;
        EXPECT_EQ(Bits(draw->uvSize.x), 0x3e75c28fu);
        EXPECT_EQ(Bits(draw->uvSize.y), 0x3e75c28fu);
        EXPECT_EQ(draw->blend, Client::World::ParticleDraw::Blend::Additive);
    }
}
} // namespace
