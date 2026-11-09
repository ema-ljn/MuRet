#include "World/Particles.h"

#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>

// Expectativas derivadas del ASM de 0x53fe70 y 0x5446e0 para 0x622/10. No son vectores
// obtenidos ejecutando el original. Ver 06 § Los efectos del login, neblina.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Tests::DrawOneParticle;
using Client::Tests::RecordedParticleDraw;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t LoginCloudType = 0x622;
constexpr std::int32_t LoginCloudSubtype = 10;

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

Particle& Cloud(Particles& particles)
{
    Particle& particle = particles.All()[0];
    particle.alive = true;
    particle.type = LoginCloudType;
    particle.texture = LoginCloudType;
    particle.subtype = LoginCloudSubtype;
    particle.life = 500;
    return particle;
}

TEST(LoginCloudParticlesTest, CreationUsesFiveRandomValuesAndKeepsThePerturbedOrigin)
{
    Particles particles;
    CrtRandom random(7);
    const auto index = particles.Create(LoginCloudType, {1000.0f, 2000.0f, 300.0f}, {10.0f, 20.0f, 30.0f},
                                        {1.0f, 2.0f, 3.0f}, LoginCloudSubtype, 0.75f, 1234.0f, random);
    ASSERT_EQ(index, 0u);
    const Particle& particle = particles.All()[*index];
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(particle.type, LoginCloudType);
    EXPECT_EQ(particle.texture, LoginCloudType);
    EXPECT_EQ(particle.subtype, LoginCloudSubtype);
    EXPECT_EQ(particle.life, 500);
    EXPECT_EQ(particle.light, glm::vec3(0.0f));
    EXPECT_EQ(particle.angles, glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_TRUE(particle.moves);
    EXPECT_EQ(particle.rotation, 0.0f);
    EXPECT_EQ(particle.field48, 0.0f);
    EXPECT_EQ(particle.verticalSpeed, 0.0f);
    EXPECT_EQ(particle.owner, 0u);
    EXPECT_EQ(particle.start, particle.position);
    // CRT semilla 7: 61, 17422, 15215, 7040, 15521. Los cinco restos son parte del contrato.
    EXPECT_EQ(particle.position, glm::vec3(861.0f, 2022.0f, 115.0f));
    EXPECT_EQ(Bits(particle.scale), 0x3fe00000u);
    EXPECT_EQ(Bits(particle.velocity.x), 0x3f3851ebu);
    EXPECT_EQ(Bits(particle.velocity.y), 0x3f3851ebu);
    EXPECT_EQ(particle.velocity.z, 0.0f);
    EXPECT_EQ(random.State(), 0xbca151d2u);
}

TEST(LoginCloudParticlesTest, FirstFreeSlotWinsAndAFullPoolConsumesNoRandomValues)
{
    Particles particles;
    for (Particle& particle : particles.All())
    {
        particle.alive = true;
    }
    particles.All()[17].alive = false;
    particles.All()[29].alive = false;
    CrtRandom random(7);
    EXPECT_EQ(particles.Create(LoginCloudType, {}, {}, {}, LoginCloudSubtype, 1.0f, 0.0f, random), 17u);
    EXPECT_EQ(particles.Create(LoginCloudType, {}, {}, {}, LoginCloudSubtype, 1.0f, 0.0f, random), 29u);
    const std::uint32_t before = random.State();
    EXPECT_FALSE(particles.Create(LoginCloudType, {}, {}, {}, LoginCloudSubtype, 1.0f, 0.0f, random).has_value());
    EXPECT_EQ(random.State(), before);
    EXPECT_FALSE(particles.Create(LoginCloudType, {}, {}, {}, 9, 1.0f, 0.0f, random).has_value());
    EXPECT_EQ(random.State(), before);
}

TEST(LoginCloudParticlesTest, TheCreationScaleMultipliesTheStoredConstantInsteadOfDividingBy15)
{
    Particles particles;
    CrtRandom random(1);
    const auto index = particles.Create(LoginCloudType, {}, {}, {}, LoginCloudSubtype, 0.1f, 0.0f, random);
    ASSERT_TRUE(index.has_value());
    // El cuarto rand es 26500: (20 * floatBits(0x3d888889)) + 0,1f.
    EXPECT_EQ(Bits(particles.All()[*index].scale), 0x3fb77778u);
}

TEST(LoginCloudParticlesTest, DrawUsesTheTextureDimensionsAndPreservesTheOriginalColor)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.texture = 123;
    particle.scale = 1.75f;
    particle.position = {10.0f, 20.0f, 30.0f};
    particle.light = {0.1f, 0.2f, 0.3f};
    const auto drawn = DrawOneParticle(particle, glm::vec2(256.0f, 128.0f), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, 123);
    EXPECT_EQ(drawn.Value()->position, particle.position);
    EXPECT_EQ(drawn.Value()->width, 448.0f);
    EXPECT_EQ(drawn.Value()->height, 224.0f);
    EXPECT_EQ(drawn.Value()->light, particle.light);
    EXPECT_EQ(drawn.Value()->uv, glm::vec2(0.0f));
    EXPECT_EQ(drawn.Value()->uvSize, glm::vec2(1.0f));
    EXPECT_EQ(drawn.Value()->blend, RecordedParticleDraw::Blend::Subtractive);
}

