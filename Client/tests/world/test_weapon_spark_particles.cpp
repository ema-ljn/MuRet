#include "World/WeaponSparkKernels.h"

#include "Render/Models/Cloth.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>

// La chispa 0x5c6/0xb del arco 0xa27 y del bastón 0xc1e: 0x540977 al crearla y 0x545368 en cada paso. Los
// esperados repiten las fórmulas del ASM con otro CrtRandom de la misma semilla.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;
constexpr std::int32_t Spark = 0x5c6;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(float value)
{
    return static_cast<double>(value);
}

std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}

// 0x540977: +0x38 en 1,0f; la velocidad ((rand() & 7) − 4,0f; rand() & 3; rand() & 3) con la vida 30 en el medio,
// y la escala (rand() % 5) · 0,1f + 1, que pisa la del argumento. El prefijo deja la posición, los ángulos y la luz.
TEST(WeaponSparkParticlesTest, TheCreationDrawsFourRandomsAndReplacesTheScale)
{
    for (const std::uint32_t seed : {1u, 7u, 319u})
    {
        SCOPED_TRACE(seed);
        Particles particles;
        CrtRandom random(seed);
        const auto created =
            particles.CreateChecked(Spark, {100.0f, 200.0f, 300.0f}, {0.0f, 0.0f, 45.0f}, {0.8f, 0.8f, 0.2f},
                                    Client::World::WeaponSparkSubtype, 0.8f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_EQ(created.Value(), 0u);
        CrtRandom draws(seed);
        const float x = F32(static_cast<double>(draws.Next() & 7) - 4.0);
        const auto y = static_cast<float>(draws.Next() & 3);
        const auto z = static_cast<float>(draws.Next() & 3);
        const float scale = F32(static_cast<double>(draws.Next() % 5) * D(0.1f) + 1.0);
        EXPECT_EQ(random.State(), draws.State());
        const Particle& particle = particles.All()[0];
        EXPECT_EQ(particle.texture, Spark);
        EXPECT_EQ(particle.life, 30);
        EXPECT_EQ(particle.field38, Bits(1.0f));
        EXPECT_EQ(particle.velocity, glm::vec3(x, y, z));
        EXPECT_EQ(particle.scale, scale);
        EXPECT_EQ(particle.position, glm::vec3(100.0f, 200.0f, 300.0f));
        EXPECT_EQ(particle.start, glm::vec3(100.0f, 200.0f, 300.0f));
        EXPECT_EQ(particle.angles, glm::vec3(0.0f, 0.0f, 45.0f));
        EXPECT_EQ(particle.light, glm::vec3(0.8f, 0.8f, 0.2f));
        EXPECT_EQ(particle.verticalSpeed, 0.0f);
        EXPECT_TRUE(particle.moves);
    }
}

// 0x545368, después de la vida y del movimiento comunes: la luz por 0,952381f, la escala −0,04f y +0x38 −0,001f.
TEST(WeaponSparkParticlesTest, EachStepMovesThenFadesShrinksAndCountsDown)
{
    Particles particles;
    Particle& particle = particles.All()[0];
    particle = Particle{.alive = true,
                        .type = Spark,
                        .subtype = Client::World::WeaponSparkSubtype,
                        .scale = 1.3f,
                        .position = {100.0f, 200.0f, 300.0f},
                        .light = {0.8f, 0.8f, 0.2f},
                        .field38 = Bits(1.0f),
                        .life = 30,
                        .moves = true,
                        .velocity = {-3.0f, 2.0f, 1.0f}};
    CrtRandom random(7);
    ASSERT_FALSE(Client::World::StepParticles(particles, random).has_value());
    const double fade = D(std::bit_cast<float>(0x3f73cf3eu));
    EXPECT_TRUE(particle.alive);
    EXPECT_EQ(particle.life, 29);
    EXPECT_EQ(particle.position, glm::vec3(97.0f, 202.0f, 301.0f));
    EXPECT_EQ(particle.light, glm::vec3(F32(D(0.8f) * fade), F32(D(0.8f) * fade), F32(D(0.2f) * fade)));
    EXPECT_EQ(particle.scale, F32(D(1.3f) - D(0.04f)));
    EXPECT_EQ(particle.field38, Bits(F32(1.0 - D(0.001f))));
    EXPECT_EQ(particle.velocity, glm::vec3(-3.0f, 2.0f, 1.0f));
}

// La cuenta de +0x38 mata con la resta sin redondear en 0 o por debajo, y sin orden; no con un resto positivo.
TEST(WeaponSparkParticlesTest, TheCountdownKillsAtZeroAndBelowButNotAbove)
{
    struct Case
    {
        float left;
        bool alive;
    };
    for (const Case value : {Case{0.002f, true}, Case{0.001f, false}, Case{0.0005f, false},
                             Case{std::numeric_limits<float>::quiet_NaN(), false}})
    {
        SCOPED_TRACE(value.left);
        Particle particle{.alive = true,
                          .type = Spark,
                          .subtype = Client::World::WeaponSparkSubtype,
                          .field38 = Bits(value.left),
                          .life = 20};
        Client::World::StepWeaponSpark(particle);
        EXPECT_EQ(particle.alive, value.alive);
    }
}
} // namespace
