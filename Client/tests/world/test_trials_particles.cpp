#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/TrialsParticleKernels.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

// Las partículas de los objetos de Land of Trials: la salpicadura 6f2/3 del 3 y el humo 5f2/22 del 0x35
// (06 § Los objetos de Land of Trials).
namespace
{
using Client::Render::Models::CrtRandom;
using Client::World::Particle;
using Client::World::Particles;

constexpr std::int32_t Splash = 0x6f2;
constexpr std::int32_t Smoke = 0x5f2;
constexpr int Scene = 5;

double Bits(std::uint32_t bits)
{
    return static_cast<double>(std::bit_cast<float>(bits));
}
} // namespace

// 0x544319..0x5443a6: vida 60, velocidad z −1, cuatro rand(): la escala (r % 10 + 15) · escala · 0,02f, el
// giro, la x con r % 40 − 20 y la z con r % 25 − 10. La y queda.
TEST(TrialsParticlesTest, TheSplashIsCreatedWithFourDrawsAroundItsObject)
{
    for (const std::int32_t type : {Splash, Splash + 1})
    {
        SCOPED_TRACE(type);
        Particles particles;
        CrtRandom random(7);
        const auto made = particles.CreateOwnedChecked(type, {100.5f, 200.25f, 300.75f}, {0, 0, 30}, {1, 1, 1}, 3,
                                                       1.25f, 99, random, Scene, {});
        ASSERT_TRUE(made.HasValue());
        ASSERT_EQ(made.Value(), std::optional<std::size_t>(0));
        const Particle& p = particles.All()[0];
        CrtRandom draws(7);
        const int size = draws.Next() % 10 + 15;
        const int turn = draws.Next() % 360;
        const int dx = draws.Next() % 40;
        const int dz = draws.Next() % 25;
        EXPECT_EQ(p.life, 60);
        EXPECT_EQ(p.velocity, glm::vec3(0.0f, 0.0f, -1.0f));
        EXPECT_EQ(p.scale, static_cast<float>((static_cast<double>(size) * 1.25) * Bits(0x3ca3d70au)));
        EXPECT_EQ(p.rotation, static_cast<float>(turn));
        EXPECT_EQ(p.position.x, static_cast<float>((dx - 20.0) + 100.5));
        EXPECT_EQ(p.position.y, 200.25f);
        EXPECT_EQ(p.position.z, static_cast<float>((dz - 10.0) + 300.75));
        EXPECT_EQ(p.start, glm::vec3(100.5f, 200.25f, 300.75f));
        EXPECT_EQ(p.light, glm::vec3(1.0f));
        EXPECT_TRUE(p.moves);
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x5423e0..0x542448: vida 60, tres rand(): la escala ((r & 63) + 64) · escala · 0,005f, el giro y la
// velocidad vertical ((r & 31) + 60) · 0,1f.
TEST(TrialsParticlesTest, TheSmokeIsCreatedWithThreeDrawsAndItsRise)
{
    Particles particles;
    CrtRandom random(11);
    const auto made =
        particles.CreateOwnedChecked(Smoke, {10, 20, 30}, {0, 0, 0}, {1, 1, 1}, 22, 0.8f, 99, random, Scene, {});
    ASSERT_TRUE(made.HasValue());
    const Particle& p = particles.All()[0];
    CrtRandom draws(11);
    const int size = (draws.Next() & 63) + 64;
    const int turn = draws.Next() % 360;
    const int rise = (draws.Next() & 31) + 60;
    EXPECT_EQ(p.life, 60);
    EXPECT_EQ(p.scale, static_cast<float>((static_cast<double>(size) * static_cast<double>(0.8f)) * Bits(0x3ba3d70au)));
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.verticalSpeed, static_cast<float>(static_cast<double>(rise) * Bits(0x3dcccccdu)));
    EXPECT_EQ(p.velocity, glm::vec3(0.0f));
    EXPECT_EQ(random.State(), draws.State());
}

// Otros subtipos de esos tipos no son de esta familia.
TEST(TrialsParticlesTest, OnlyTheTwoObjectSubtypesBelongToTheFamily)
{
    EXPECT_TRUE(Client::World::IsTrialsParticle(0x6f2, 3));
    EXPECT_TRUE(Client::World::IsTrialsParticle(0x6f3, 3));
    EXPECT_TRUE(Client::World::IsTrialsParticle(0x5f2, 22));
    EXPECT_FALSE(Client::World::IsTrialsParticle(0x6f2, 4));
    EXPECT_FALSE(Client::World::IsTrialsParticle(0x6f4, 3));
    EXPECT_FALSE(Client::World::IsTrialsParticle(0x5f2, 3));
    EXPECT_FALSE(Client::World::IsTrialsParticle(0x5f3, 22));
}

// 0x54815d..0x54819e: la escala +0,005f, la velocidad z −0,05f y la luz por 0,97f, sin rand().
TEST(TrialsParticlesTest, TheSplashGrowsFallsFasterAndFades)
{
    Particle p;
    p.alive = true;
    p.type = p.texture = Splash;
    p.subtype = 3;
    p.scale = 0.5f;
    p.velocity = {0.0f, 0.0f, -1.0f};
    p.light = {1.0f, 0.75f, 0.5f};
    ASSERT_TRUE(Client::World::StepTrialsParticle(p, nullptr).HasValue());
    const double fade = Bits(0x3f7851ecu);
    EXPECT_EQ(p.scale, static_cast<float>(0.5 + Bits(0x3ba3d70au)));
    EXPECT_EQ(p.velocity.z, static_cast<float>(-1.0 - Bits(0x3d4ccccdu)));
    EXPECT_EQ(p.light,
              glm::vec3(static_cast<float>(fade), static_cast<float>(0.75 * fade), static_cast<float>(0.5 * fade)));
}

// 0x546706..0x546757: la luz de la vida (rojo por 0,9f, verde y azul por 0,5); la velocidad vertical −0,1f
// guardada en float y usada sin redondear en el vaivén de la x (sin(reloj) por ella por 0,05f) y en la z;
// la escala +0,04f.
TEST(TrialsParticlesTest, TheSmokeRisesSlowerSwaysWithTheClockAndDims)
{
    Particle p;
    p.alive = true;
    p.type = p.texture = Smoke;
    p.subtype = 22;
    p.life = 37;
    p.scale = 0.6f;
    p.verticalSpeed = 7.3f;
    p.position = {100.0f, 200.0f, 300.0f};
    const float clock = 123456.0f;
    ASSERT_TRUE(Client::World::StepTrialsParticle(p, &clock).HasValue());
    const double light = 37.0 * Bits(0x3ca3d70au);
    const double rise = static_cast<double>(7.3f) - Bits(0x3dcccccdu);
    const auto sway = Client::Render::Models::Detail::SineDoubleProductPC53(static_cast<double>(clock), rise);
    ASSERT_TRUE(sway.has_value());
    EXPECT_EQ(p.light, glm::vec3(static_cast<float>(light * Bits(0x3f666666u)), static_cast<float>(light * 0.5),
                                 static_cast<float>(light * 0.5)));
    EXPECT_EQ(p.verticalSpeed, static_cast<float>(rise));
    EXPECT_EQ(p.position.x, static_cast<float>((*sway * Bits(0x3d4ccccdu)) + 100.0));
    EXPECT_EQ(p.position.y, 200.0f);
    EXPECT_EQ(p.position.z, static_cast<float>(rise + 300.0));
    EXPECT_EQ(p.scale, static_cast<float>(static_cast<double>(0.6f) + Bits(0x3d23d70au)));

    // Sin el reloj del mundo, corta después de la luz y de la velocidad, sin tocar la posición.
    Particle q = p;
    const auto cut = Client::World::StepTrialsParticle(q, nullptr);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_EQ(cut.Error(), Client::World::ParticleError::MissingClock);
    EXPECT_EQ(q.position, p.position);
    EXPECT_NE(q.verticalSpeed, p.verticalSpeed);
}

// FSIN y FMUL ST1 por un double: con un factor float da lo mismo que el producto por float, con el signo
// del factor, y el cero conserva su signo.
TEST(TrialsParticlesTest, TheSineByADoubleMatchesTheFloatProductAndKeepsTheSign)
{
    for (const double argument : {0.5, 1.0e3, 123456.0, -77.25})
    {
        SCOPED_TRACE(argument);
        const auto byFloat = Client::Render::Models::Detail::SineProductPC53(argument, 0.3f);
        const auto byDouble =
            Client::Render::Models::Detail::SineDoubleProductPC53(argument, static_cast<double>(0.3f));
        const auto negative =
            Client::Render::Models::Detail::SineDoubleProductPC53(argument, -static_cast<double>(0.3f));
        ASSERT_TRUE(byFloat && byDouble && negative);
        EXPECT_EQ(*byFloat, *byDouble);
        EXPECT_EQ(*negative, -*byDouble);
    }
    const auto zero = Client::Render::Models::Detail::SineDoubleProductPC53(0.0, -2.0);
    ASSERT_TRUE(zero.has_value());
    EXPECT_EQ(*zero, 0.0);
    EXPECT_TRUE(std::signbit(*zero));
    EXPECT_FALSE(Client::Render::Models::Detail::SineDoubleProductPC53(1.0, std::nan("")).has_value());
}

// Por el pool: las dos viven 60 pasos con la vida común y se mueven con la velocidad girada por los ángulos.
TEST(TrialsParticlesTest, BothLiveSixtyStepsInThePool)
{
    Particles particles;
    CrtRandom random(7);
    ASSERT_TRUE(particles.CreateOwnedChecked(Splash, {0, 0, 500}, {0, 0, 0}, {1, 1, 1}, 3, 1.0f, 99, random, Scene, {})
                    .HasValue());
    ASSERT_TRUE(particles.CreateOwnedChecked(Smoke, {0, 0, 500}, {0, 0, 0}, {1, 1, 1}, 22, 1.0f, 99, random, Scene, {})
                    .HasValue());
    const float splashZ = particles.All()[0].position.z;
    float clock = 1000.0f;
    Client::World::PortableParticleTrigonometry trigonometry;
    int steps = 0;
    while ((particles.All()[0].alive || particles.All()[1].alive) && steps < 100)
    {
        ASSERT_TRUE(
            Client::World::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trigonometry).HasValue());
        clock += 40.0f;
        ++steps;
    }
    EXPECT_EQ(steps, 60);
    // La salpicadura cae: −1 el primer paso, después más rápido.
    EXPECT_LT(particles.All()[0].position.z, splashZ - 60.0f);
    EXPECT_GT(particles.All()[1].position.z, 500.0f);
}
