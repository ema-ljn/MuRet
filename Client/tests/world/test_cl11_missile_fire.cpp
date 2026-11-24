#include "World/MissileFireParticleKernels.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"

#include "support/ParticleDrawRecorder.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>

// El fuego 0x5da de la CL11 (MissileFireParticleKernels): oráculos escritos desde el ASM de 0x53fe70 y 0x5446e0, con
// los rand() contados contra Client::Tests::AfterDraws.
namespace
{
using Client::Crt::CrtRandom;
using Client::Render::Models::Detail::CosineProductPC53;
using Client::Render::Models::Detail::SineProductPC53;
using Client::Tests::AfterDraws;
using Client::World::Particle;
using Client::World::ParticleError;
using Client::World::Particles;
using Client::World::PortableParticleTrigonometry;

constexpr std::uint32_t Seed = 0x1234;
constexpr std::int32_t Fire = 0x5da;

using Client::Math::D;
using Client::Math::F32;

double D(std::uint32_t bits)
{
    return static_cast<double>(std::bit_cast<float>(bits));
}

struct Draws
{
    CrtRandom random{Seed};
    int Next()
    {
        return random.Next();
    }
};

Particle& Only(Particles& particles)
{
    return particles.All()[0];
}

Client::Core::Expected<void, ParticleError> Step(Particles& particles, CrtRandom& random)
{
    PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    return Client::World::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry);
}

Particle& Create(Particles& particles, CrtRandom& random, std::int32_t subtype, const glm::vec3& position = {},
                 const glm::vec3& angles = {}, float scale = 1.0f)
{
    const auto created = particles.CreateChecked(Fire, position, angles, glm::vec3(1.0f), subtype, scale, 0.0f, random);
    EXPECT_TRUE(created.HasValue()) << subtype;
    return Only(particles);
}

// 0x541045, 0x540d1b, 0x540d5c y 0x540d9d: la /0..3 viven 12, 3, 5 y 7 y salen con la velocidad (0; ((rand() & 7) +
// 32) · −0,3f o −0,1f; 0) (la /0 con la escala 0,8f); después el giro rand() % 360 de 0x54107f.
TEST(Cl11MissileFire, TheTrailsPushBackAndTurn)
{
    struct Case
    {
        std::int32_t subtype;
        std::int32_t life;
        std::uint32_t step;
    };
    for (const Case& value :
         {Case{0, 12, 0xbe99999au}, Case{1, 3, 0xbe99999au}, Case{2, 5, 0xbdcccccdu}, Case{3, 7, 0xbdcccccdu}})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        Particle& fire = Create(*particles, random, value.subtype);
        Draws draws;
        EXPECT_EQ(fire.life, value.life) << value.subtype;
        EXPECT_EQ(fire.velocity,
                  glm::vec3(0.0f, F32(static_cast<double>((draws.Next() & 7) + 32) * D(value.step)), 0.0f))
            << value.subtype;
        EXPECT_EQ(fire.rotation, static_cast<float>(draws.Next() % 360)) << value.subtype;
        EXPECT_EQ(fire.scale, value.subtype == 0 ? std::bit_cast<float>(0x3f4ccccdu) : 1.0f) << value.subtype;
        EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State()) << value.subtype;
    }
}

// 0x5456a6: la /0 y la /3 suben 0,02f, se agrandan con la subida, aceleran 1,05f y la z suma la subida por 20;
// 0x54556d, la /1 sin la subida; 0x5456ea, la /2 sólo la luz vida · 0,2f.
TEST(Cl11MissileFire, TheTrailStepsClimbWithTheirLift)
{
    for (const std::int32_t subtype : {0, 1, 2, 3})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        Particle& fire = Create(*particles, random, subtype);
        fire.life = 3;
        fire.verticalSpeed = 0.5f;
        fire.scale = 1.0f;
        fire.velocity = glm::vec3(0.0f);
        fire.position = glm::vec3(0.0f, 0.0f, 10.0f);
        ASSERT_TRUE(Step(*particles, random).HasValue()) << subtype;
        EXPECT_EQ(fire.light, glm::vec3(F32(2.0 * D(0x3e4ccccdu)))) << subtype;
        const double lift = 0.5 + D(0x3ca3d70au);
        if (subtype == 0 || subtype == 3)
        {
            EXPECT_EQ(fire.verticalSpeed, F32(lift)) << subtype;
            EXPECT_EQ(fire.scale, F32(lift + 1.0)) << subtype;
            EXPECT_EQ(fire.position.z, F32(lift * 20.0 + 10.0)) << subtype;
        }
        else
        {
            EXPECT_EQ(fire.verticalSpeed, 0.5f) << subtype;
            EXPECT_EQ(fire.position.z, subtype == 1 ? 20.0f : 10.0f) << subtype;
        }
    }
}

