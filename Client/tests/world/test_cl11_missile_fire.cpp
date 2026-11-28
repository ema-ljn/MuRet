#include "Particles/MissileFireParticleKernels.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"

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

// The fire 0x5da (MissileFireParticleKernels): oracles written from the ASM of 0x53fe70 and 0x5446e0, with the rand()
// counted against Client::Tests::AfterDraws.
namespace
{
using Client::Crt::CrtRandom;
using Client::Math::Detail::CosineProductPC53;
using Client::Math::Detail::SineProductPC53;
using Client::Particles::Particle;
using Client::Particles::ParticleError;
using Client::Particles::Particles;
using Client::Particles::PortableParticleTrigonometry;
using Client::Tests::AfterDraws;

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
    return Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry);
}

Particle& Create(Particles& particles, CrtRandom& random, std::int32_t subtype, const glm::vec3& position = {},
                 const glm::vec3& angles = {}, float scale = 1.0f)
{
    const auto created = particles.CreateChecked(Fire, position, angles, glm::vec3(1.0f), subtype, scale, 0.0f, random);
    EXPECT_TRUE(created.HasValue()) << subtype;
    return Only(particles);
}

// 0x541045, 0x540d1b, 0x540d5c and 0x540d9d: /0..3 live 12, 3, 5 and 7 and leave with velocity (0; ((rand() & 7) + 32)
// · −0.3f or −0.1f; 0) (/0 with scale 0.8f); then 0x54107f's turn rand() % 360.
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

// 0x5456a6: /0 and /3 rise 0.02f, grow with the rise, accelerate 1.05f and the z adds the rise times 20; 0x54556d, /1
// without the rise; 0x5456ea, /2 only the light life · 0.2f.
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

// 0x540dcf: /4 lives 100, rises (rand() % 100 · 0.01f) · 4 + 1, takes the phase rand() % 300 − 150 in the x velocity,
// scale rand() % 6 · 0.15f plus the requested one, angles at 0 and the start; five rand(). 0x5455b0: the radius-120
// loop around the start with a = (life + phase) · 0.1f, the z rises and the scale falls 0.002f.
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
    // FSIN and FCOS retained, times the radius 120 (0x6b86c8), with a single R53.
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

// 0x540e74 (/5): life 6, the rise plus angle x · 1.2f, scale · 0.2f; 0x5455f9 advances cos and sin of the raw z angle
// times 20 and turns 1. 0x540ee7 (/6): the rise plus 5 and scale · 0.1f; 0x54563a rises and turns 4. 0x540f51 (/7):
// life 10; 0x545659 accelerates and fades.
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

// Subtypes outside 0..9 only take 0x54107f's turn and their step does nothing; all go to the common billboard.
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
