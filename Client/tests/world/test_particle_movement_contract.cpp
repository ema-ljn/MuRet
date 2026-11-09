#include "World/Particles.h"

#include "Render/Models/Cloth.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

// Reconciliación de 4e98d0 con su binding P=S+14,A=S+20,V=S+54 en 5446e0.
// Sólo proyección finita acreditada de OriginalRotation; no ABI/alias nativos universales.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;

TEST(ParticleMovementContractTest, TheRotatedTemporaryIsAddedToTheThreePositionComponents)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = Client::World::SparkParticle,
                        .subtype = Client::World::RavenSparkSubtype,
                        .scale = 1,
                        .position = {100, 200, 300},
                        .angles = {0, 90, 0},
                        .light = {1, 1, 1},
                        .field38 = 0xdeadbeefu,
                        .life = 10,
                        .owner = 0xfedcba98u,
                        .rotation = 95,
                        .field48 = std::bit_cast<float>(-5),
                        .moves = true,
                        .velocity = {1, 2, 3},
                        .rotationFactor = -7,
                        .field64 = {31, 32},
                        .start = {7, 8, 9}};
    CrtRandom random(7);
    const auto step = Client::World::StepParticlesChecked(particles, random);
    ASSERT_TRUE(step.HasValue());
    // Matriz de 90° acreditada en rotation.txt: sinY=1, cosY=bits32510b46.
    // R=(3,2,-0.99999994), y cada suma independiente se almacena como float.
    EXPECT_EQ(particle.position, glm::vec3(103, 202, 299));
    EXPECT_EQ(particle.angles, glm::vec3(0, 90, 0));
    EXPECT_EQ(particle.velocity, glm::vec3(1, 2, 3));
    EXPECT_EQ(particle.field38, 0xdeadbeefu);
    EXPECT_EQ(particle.owner, 0xfedcba98u);
    EXPECT_EQ(particle.rotation, 95);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.field48), 0xfffffffbu);
    EXPECT_EQ(particle.rotationFactor, -7);
    EXPECT_EQ(particle.field64, glm::vec2(31, 32));
    EXPECT_EQ(particle.start, glm::vec3(7, 8, 9));
    EXPECT_EQ(particle.life, 9);            // Del prefijo caller, no del helper.
    EXPECT_EQ(random.State(), 0xc40e66b9u); // Sólo viento del caller.
}

TEST(ParticleMovementContractTest, AZeroMovementFlagSkipsTheHelperEvenWithUnorderedAngles)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = Client::World::SparkParticle,
                        .subtype = Client::World::RavenSparkSubtype,
                        .position = {100, 200, 300},
                        .angles = glm::vec3(std::numeric_limits<float>::quiet_NaN()),
                        .life = 10,
                        .velocity = {1, 2, 3}};
    CrtRandom random(7);
    const auto step = Client::World::StepParticlesChecked(particles, random);
    ASSERT_TRUE(step.HasValue());
    EXPECT_EQ(particle.position, glm::vec3(100, 200, 300));
    EXPECT_EQ(random.State(), 0xc40e66b9u);
}

TEST(ParticleMovementContractTest, ThePositionAdditionDoesNotIntroduceAFiniteValueGuard)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = Client::World::SparkParticle,
                        .subtype = Client::World::RavenSparkSubtype,
                        .position = {std::numeric_limits<float>::quiet_NaN(), 20, 30},
                        .life = 10,
                        .moves = true,
                        .velocity = {1, 2, 3}};
    CrtRandom random(7);
    const auto step = Client::World::StepParticlesChecked(particles, random);
    ASSERT_TRUE(step.HasValue());
    EXPECT_TRUE(std::isnan(particle.position.x));
    EXPECT_EQ(particle.position.y, 22);
    EXPECT_EQ(particle.position.z, 33);
}
} // namespace
