#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstdint>
#include <limits>

// Contratos extendidos 53fe70/5446e0/548550, 5c6/9. Matrices reutilizadas por permiso root;
// el caso de90grados usa el coseno32510b46 ya acreditado en rotation.txt, no el port como oráculo.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t Spark = 0x5c6;
constexpr std::int32_t MapSpark = 9;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

TEST(MapSparkParticlesTest, CreationOffsetsByTheRotatedOldVectorBeforeReplacingItWithTwelveZeroMinusTwo)
{
    Particles particles;
    CrtRandom random(319); // R1=1080 -> yaw0; R2=28589 -> z169.
    const auto slot = particles.Create(Spark, {100, 200, 300}, {10, 20, 30}, {2, 3, 4}, MapSpark, 1.4f, 0, random);
    ASSERT_EQ(slot, 0u);
    const Particle& particle = particles.All()[0];
    EXPECT_EQ(particle.life, 30);
    EXPECT_EQ(particle.angles, glm::vec3(0));
    EXPECT_EQ(particle.position, glm::vec3(100, 200, 469));
    EXPECT_EQ(particle.start, glm::vec3(100, 200, 300));
    EXPECT_EQ(particle.velocity, glm::vec3(12, 0, -2));
    EXPECT_EQ(Bits(particle.scale), 0x3fb33333u);
    EXPECT_EQ(particle.light, glm::vec3(2, 3, 4));
    EXPECT_EQ(particle.rotation, 0);
    EXPECT_EQ(random.State(), 0xefad31b1u);
}

TEST(MapSparkParticlesTest, CreationRotatesAroundYAndDoesNotMultiplyTheOffsetByScale)
{
    Particles particles;
    CrtRandom random(16); // R1=90, R2=15854 -> z164.
    const auto slot = particles.Create(Spark, {}, {}, {}, MapSpark, 1.4f, 0, random);
    ASSERT_EQ(slot, 0u);
    const Particle& particle = particles.All()[0];
    EXPECT_EQ(particle.angles, glm::vec3(0, 90, 0));
    EXPECT_EQ(particle.position.x, 164);
    EXPECT_EQ(particle.position.y, 0);
    EXPECT_EQ(Bits(particle.position.z), 0x3605eb39u);
    EXPECT_EQ(particle.velocity, glm::vec3(12, 0, -2));
    EXPECT_EQ(random.State(), 0xbdee0f0au);
}

TEST(MapSparkParticlesTest, StepMovesWithTheOldVelocityThenDeceleratesAndShrinks)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = Spark,
                        .subtype = MapSpark,
                        .scale = 1.4f,
                        .position = {100, 200, 300},
                        .light = {2, 3, 4},
                        .life = 30,
                        .moves = true,
                        .velocity = {12, 5, -2}};
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(particle.position, glm::vec3(112, 205, 298));
    EXPECT_EQ(Bits(particle.velocity.x), 0x412ccccdu);
    EXPECT_EQ(particle.velocity.y, 5);
    EXPECT_EQ(particle.velocity.z, -3);
    EXPECT_EQ(Bits(particle.scale), 0x3fa8f5c2u);
    EXPECT_EQ(particle.light, glm::vec3(2, 3, 4));
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(MapSparkParticlesTest, ExpiryStillRunsBothVelocityStoresAndTheScaleStore)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle =
        Particle{.alive = true, .type = Spark, .subtype = MapSpark, .scale = 1.4f, .life = 1, .velocity = {12, 5, -2}};
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(Bits(particle.velocity.x), 0x412ccccdu);
    EXPECT_EQ(particle.velocity.y, 5);
    EXPECT_EQ(particle.velocity.z, -3);
    EXPECT_EQ(Bits(particle.scale), 0x3fa8f5c2u);
}

TEST(MapSparkParticlesTest, TheRetainedScaleComparisonKillsUnorderedAndBelowButKeepsEquality)
{
    struct Case
    {
        float scale;
        bool alive;
    };
    const std::array cases{
        Case{std::bit_cast<float>(0x3e8f5c29u), true}, Case{std::bit_cast<float>(0x3e8f5c28u), false},
        Case{std::numeric_limits<float>::quiet_NaN(), false}, Case{std::numeric_limits<float>::infinity(), true},
        Case{-std::numeric_limits<float>::infinity(), false}};
    for (const Case& value : cases)
    {
        Particles particles;
        Particle& particle = particles.All()[0];
        particle = Particle{.alive = true, .type = Spark, .subtype = MapSpark, .scale = value.scale, .life = 30};
        CrtRandom random(7);
        Client::World::StepParticles(particles, random);
        EXPECT_EQ(particle.alive, value.alive) << value.scale;
        EXPECT_EQ(random.State(), 0xc40e66b9u);
    }
}

TEST(MapSparkParticlesTest, DrawUsesTheWholeTextureAndRotation44WithoutAtlasOrAdditionalBlend)
{
    Particle particle{.alive = true,
                      .type = Spark,
                      .texture = 17,
                      .subtype = MapSpark,
                      .scale = 1.4f,
                      .angles = {17, 22, 33},
                      .rotation = 95,
                      .field48 = std::bit_cast<float>(8)};
    const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(64, 32), std::uint8_t{4});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    const Client::Tests::RecordedParticleDraw& draw = *drawn.Value();
    EXPECT_EQ(draw.texture, 17);
    EXPECT_EQ(Bits(draw.width), 0x42b33333u);
    EXPECT_EQ(Bits(draw.height), 0x42333333u);
    EXPECT_EQ(draw.rotation, 95);
    EXPECT_EQ(draw.uv, glm::vec2(0));
    EXPECT_EQ(draw.uvSize, glm::vec2(1));
    EXPECT_EQ(draw.blend, Client::Tests::RecordedParticleDraw::Blend::Alpha);
}
} // namespace