TEST(LoginCloudParticlesTest, CreationPreservesTheRotationFactorUntilTheFirstStep)
{
    Particles particles;
    particles.All()[0].rotationFactor = -7.25f;
    CrtRandom random(7);
    const auto index = particles.Create(LoginCloudType, {}, {}, {}, LoginCloudSubtype, 1.0f, 0.0f, random);
    ASSERT_EQ(index, 0u);
    Particle& particle = particles.All()[*index];
    EXPECT_EQ(particle.rotationFactor, -7.25f);
    const auto drawn = DrawOneParticle(particle, glm::vec2(256.0f), std::uint8_t{3}, 100.0f, *index);
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    const auto expected = static_cast<float>(100.0 * -7.25 * static_cast<double>(0.02f) + particle.start.y);
    EXPECT_EQ(Bits(drawn.Value()->rotation), Bits(expected));
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(particle.rotationFactor, 1.5f);
}

TEST(LoginCloudParticlesTest, DrawingUsesSlotParityTheRetainedFactorAndTheOriginY)
{
    for (const std::size_t index : {0u, 1u, 2999u})
    {
        Particles particles;
        Particle& particle = Cloud(particles);
        particle.start.y = 2022.0f;
        particle.rotationFactor = 1.5f;
        particle.rotation = 99.0f;
        // El índice es el lugar real del pool: la paridad sale de ahí, no de un argumento aparte.
        const auto drawn = DrawOneParticle(particle, glm::vec2(256.0f), std::uint8_t{4}, 1234.5f, index);
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->blend, RecordedParticleDraw::Blend::Subtractive);
        EXPECT_EQ(Bits(drawn.Value()->rotation), index % 2 == 0 ? 0x4500b08fu : 0x44f81ee1u);
        EXPECT_EQ(Bits(particle.rotation), Bits(drawn.Value()->rotation));
        const auto again = DrawOneParticle(particle, glm::vec2(256.0f), std::uint8_t{3}, 1234.5f, index);
        ASSERT_TRUE(again.HasValue());
        ASSERT_TRUE(again.Value().has_value());
        EXPECT_EQ(Bits(again.Value()->rotation), Bits(drawn.Value()->rotation));
    }
}

TEST(LoginCloudParticlesTest, ADeadCloudIsNotDrawnAndDoesNotChangeItsRotation)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.alive = false;
    particle.rotation = 99.0f;
    particle.rotationFactor = 1.5f;
    const auto drawn = DrawOneParticle(particle, glm::vec2(256.0f), std::uint8_t{3}, 1234.5f, 0);
    ASSERT_TRUE(drawn.HasValue());
    EXPECT_FALSE(drawn.Value().has_value());
    EXPECT_EQ(particle.rotation, 99.0f);
}

TEST(LoginCloudParticlesTest, DrawingRoundsTheRotationOnlyWhenStoringTheResult)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.start.y = 12.5f;
    particle.rotationFactor = 1.5f;
    const auto drawn = DrawOneParticle(particle, glm::vec2(256.0f), std::uint8_t{3}, 12345678.0f, 0);
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(Bits(drawn.Value()->rotation), 0x48b4d9dbu);
}

TEST(LoginCloudParticlesTest, UpdateAddsRotatedAndUnrotatedVelocity)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.position = {1000.0f, 2000.0f, 300.0f};
    particle.start = particle.position;
    particle.velocity = {3.0f, -2.0f, 0.5f};
    particle.angles = {10.0f, 20.0f, 30.0f};
    particle.rotation = 99.0f;
    particle.scale = 1.75f;
    particle.moves = true;
    const glm::vec3 turned = Client::Render::Models::OriginalVectorRotate(
        particle.velocity, Client::Render::Models::OriginalAngleMatrix(particle.angles));
    const glm::vec3 before = particle.position;
    CrtRandom random(7);
    Client::World::StepParticles(particles, random);
    for (int axis = 0; axis < 3; ++axis)
    {
        const auto common = static_cast<float>(static_cast<double>(before[axis]) + turned[axis]);
        EXPECT_EQ(Bits(particle.position[axis]),
                  Bits(static_cast<float>(static_cast<double>(common) + particle.velocity[axis])));
    }
    EXPECT_EQ(particle.life, 499);
    EXPECT_EQ(particle.angles, glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(particle.velocity, glm::vec3(3.0f, -2.0f, 0.5f));
    EXPECT_EQ(particle.rotation, 99.0f);
    EXPECT_EQ(particle.scale, 1.75f);
    CrtRandom expected(7);
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
}

TEST(LoginCloudParticlesTest, DisabledCommonMoveStillAddsTheUnrotatedVelocity)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.position = {1.0f, 2.0f, 3.0f};
    particle.start = particle.position;
    particle.velocity = {4.0f, 5.0f, 6.0f};
    particle.angles = {90.0f, 90.0f, 90.0f};
    CrtRandom random;
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(particle.position, glm::vec3(5.0f, 7.0f, 9.0f));
}