// 0x540dcf: la /4 vive 100, sube (rand() % 100 · 0,01f) · 4 + 1, toma la fase rand() % 300 − 150 en la velocidad x, la
// escala rand() % 6 · 0,15f más la del pedido, los ángulos en 0 y la partida; cinco rand(). 0x5455b0: la vuelta de
// radio 120 alrededor de la partida con a = (vida + fase) · 0,1f, la z sube y la escala baja 0,002f.
TEST(Cl11MissileFire, TheOrbitCirclesItsBirthplace)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    Particle& fire = Create(*particles, random, 4, {100.0f, 200.0f, 300.0f}, {10.0f, 20.0f, 30.0f}, 2.0f);
    Draws draws;
    EXPECT_EQ(fire.life, 100);
    EXPECT_EQ(fire.verticalSpeed, F32((static_cast<double>(draws.Next() % 100) * D(0x3c23d70au)) * 4.0 + 1.0));
    const float phase = static_cast<float>(draws.Next() % 300 - 150);
    EXPECT_EQ(fire.velocity.x, phase);
    EXPECT_EQ(fire.scale, F32(static_cast<double>(draws.Next() % 6) * D(0x3e19999au) + 2.0));
    static_cast<void>(draws.Next());
    EXPECT_EQ(fire.angles, glm::vec3(0.0f));
    EXPECT_EQ(fire.start, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(fire.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 5).State());

    fire.velocity = glm::vec3(phase, 0.0f, 0.0f);
    fire.position = fire.start;
    const float lift = fire.verticalSpeed;
    const float scale = fire.scale;
    ASSERT_TRUE(Step(*particles, random).HasValue());
    // FSIN y FCOS retenidos por el 120 del radio (0x6b86c8) con un solo R53.
    const double angle = (99.0 + D(phase)) * D(0x3dcccccdu);
    const auto sine = SineProductPC53(angle, 120.0f);
    const auto cosine = CosineProductPC53(angle, 120.0f);
    ASSERT_TRUE(sine.has_value());
    ASSERT_TRUE(cosine.has_value());
    EXPECT_EQ(fire.position.x, F32(*sine + 100.0));
    EXPECT_EQ(fire.position.y, F32(200.0 - *cosine));
    EXPECT_EQ(fire.position.z, F32(D(lift) + 300.0));
    EXPECT_EQ(fire.scale, F32(D(scale) - D(0x3b03126fu)));
}

// 0x540e74 (/5): vida 6, la subida más el ángulo x · 1,2f, la escala · 0,2f; 0x5455f9 avanza cos y sen del ángulo z
// crudo por 20 y gira 1. 0x540ee7 (/6): la subida más 5 y la escala · 0,1f; 0x54563a sube y gira 4. 0x540f51 (/7):
// vida 10; 0x545659 acelera y se apaga.
TEST(Cl11MissileFire, TheSwirlTheRiseAndTheFade)
{
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        Particle& fire = Create(*particles, random, 5, {0, 0, 0}, {10.0f, 0.0f, 2.0f}, 1.0f);
        Draws draws;
        EXPECT_EQ(fire.life, 6);
        EXPECT_EQ(fire.verticalSpeed,
                  F32((static_cast<double>(draws.Next() % 100) * D(0x3c23d70au)) * 4.0 + 10.0 * D(0x3f99999au)));
        EXPECT_EQ(fire.scale, F32(static_cast<double>(draws.Next() % 6) * D(0x3e4ccccdu) + 1.0));
        EXPECT_EQ(random.State(), AfterDraws(Seed, 4).State());
        fire.position = glm::vec3(0.0f);
        fire.verticalSpeed = 1.0f;
        const float turn = fire.rotation;
        ASSERT_TRUE(Step(*particles, random).HasValue());
        const auto cosine = CosineProductPC53(2.0, 20.0f);
        const auto sine = SineProductPC53(2.0, 20.0f);
        ASSERT_TRUE(cosine.has_value());
        ASSERT_TRUE(sine.has_value());
        EXPECT_EQ(fire.position, glm::vec3(F32(*cosine + 0.0), F32(*sine + 0.0), 1.0f));
        EXPECT_EQ(fire.rotation, F32(D(turn) + 1.0));
    }
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        Particle& fire = Create(*particles, random, 6);
        Draws draws;
        EXPECT_EQ(fire.life, 6);
        EXPECT_EQ(fire.verticalSpeed, F32((static_cast<double>(draws.Next() % 100) * D(0x3c23d70au)) * 4.0 + 5.0));
        EXPECT_EQ(fire.scale, F32(static_cast<double>(draws.Next() % 6) * D(0x3dcccccdu) + 1.0));
        const float turn = fire.rotation;
        const float lift = fire.verticalSpeed;
        ASSERT_TRUE(Step(*particles, random).HasValue());
        EXPECT_EQ(fire.position.z, lift);
        EXPECT_EQ(fire.rotation, F32(D(turn) + 4.0));
    }
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        Particle& fire = Create(*particles, random, 7);
        EXPECT_EQ(fire.life, 10);
        EXPECT_EQ(random.State(), AfterDraws(Seed, 1).State());
        fire.velocity = glm::vec3(0.0f, 2.0f, 0.0f);
        ASSERT_TRUE(Step(*particles, random).HasValue());
        EXPECT_EQ(fire.velocity, glm::vec3(0.0f, F32(2.0 * D(0x3f866666u)), 0.0f));
        EXPECT_EQ(fire.light, glm::vec3(F32(9.0 * D(0x3e4ccccdu))));
    }
}

// Los subtipos fuera de 0..9 sólo sacan el giro de 0x54107f y su paso no hace nada; todos van al billboard común.
TEST(Cl11MissileFire, TheOthersOnlyTurnAndAllDraw)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    Particle& fire = Create(*particles, random, 12);
    Draws draws;
    EXPECT_EQ(fire.life, 2);
    EXPECT_EQ(fire.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 1).State());
    for (const std::int32_t subtype : {0, 1, 2, 3, 4, 5, 6, 7, 12})
    {
        Particle particle{
            .alive = true, .type = Fire, .texture = Fire, .subtype = subtype, .scale = 1.0f, .light = glm::vec3(1.0f)};
        const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(64, 64), std::uint8_t{4});
        ASSERT_TRUE(drawn.HasValue()) << subtype;
        EXPECT_TRUE(drawn.Value().has_value()) << subtype;
    }
}
} // namespace