TEST(LoginCloudParticlesTest, Radius300GrowsAndTheRedThresholdSetsAllChannels)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.start = {40.0f, 50.0f, 60.0f};
    particle.position = {40.0f, 50.0f, 360.0f};
    particle.light = {0.145f, 0.8f, -1.0f};
    CrtRandom random;
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(Bits(particle.light.x), 0x3e19999au);
    EXPECT_EQ(Bits(particle.light.y), 0x3e19999au);
    EXPECT_EQ(Bits(particle.light.z), 0x3e19999au);
    EXPECT_TRUE(particle.alive);
}

TEST(LoginCloudParticlesTest, GrowthDoesNotClampOtherChannelsWhenRedIsBelowTheThreshold)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.light = {0.0f, 0.8f, -1.0f};
    CrtRandom random;
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(Bits(particle.light.x), 0x3c23d70au);
    EXPECT_EQ(Bits(particle.light.y), Bits(static_cast<float>(static_cast<double>(0.8f) + 0.01f)));
    EXPECT_EQ(Bits(particle.light.z), Bits(static_cast<float>(-1.0 + 0.01f)));
}

TEST(LoginCloudParticlesTest, TheRedComparisonUsesTheUnspilledSum)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.light = {0.14f, 0.8f, -1.0f};
    CrtRandom random;
    Client::World::StepParticles(particles, random);
    // 0,14f + 0,01f queda debajo de 0,15f en double, aunque guardado en float sea 0,15f.
    EXPECT_EQ(Bits(particle.light.x), 0x3e19999au);
    EXPECT_GT(particle.light.y, 0.8f);
    EXPECT_LT(particle.light.z, 0.0f);
}

TEST(LoginCloudParticlesTest, TheXDistanceRetainsTheSumButTheYDistanceUsesTheStoredFloat)
{
    for (int axis = 0; axis < 2; ++axis)
    {
        Particles particles;
        Particle& particle = Cloud(particles);
        particle.position[axis] = 300.0f;
        particle.velocity[axis] = 0.000001f;
        particle.light = glm::vec3(0.1f);
        CrtRandom random;
        Client::World::StepParticles(particles, random);
        EXPECT_EQ(particle.position[axis], 300.0f);
        EXPECT_EQ(Bits(particle.light.x), axis == 0 ? 0x3dc30c32u : 0x3de147aeu);
    }
}

TEST(LoginCloudParticlesTest, TheDistanceIsSphericalAndIncludesAllThreeAxes)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.position = {300.0f, 300.0f, 300.0f};
    CrtRandom random;
    Client::World::StepParticles(particles, random);
    EXPECT_FALSE(particle.alive);
}

TEST(LoginCloudParticlesTest, JustOver300AndExactly500FadeByTheStoredConstant)
{
    for (const float radius : {std::bit_cast<float>(0x43960001u), 500.0f})
    {
        Particles particles;
        Particle& particle = Cloud(particles);
        particle.position.z = radius;
        particle.light = {0.1f, 0.2f, 0.3f};
        CrtRandom random;
        Client::World::StepParticles(particles, random);
        EXPECT_TRUE(particle.alive);
        EXPECT_EQ(Bits(particle.light.x), 0x3dc30c32u);
        EXPECT_EQ(Bits(particle.light.y), 0x3e430c32u);
        EXPECT_EQ(Bits(particle.light.z), 0x3e924926u);
    }
}

TEST(LoginCloudParticlesTest, JustOver500DiesWithoutFadingOrWrapping)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.position = {0.0f, 0.0f, std::bit_cast<float>(0x43fa0001u)};
    particle.light = {0.1f, 0.2f, 0.3f};
    const glm::vec3 before = particle.position;
    CrtRandom random;
    Client::World::StepParticles(particles, random);
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.position, before);
    EXPECT_EQ(particle.light, glm::vec3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(particle.rotationFactor, 1.5f);
}

TEST(LoginCloudParticlesTest, LifetimeExpiryStillRunsTheLastUpdateAndInactiveSlotsDoNothing)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.life = 1;
    particle.velocity.x = 1.0f;
    CrtRandom random;
    Client::World::StepParticles(particles, random);
    EXPECT_FALSE(particle.alive);
    EXPECT_EQ(particle.life, 0);
    EXPECT_EQ(particle.position.x, 1.0f);
    EXPECT_EQ(Bits(particle.light.x), 0x3c23d70au);
    Client::World::StepParticles(particles, random);
    EXPECT_EQ(particle.position.x, 1.0f);
    EXPECT_EQ(particle.life, 0);
}

TEST(LoginCloudParticlesTest, AnUnorderedDistanceTakesTheGrowthBranch)
{
    Particles particles;
    Particle& particle = Cloud(particles);
    particle.position.x = std::numeric_limits<float>::quiet_NaN();
    CrtRandom random;
    Client::World::StepParticles(particles, random);
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(Bits(particle.light.x), 0x3c23d70au);
}
} // namespace
