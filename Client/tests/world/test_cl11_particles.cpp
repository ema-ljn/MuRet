#include "Particles/Particles.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Math/RoundedMath.h"
#include "Particles/EffectParticleKernels.h"
#include "Particles/KalimaParticleKernels.h"
#include "Particles/ParticleOwner.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Particles/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "WorldObjects/WorldObjectStepServices.h"

#include "support/ParticleDrawRecorder.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <tuple>
#include <utility>

// Oracles written from the ASM of 0x53fe70, 0x5446e0 and 0x548550, with the rand() counted against
// Client::Tests::AfterDraws.
namespace
{
using Client::Crt::CrtRandom;
using Client::Math::RoundedCos;
using Client::Math::RoundedSin;
using Client::Particles::Particle;
using Client::Particles::ParticleError;
using Client::Particles::ParticleOwner;
using Client::Particles::ParticleOwnerKind;
using Client::Particles::ParticleOwnerState;
using Client::Particles::Particles;
using Client::Particles::PortableParticleTrigonometry;
using Client::Tests::AfterDraws;
using Client::Tests::DrawOneParticle;

constexpr std::uint32_t Seed = 0x1234;

using Client::Math::F32;

double D(std::uint32_t bits)
{
    return static_cast<double>(std::bit_cast<float>(bits));
}

// The seed's rand(), in order.
struct Draws
{
    CrtRandom random{Seed};
    int Next()
    {
        return random.Next();
    }
};

Particle& Only(Particles& particles, std::size_t index)
{
    return particles.All()[index];
}

// 0x5446e0 with all the game's services except the dynamic light and the owners.
Client::Core::Expected<void, ParticleError> Step(Particles& particles, CrtRandom& random, float clock,
                                                 const Client::Particles::ParticleOwnerStates& owners = {},
                                                 Client::Terrain::TerrainLighting* lighting = nullptr)
{
    PortableParticleTrigonometry trigonometry;
    const std::int32_t map = 0;
    return Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry, {}, {}, {},
                                                   {}, lighting, {}, {}, {}, owners);
}

TEST(Cl11Particles, TheBurstRingTurnsWithOneRandAndPulsesWithTheSine)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    const auto created = particles->CreateChecked(0x600, {1, 2, 3}, {0, 0, 45}, glm::vec3(1.0f), 1, 1.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    Draws draws;
    Particle& ring = Only(*particles, *created.Value());
    EXPECT_EQ(ring.life, 15);
    EXPECT_EQ(ring.verticalSpeed, 0.0f);
    EXPECT_EQ(ring.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 1).State());

    // 0x5473f1 with life 14: the scale from the sine of +0x50 (0), and +0x50 = (15 − 14) · 6.
    CrtRandom stepRandom(Seed);
    ASSERT_TRUE(Step(*particles, stepRandom, 0.0f).HasValue());
    EXPECT_EQ(ring.life, 14);
    EXPECT_EQ(ring.scale, F32(RoundedSin(0.0) * 3.0 + 1.5));
    EXPECT_EQ(ring.verticalSpeed, 6.0f);
    EXPECT_EQ(stepRandom.State(), AfterDraws(Seed, 2).State()); // only the wind
}

TEST(Cl11Particles, TheBurstRingStopsAtNinetyAndDimsBelowSix)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x600, {}, {}, glm::vec3(1.0f), 1, 1.0f, 0.0f, random).HasValue());
    Particle& ring = Only(*particles, 0);
    ring.life = 6;
    ring.verticalSpeed = 40.0f;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    // (15 − 5) · 6 + 40 = 100 > 90; life 5 halves the light.
    EXPECT_EQ(ring.verticalSpeed, 90.0f);
    EXPECT_EQ(ring.light, glm::vec3(0.5f));
    EXPECT_EQ(ring.scale, F32(RoundedSin(40.0 * D(0x3c8efa35u)) * 3.0 + 1.5));
}

TEST(Cl11Particles, ThePulseRingLivesTwentyWithoutRandAndShinesWithItsLife)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x600, {}, {}, glm::vec3(1.0f), 0, 1.0f, 0.0f, random).HasValue());
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
    Particle& ring = Only(*particles, 0);
    EXPECT_EQ(ring.life, 20);
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(ring.scale, F32(RoundedSin(19.0 * D(0x3e32b8c2u)) * 3.0 + 2.0));
    EXPECT_EQ(ring.light, glm::vec3(F32(19.0 * D(0x3e4ccccdu))));
}

TEST(Cl11Particles, AFullPoolCreatesNothingAndDrawsNoRand)
{
    auto particles = std::make_unique<Particles>();
    for (Particle& particle : particles->All())
        particle.alive = true;
    CrtRandom random(Seed);
    for (const auto& [type, subtype] : {std::pair{0x600, 1}, std::pair{0x5f2, 1}, std::pair{0x5d9, 13},
                                        std::pair{0x5c6, 7}, std::pair{0x6f4, 4}, std::pair{0x6f2, 2}})
    {
        const auto created = particles->CreateChecked(type, {}, {}, glm::vec3(1.0f), subtype, 1.0f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        EXPECT_FALSE(created.Value().has_value());
    }
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

TEST(Cl11Particles, ThePoisonSmokeDrawsSevenRandsAndFadesWithItsLife)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    const glm::vec3 position{100, 200, 300};
    ASSERT_TRUE(
        particles->CreateChecked(0x5f2, position, {7, 8, 9}, glm::vec3(1.0f), 1, 1.0f, 0.0f, random).HasValue());
    Draws draws;
    const Particle& smoke = Only(*particles, 0);
    EXPECT_EQ(smoke.life, 50);
    EXPECT_EQ(smoke.scale, F32(static_cast<double>((draws.Next() & 31) + 80) * D(0x3c23d70au)));
    EXPECT_EQ(smoke.position.x, F32(static_cast<double>((draws.Next() & 63) - 32) + 100.0));
    EXPECT_EQ(smoke.position.y, F32(static_cast<double>((draws.Next() & 63) - 32) + 200.0));
    EXPECT_EQ(smoke.position.z, F32(static_cast<double>((draws.Next() & 63) + 32) + 300.0));
    EXPECT_EQ(smoke.angles.x, static_cast<float>(draws.Next() % 90 - 45));
    EXPECT_EQ(smoke.angles.y, 8.0f);
    EXPECT_EQ(smoke.angles.z, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(smoke.velocity, glm::vec3(0.0f, static_cast<float>(-((draws.Next() & 7) + 40)), 0.0f));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 7).State());

    Particle copy = smoke;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    const double fade = 49.0 * D(0x3ca3d70au);
    EXPECT_EQ(smoke.light, glm::vec3(F32(fade * 0.5), F32(fade), F32(fade * D(0x3f4ccccdu))));
    EXPECT_EQ(smoke.velocity.y, F32(static_cast<double>(copy.velocity.y) * D(0x3ecccccdu)));
    EXPECT_EQ(smoke.scale, F32(static_cast<double>(copy.scale) + D(0x3d4ccccdu)));
}

TEST(Cl11Particles, TheEffectSmokeDrawsFourRandsAndGraysWithItsLife)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x5f2, {}, {}, glm::vec3(1.0f), 3, 1.0f, 0.0f, random).HasValue());
    EXPECT_EQ(Only(*particles, 0).life, 10);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 4).State());
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    const double fade = 9.0 * 0.125;
    const float gray = F32(fade * D(0x3f4ccccdu));
    EXPECT_EQ(Only(*particles, 0).light, glm::vec3(gray, gray, F32(fade)));
}

TEST(Cl11Particles, TheFountainFireRisesStraightAndTurnsInItsStep)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x5d9, {}, {}, glm::vec3(1.0f), 13, 2.0f, 0.0f, random).HasValue());
    Draws draws;
    const Particle& fire = Only(*particles, 0);
    static_cast<void>(draws.Next()); // the common velocity, which /13 overwrites
    EXPECT_EQ(fire.life, 20);
    EXPECT_EQ(fire.velocity.x, 0.0f);
    EXPECT_EQ(fire.velocity.y, 0.0f);
    EXPECT_EQ(fire.velocity.z, F32(static_cast<double>((draws.Next() & 127) + 128) * D(0x3e19999au)));
    static_cast<void>(draws.Next()); // the turn that the end overwrites
    EXPECT_EQ(fire.scale, F32(static_cast<double>((draws.Next() & 15) + 150) * D(0x3c449ba6u)));
    EXPECT_EQ(fire.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 5).State());
}

TEST(Cl11Particles, EveryFireSubtypeIsCreatedAndStepped)
{
    for (const std::int32_t type : {0x5d9, 0x5db, 0x5dc})
        for (std::int32_t subtype = -1; subtype <= 15; ++subtype)
        {
            if (subtype == Client::Particles::FireRingSubtype)
                continue;
            auto particles = std::make_unique<Particles>();
            CrtRandom random(Seed);
            ASSERT_TRUE(particles->CreateChecked(type, {}, {}, glm::vec3(1.0f), subtype, 1.0f, 0.0f, random).HasValue())
                << type << "/" << subtype;
            ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue()) << type << "/" << subtype;
            // 0x545958's common part: frame (23 − 23) / 6.
            EXPECT_EQ(std::bit_cast<std::int32_t>(Only(*particles, 0).field48), 0);
        }
}

// 0x5412d2 and 0x54132c: /7 and /11 sway sideways and grow over the requested scale; only /11 writes the life again
// (24, the same). Four rand(): the common velocity (which they overwrite), the sway, the scale and the turn.
TEST(Cl11Particles, TheSwayingFiresDrawTheirSwayAndTheirScale)
{
    for (const std::int32_t subtype : {7, 11})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(particles->CreateChecked(0x5d9, {}, {}, glm::vec3(1.0f), subtype, 0.5f, 0.0f, random).HasValue());
        Draws draws;
        const Particle& fire = Only(*particles, 0);
        static_cast<void>(draws.Next());
        EXPECT_EQ(fire.life, 24) << subtype;
        EXPECT_EQ(fire.velocity,
                  glm::vec3(0.0f, F32(static_cast<double>((draws.Next() & 31) - 16) * D(0xbdcccccdu)), 0.0f))
            << subtype;
        EXPECT_EQ(fire.scale, F32(static_cast<double>((draws.Next() & 63) + 128) * D(0x3c03126fu) + 0.5)) << subtype;
        EXPECT_EQ(fire.rotation, static_cast<float>(draws.Next() % 360)) << subtype;
        EXPECT_EQ(random.State(), AfterDraws(Seed, 4).State()) << subtype;
    }
}

// 0x5457a5: rise + 0.004f and the subtype's case (0x548270): /5 and /6 fall 0.04f and turn 5; /8 by 0.95f and turns 5;
// /10 by 0.95f; /11 grows 0.04f. Common part: z + rise · 10 and frame (23 − life) / 6.
TEST(Cl11Particles, TheFireStepFollowsItsSubtypeTable)
{
    struct Case
    {
        std::int32_t subtype;
        double scale;
        double turn;
    };
    const double shrunk = 1.0 - D(0x3d23d70au);
    const double damped = 1.0 * D(0x3f733333u);
    for (const Case& value : {Case{5, shrunk, 5.0}, Case{6, shrunk, 5.0}, Case{8, damped, 5.0}, Case{10, damped, 0.0},
                              Case{11, 1.0 + D(0x3d23d70au), 0.0}})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(
            particles->CreateChecked(0x5db, {}, {}, glm::vec3(1.0f), value.subtype, 1.0f, 0.0f, random).HasValue());
        Particle& fire = Only(*particles, 0);
        fire.scale = 1.0f;
        fire.rotation = 10.0f;
        fire.verticalSpeed = 0.5f;
        fire.velocity = glm::vec3(0.0f);
        fire.position = glm::vec3(0.0f, 0.0f, 100.0f);
        fire.life = 20;
        ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue()) << value.subtype;
        const float rise = F32(0.5 + D(0x3b83126fu));
        EXPECT_EQ(fire.verticalSpeed, rise) << value.subtype;
        EXPECT_EQ(fire.scale, F32(value.scale)) << value.subtype;
        EXPECT_EQ(fire.rotation, F32(10.0 + value.turn)) << value.subtype;
        EXPECT_EQ(fire.position.z, F32(static_cast<double>(rise) * 10.0 + 100.0)) << value.subtype;
        EXPECT_EQ(std::bit_cast<std::int32_t>(fire.field48), (23 - 19) / 6) << value.subtype;
    }
}

// 0x5410ea and 0x545750: 5ca/1 lives 10 with light 0.5 and its step fades it with life and enlarges it 0.1f.
TEST(Cl11Particles, TheGrayThunderFadesWithItsLife)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x5ca, {}, {}, glm::vec3(1.0f), 1, 2.0f, 0.0f, random).HasValue());
    Draws draws;
    Particle& thunder = Only(*particles, 0);
    const float scale = F32(static_cast<double>((draws.Next() & 7) + 6) * 2.0 * D(0x3dcccccdu));
    EXPECT_EQ(thunder.scale, scale);
    EXPECT_EQ(thunder.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(thunder.verticalSpeed, 20.0f);
    EXPECT_EQ(thunder.life, 10);
    EXPECT_EQ(thunder.light, glm::vec3(0.5f));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(thunder.light, glm::vec3(F32(9.0 * D(0x3d888889u))));
    EXPECT_EQ(thunder.scale, F32(static_cast<double>(scale) + D(0x3dcccccdu)));
}

// 0x541112..0x541167 and 0x54577c: 5ca/2 changes the texture to 0x61e, lives 10, takes a third rand() for the scale
// (+ 0.3f) and stays without turn; its step lowers the light 0.01f per channel.
TEST(Cl11Particles, TheThunderBoltTakesItsTextureAndAThirdRand)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x5ca, {}, {}, glm::vec3(0.5f), 2, 2.0f, 0.0f, random).HasValue());
    Draws draws;
    Particle& bolt = Only(*particles, 0);
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    EXPECT_EQ(bolt.texture, 0x61e);
    EXPECT_EQ(bolt.life, 10);
    EXPECT_EQ(bolt.scale, F32(static_cast<double>((draws.Next() & 7) + 6) * 2.0 * D(0x3dcccccdu) + D(0x3e99999au)));
    EXPECT_EQ(bolt.rotation, 0.0f);
    EXPECT_EQ(bolt.verticalSpeed, 0.0f);
    EXPECT_EQ(bolt.light, glm::vec3(0.5f));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 3).State());
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(bolt.light, glm::vec3(F32(0.5 - D(0x3c23d70au))));
}

// 0x541693..0x54174b: 5de/2 jumps forward with 0x4e98d0 (angles 0: the y) t = rand() % 80 · red · 0.01f and keeps the
// velocity (0; (red − t) · 0.0666667f; 0). 0x5414a5: /6 rises straight and scales with the requested one. 0x545d21:
// /1's step at life 10 adds 6.4f to the velocity and lowers the scale 0.15f; it turns and fades the light 0.05f.
TEST(Cl11Particles, TheFlameJumpsGrowsAndKicks)
{
    PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(particles
                        ->CreateChecked(0x5de, {0, 0, 0}, {0, 0, 0}, glm::vec3(0.8f, 1, 1), 2, 1.0f, std::cref(clock),
                                        random, 0, trigonometry)
                        .HasValue());
        Draws draws;
        const Particle& flame = Only(*particles, 0);
        EXPECT_EQ(flame.scale, F32(static_cast<double>((draws.Next() & 31) + 32) * D(0x3c23d70au) + 1.0));
        const float jump = F32(static_cast<double>(draws.Next() % 80) * D(0x3f4ccccdu) * D(0x3c23d70au));
        EXPECT_EQ(flame.position, glm::vec3(0.0f, jump, 0.0f));
        EXPECT_EQ(flame.velocity,
                  glm::vec3(0.0f, F32((D(0x3f4ccccdu) - static_cast<double>(jump)) * D(0x3d888889u)), 0.0f));
    }
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(particles->CreateChecked(0x5de, {}, {}, glm::vec3(1.0f), 6, 2.0f, 0.0f, random).HasValue());
        Draws draws;
        const Particle& jet = Only(*particles, 0);
        EXPECT_EQ(jet.velocity,
                  glm::vec3(0.0f, 0.0f, F32(static_cast<double>((draws.Next() & 127) + 256) * D(0x3df5c28fu))));
        EXPECT_EQ(jet.scale, F32(static_cast<double>((draws.Next() & 63) + 64) * 2.0 * D(0x3c23d70au)));
    }
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(particles->CreateChecked(0x5de, {}, {}, glm::vec3(1.0f), 1, 1.0f, 0.0f, random).HasValue());
        Particle& flame = Only(*particles, 0);
        flame.life = 11;
        flame.scale = 1.0f;
        flame.velocity = glm::vec3(0.0f, 1.0f, 0.0f);
        CrtRandom step(Seed);
        ASSERT_TRUE(Step(*particles, step, 0.0f).HasValue());
        Draws draws;
        static_cast<void>(draws.Next());
        static_cast<void>(draws.Next()); // the wind
        EXPECT_EQ(flame.velocity.y, F32(1.0 + D(0x40cccccdu)));
        EXPECT_EQ(flame.scale, F32(1.0 - D(0x3e19999au)));
        EXPECT_EQ(flame.rotation, static_cast<float>(draws.Next() % 360));
        EXPECT_EQ(flame.light, glm::vec3(F32(1.0 - D(0x3d4ccccdu))));
    }
}

TEST(Cl11Particles, TheRingFireFollowsItsOwner)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const ParticleOwner owner{ParticleOwnerKind::Actor, 3};
    ASSERT_TRUE(
        particles
            ->CreateChecked(0x5d9, {}, {}, glm::vec3(1.0f), 9, 1.0f, std::cref(clock), random, 0, trigonometry, owner)
            .HasValue());
    Draws draws;
    static_cast<void>(draws.Next());
    const int offset = draws.Next() % 60 - 30;
    Particle& fire = Only(*particles, 0);
    EXPECT_EQ(fire.start.x, static_cast<float>(offset));
    EXPECT_EQ(fire.start.z, F32(190.0 - std::abs(offset) * 1.5));
    EXPECT_EQ(fire.typedOwner, owner);
    EXPECT_EQ(Step(*particles, random, 0.0f).Error(), ParticleError::MissingOwner);

    fire.life = 24;
    const auto owners = [](const ParticleOwner&) { return std::optional(ParticleOwnerState{true, {10, 20, 30}, {}}); };
    ASSERT_TRUE(Step(*particles, random, 0.0f, owners).HasValue());
    EXPECT_EQ(fire.position.x, F32(10.0 + fire.start.x));
}

TEST(Cl11Particles, TheFollowingLightningSticksEightyAboveItsOwner)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const ParticleOwner owner{ParticleOwnerKind::Actor, 1};
    ASSERT_TRUE(
        particles
            ->CreateChecked(0x5d5, {}, {}, glm::vec3(0.25f), 2, 3.0f, std::cref(clock), random, 0, trigonometry, owner)
            .HasValue());
    Particle& bolt = Only(*particles, 0);
    EXPECT_EQ(bolt.life, 20);
    EXPECT_EQ(bolt.scale, 1.0f);
    EXPECT_EQ(bolt.light, glm::vec3(0.75f));
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());

    auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    const auto owners = [](const ParticleOwner& asked)
    { return asked.identity == 1 ? std::optional(ParticleOwnerState{true, {10, 20, 30}, {}}) : std::nullopt; };
    ASSERT_TRUE(Step(*particles, random, 0.0f, owners, lighting.get()).HasValue());
    const double glow = 19.0 * D(0x3dcccccdu);
    EXPECT_EQ(bolt.scale, F32(1.0 + D(0x3dcccccdu)));
    EXPECT_EQ(bolt.light, glm::vec3(F32(glow * 0.5), F32(glow), F32(static_cast<double>(F32(glow)) * D(0x3f4ccccdu))));
    EXPECT_EQ(bolt.position, glm::vec3(10.0f, 20.0f, 110.0f));
}

TEST(Cl11Particles, TheOtherLightningSubtypesKeepOrLiftTheLight)
{
    for (const auto& [subtype, life, scale, light] : {std::tuple{3, 1, 2.0f, 0.75f}, std::tuple{4, 2, 0.25f, 0.25f},
                                                      std::tuple{5, 2, 2.0f, 0.25f}, std::tuple{9, 2, 2.0f, 0.75f}})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(particles->CreateChecked(0x5d5, {}, {}, glm::vec3(0.25f), subtype, 2.0f, 0.0f, random).HasValue());
        EXPECT_EQ(Only(*particles, 0).life, life) << subtype;
        EXPECT_EQ(Only(*particles, 0).scale, scale) << subtype;
        EXPECT_EQ(Only(*particles, 0).light, glm::vec3(light)) << subtype;
    }
}

TEST(Cl11Particles, TheFollowingSparkNeedsItsOwnerAndChasesIt)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const ParticleOwner owner{ParticleOwnerKind::ModelEffect, 2};
    EXPECT_EQ(particles->CreateChecked(0x5c6, {}, {}, glm::vec3(1.0f), 4, 1.0f, 0.0f, random).Error(),
              ParticleError::MissingOwner);
    EXPECT_FALSE(Only(*particles, 0).alive);
    ASSERT_TRUE(particles
                    ->CreateTowardChecked(0x5c6, {0, 0, 0}, {1, 2, 3}, glm::vec3(1.0f), 4, 1.0f, std::cref(clock),
                                          random, trigonometry, owner, {60, 0, -6})
                    .HasValue());
    Particle& spark = Only(*particles, 0);
    EXPECT_EQ(spark.angles, glm::vec3(0.0f));
    EXPECT_EQ(spark.life, 10);
    EXPECT_EQ(spark.velocity.x, F32(60.0 * D(0x3e2aaaabu)));
    EXPECT_EQ(spark.velocity.z, F32(-6.0 * D(0x3e2aaaabu)));
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());

    // The dead owner leaves it at life 5; it moved (1, 2, 3) from its start.
    const auto owners = [](const ParticleOwner&)
    { return std::optional(ParticleOwnerState{false, {11, 22, 33}, glm::vec3(10, 20, 30)}); };
    const glm::vec3 velocity = spark.velocity;
    ASSERT_TRUE(Step(*particles, random, 0.0f, owners).HasValue());
    EXPECT_EQ(spark.life, 5);
    EXPECT_EQ(spark.light, glm::vec3(F32(5.0 * D(0x3e4ccccdu))));
    EXPECT_EQ(spark.start, velocity);
    EXPECT_EQ(spark.position, glm::vec3(velocity.x + 1.0f, velocity.y + 2.0f, velocity.z + 3.0f));
}

TEST(Cl11Particles, TheRareSparkIsBornOnceInFiftyAndTheOthersDie)
{
    auto particles = std::make_unique<Particles>();
    std::uint32_t lucky = 1;
    while (CrtRandom(lucky).Next() % 50 != 0)
        ++lucky;
    CrtRandom random(lucky);
    ASSERT_TRUE(particles->CreateChecked(0x5c6, {}, {}, glm::vec3(1.0f), 6, 1.0f, 0.0f, random).HasValue());
    EXPECT_EQ(Only(*particles, 0).life, 50);
    EXPECT_EQ(random.State(), AfterDraws(lucky, 3).State());

    CrtRandom unlucky(lucky + 1);
    ASSERT_NE(CrtRandom(lucky + 1).Next() % 50, 0);
    ASSERT_TRUE(particles->CreateChecked(0x5c6, {}, {}, glm::vec3(1.0f), 6, 1.0f, 0.0f, unlucky).HasValue());
    EXPECT_EQ(Only(*particles, 1).life, 0);
    EXPECT_TRUE(Only(*particles, 1).alive);
    EXPECT_EQ(unlucky.State(), AfterDraws(lucky + 1, 1).State());
    // The step turns it off, but its case still runs: the wind (2) and the green (1) of each one.
    CrtRandom step(Seed);
    ASSERT_TRUE(Step(*particles, step, 0.0f).HasValue());
    EXPECT_FALSE(Only(*particles, 1).alive);
    EXPECT_EQ(step.State(), AfterDraws(Seed, 4).State());
}

TEST(Cl11Particles, TheHitSparkShrinksAndDiesUnderAFifth)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x5c6, {}, {}, glm::vec3(1.0f), 0, 1.0f, 0.0f, random).HasValue());
    EXPECT_EQ(Only(*particles, 0).life, 2);
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(Only(*particles, 0).scale, 0.5f);
    EXPECT_TRUE(Only(*particles, 0).alive);
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_FALSE(Only(*particles, 0).alive);
}

TEST(Cl11Particles, TheFlameSubtypesDrawTheirRandsAndLightByTheClock)
{
    PortableParticleTrigonometry trigonometry;
    const float clock = 1234.0f;
    const double glow = RoundedSin(1234.0 * D(0x3b03126fu)) * D(0x3e99999au) + D(0x3f333333u);
    for (const auto& [subtype, draws, life] :
         {std::tuple{1, 2, 15}, std::tuple{2, 2, 10}, std::tuple{3, 1, 10}, std::tuple{5, 1, 4}, std::tuple{6, 2, 10}})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(particles
                        ->CreateChecked(0x5de, {}, {}, glm::vec3(1.0f), subtype, 1.0f, std::cref(clock), random, 0,
                                        trigonometry)
                        .HasValue())
            << subtype;
        EXPECT_EQ(Only(*particles, 0).life, life) << subtype;
        EXPECT_EQ(random.State(), AfterDraws(Seed, draws).State()) << subtype;
        if (subtype == 2 || subtype == 3)
        {
            EXPECT_EQ(Only(*particles, 0).light, glm::vec3(F32(glow), F32(glow * 0.5), F32(glow * 0.5))) << subtype;
        }
        ASSERT_TRUE(Step(*particles, random, clock).HasValue()) << subtype;
    }
}

TEST(Cl11Particles, TheKundunSplashesRiseWithTheirTiltAndAdvanceTwenty)
{
    for (const auto& [type, subtype, scaleStep] :
         {std::tuple{0x6f2, 2, 0x3dcccccdu}, std::tuple{0x6f3, 2, 0x3dcccccdu}, std::tuple{0x6f4, 4, 0x3e4ccccdu}})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        const glm::vec3 position{5, 6, 7};
        ASSERT_TRUE(particles->CreateChecked(type, position, {10, 0, 2}, glm::vec3(0.5f), subtype, 1.0f, 0.0f, random)
                        .HasValue());
        Draws draws;
        Particle& splash = Only(*particles, 0);
        const double rise = static_cast<double>(draws.Next() % 100) * D(0x3c23d70au) * 4.0;
        EXPECT_EQ(splash.life, 6);
        EXPECT_EQ(splash.verticalSpeed, F32(10.0 * D(0x3f99999au) + rise));
        EXPECT_EQ(splash.scale, F32(static_cast<double>(draws.Next() % 6) * D(scaleStep) + 1.0));
        EXPECT_EQ(splash.rotation, static_cast<float>(draws.Next() % 360));
        EXPECT_EQ(splash.start, position);
        EXPECT_EQ(random.State(), AfterDraws(Seed, 3).State());

        const float speed = splash.verticalSpeed;
        const float turn = splash.rotation;
        ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
        EXPECT_EQ(splash.position.x, F32(RoundedCos(2.0) * 20.0 + 5.0));
        EXPECT_EQ(splash.position.y, F32(RoundedSin(2.0) * 20.0 + 6.0));
        EXPECT_EQ(splash.position.z, F32(static_cast<double>(speed) + 7.0));
        EXPECT_EQ(splash.rotation, F32(static_cast<double>(turn) + 1.0));
        // 6f4/4 keeps 6f4's fade (life 5 < 8); the others, by 0.9090909f.
        const float faded = type == 0x6f4 ? F32(0.5 * D(0x3f555555u)) : F32(0.5 * D(0x3f68ba2eu));
        EXPECT_EQ(splash.light, glm::vec3(faded));
    }
}

TEST(Cl11Particles, TheKundunFallKeepsTheRequestedLightAndTurns)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x6f4, {}, {}, glm::vec3(0.5f), 3, 1.0f, 0.0f, random).HasValue());
    Draws draws;
    Particle& fall = Only(*particles, 0);
    EXPECT_EQ(fall.life, 20);
    EXPECT_EQ(fall.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(fall.scale, std::bit_cast<float>(0x3fcccccdu));
    EXPECT_EQ(fall.velocity.z, static_cast<float>(-7 - draws.Next() % 5));
    EXPECT_EQ(fall.light, glm::vec3(0.5f));
    const float turn = fall.rotation;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(fall.rotation, F32(static_cast<double>(turn) + 4.0));
    EXPECT_EQ(fall.scale, F32(D(0x3fcccccdu) - static_cast<double>(0.005f)));
}

// 0x543d68..0x543dba: 6f4/2 of the hit of a piece of Kundun (0x52a37d) lives 20, turns rand() % 360, scale 0.5, raises
// the z 50 (123.3f + 50: 0x432d4ccd) and the rise is rand() % 5 + 10, with the requested light. 0x547ee2: after the
// common movement (z plus the rise: 0x433b4ccd), scale 0.1f more (0x3f19999a), x and y plus rand() % 10 − 5 and the
// rise 1 less; with life 19, no fade. With the 3000 full, nothing. Bits computed separately (x87).
TEST(Cl11Particles, TheKundunDebrisRisesAndWanders)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    const glm::vec3 dim{std::bit_cast<float>(0x3e99999au)};
    const auto made = particles->CreateChecked(0x6f4, {1000.0f, 2000.0f, std::bit_cast<float>(0x42f6999au)}, {}, dim, 2,
                                               1.0f, 0.0f, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), std::optional<std::size_t>(0));
    Particle& debris = Only(*particles, 0);
    EXPECT_EQ(debris.life, 20);
    EXPECT_EQ(debris.rotation, 136.0f);
    EXPECT_EQ(debris.scale, 0.5f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(debris.position.z), 0x432d4ccdu);
    EXPECT_EQ(debris.velocity, glm::vec3(0.0f, 0.0f, 14.0f));
    EXPECT_EQ(debris.light, dim);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());

    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(debris.life, 19);
    EXPECT_EQ(debris.position.x, 1001.0f);
    EXPECT_EQ(debris.position.y, 1999.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(debris.position.z), 0x433b4ccdu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(debris.scale), 0x3f19999au);
    EXPECT_EQ(debris.velocity.z, 13.0f);
    EXPECT_EQ(debris.light, dim);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 6).State());
    // 0x548550: to the default (0x54889a), the common billboard with texture 0x6f4.
    const auto drawn = DrawOneParticle(debris, glm::vec2(32.0f), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, 0x6f4);

    // Already at 1 or more, the scale does not grow; with life 7, the light times 0.8333333f (0x3f555555) after the
    // step.
    debris.scale = 1.0f;
    debris.life = 8;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(debris.scale, 1.0f);
    EXPECT_EQ(debris.light, glm::vec3(F32(D(0x3e99999au) * D(0x3f555555u))));

    for (auto& particle : particles->All())
        particle.alive = true;
    CrtRandom full(Seed);
    const auto none = particles->CreateChecked(0x6f4, {}, {}, dim, 2, 1.0f, 0.0f, full);
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.State(), CrtRandom(Seed).State());
}

// 0x5446e0 with the terrain height (0x5f93c0) fixed at `ground`.
Client::Core::Expected<void, ParticleError> StepOverGround(Particles& particles, CrtRandom& random, double ground)
{
    PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    const Client::Particles::ParticleTerrainHeight height = [ground](float, float) { return ground; };
    return Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry, height, {},
                                                   {}, {}, nullptr, {}, {}, {});
}

glm::vec3 Vec(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return {std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)};
}

// 0x542c4b..0x542d00 and 0x546da6..0x546f08: flowers 608..60a of arrow 0xe1 (0x526a8b). At birth, five rand(): life 21,
// scale 9 · 0.03f (overwrites the requested one), light 1 and velocity (0.5; −0.9; −1.2). Their step, after the common
// movement (with angles at 0, the velocity as is), adds three drifts to the velocity with /0, or two sways and a fall
// with /1, adds it to the position again and turns with another rand(). The bits come from x87.py, separately.
TEST(Cl11Particles, TheNatureArrowFlowersDriftAndSway)
{
    struct Case
    {
        std::int32_t type;
        std::int32_t subtype;
        glm::vec3 velocity;
        glm::vec3 position;
    };
    const std::array cases{
        Case{0x608, 0, Vec(0x3f000000u, 0x3eccccccu, 0xc0000000u), Vec(0x447a4000u, 0x44f9f000u, 0x43946666u)},
        Case{0x60a, 1, Vec(0x3e99999au, 0xbf59999au, 0xbf99999au), Vec(0x447a3333u, 0x44f9c800u, 0x4394ccccu)}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.subtype);
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        const auto made = particles->CreateChecked(probe.type, {1000.0f, 2000.0f, 300.0f}, {}, glm::vec3(0.5f),
                                                   probe.subtype, 2.0f, 0.0f, random);
        ASSERT_TRUE(made.HasValue());
        ASSERT_EQ(made.Value(), std::optional<std::size_t>(0));
        Particle& flower = Only(*particles, 0);
        EXPECT_EQ(flower.texture, probe.type);
        EXPECT_EQ(flower.life, 21);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.scale), 0x3e8a3d70u);
        EXPECT_EQ(flower.light, glm::vec3(1.0f));
        EXPECT_EQ(flower.velocity, Vec(0x3f000000u, 0xbf666667u, 0xbf99999au));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.field48), 0u);
        EXPECT_EQ(random.State(), AfterDraws(Seed, 5).State());

        ASSERT_TRUE(StepOverGround(*particles, random, 0.0).HasValue());
        EXPECT_EQ(flower.life, 20);
        EXPECT_EQ(flower.velocity, probe.velocity);
        EXPECT_EQ(flower.position, probe.position);
        EXPECT_EQ(flower.rotation, 2.0f);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.field48), 0u);
        EXPECT_EQ(random.State(), AfterDraws(Seed, 11).State());
        // 0x548550: 0x5488f6 → 0x54889a, the common billboard with the type's texture.
        const auto drawn = DrawOneParticle(flower, glm::vec2(32.0f), std::uint8_t{3});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->texture, probe.type);
    }
}

// 0x546eb8..0x546ee7: with the terrain above the z, the flower lands: z at the height, +0x48 at the integer 1 and
// velocity at 0. Landed, it neither drifts nor moves: only the wind and the turn's rand(); with the terrain level with
// it, nothing. Without the height it cuts (MissingTerrainHeight). Full, nothing and no rand().
TEST(Cl11Particles, TheNatureArrowFlowerLandsOnTheGround)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x609, {1000.0f, 2000.0f, 300.0f}, {}, glm::vec3(1.0f), 0, 1.0f, 0.0f, random)
                    .HasValue());
    Particle& flower = Only(*particles, 0);
    ASSERT_TRUE(StepOverGround(*particles, random, 500.0).HasValue());
    EXPECT_EQ(flower.position, Vec(0x447a4000u, 0x44f9f000u, 0x43fa0000u));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.field48), 1u);
    EXPECT_EQ(flower.velocity, glm::vec3(0.0f));
    EXPECT_EQ(flower.rotation, 2.0f);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 11).State());
    ASSERT_TRUE(StepOverGround(*particles, random, 500.0).HasValue());
    EXPECT_EQ(flower.position, Vec(0x447a4000u, 0x44f9f000u, 0x43fa0000u));
    EXPECT_EQ(flower.rotation, 2.0f);
    EXPECT_EQ(flower.life, 19);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 14).State());
    const auto cut = Step(*particles, random, 0.0f);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_EQ(cut.Error(), ParticleError::MissingTerrainHeight);

    for (auto& particle : particles->All())
        particle.alive = true;
    CrtRandom full(Seed);
    const auto none = particles->CreateChecked(0x608, {}, {}, glm::vec3(1.0f), 0, 1.0f, 0.0f, full);
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.State(), CrtRandom(Seed).State());
}

// 0x546e29..0x546e9d: /2 (and any other subtype outside /0 and /1) does not drift: no rand() of its own, position plus
// velocity again after the common movement, and the turn. With seed 0x1234, 0x609 is born like the ones above and in
// one step ends at (0x447a4000; 0x44f9c666; 0x4394cccc) with the turn at 0 (rand() % 16 of 22672): the wind, the turn
// and the five of creation, eight rand() (flower2.py, separately).
TEST(Cl11Particles, TheNatureArrowFlowerOfSubtypeTwoDoesNotDrift)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x609, {1000.0f, 2000.0f, 300.0f}, {}, glm::vec3(0.5f), 2, 2.0f, 0.0f, random)
                    .HasValue());
    Particle& flower = Only(*particles, 0);
    EXPECT_EQ(flower.subtype, 2);
    EXPECT_EQ(flower.velocity, Vec(0x3f000000u, 0xbf666667u, 0xbf99999au));
    ASSERT_TRUE(StepOverGround(*particles, random, 0.0).HasValue());
    EXPECT_EQ(flower.life, 20);
    EXPECT_EQ(flower.velocity, Vec(0x3f000000u, 0xbf666667u, 0xbf99999au));
    EXPECT_EQ(flower.position, Vec(0x447a4000u, 0x44f9c666u, 0x4394ccccu));
    EXPECT_EQ(flower.rotation, 0.0f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.field48), 0u);
    EXPECT_EQ(random.State(), 0x5890c57cu);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 8).State());
}

// 0x546ec5..0x546ed0: `fcom` of the height against the z and `test ah, 0x41`: with the height NaN, C0 and C3 at 1 and
// the flower does not land; the turn comes out the same. /0 stays as in TheNatureArrowFlowersDriftAndSway.
TEST(Cl11Particles, TheNatureArrowFlowerDoesNotLandOnANaNGround)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x608, {1000.0f, 2000.0f, 300.0f}, {}, glm::vec3(0.5f), 0, 2.0f, 0.0f, random)
                    .HasValue());
    Particle& flower = Only(*particles, 0);
    ASSERT_TRUE(StepOverGround(*particles, random, std::numeric_limits<double>::quiet_NaN()).HasValue());
    EXPECT_EQ(flower.position, Vec(0x447a4000u, 0x44f9f000u, 0x43946666u));
    EXPECT_EQ(flower.velocity, Vec(0x3f000000u, 0x3eccccccu, 0xc0000000u));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(flower.field48), 0u);
    EXPECT_EQ(flower.rotation, 2.0f);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 11).State());
}

// 0x542d01..0x542d5f and 0x546f0d..0x546f51: column 62b/0 of circle 0xcc (0x523677 and 0x52384c). Born with two
// rand(): life 36, light 1 and velocity (0; 0; 89 · 0.02f). Its step raises it with the velocity (common movement),
// leaves the scale at 0.2f and accelerates 0.4f up to 8; with life below 5, the light times 0.8333333f. The other
// subtypes only have the prefix (0x54304c) and no step of their own, no rand(). Bits from x87.py.
TEST(Cl11Particles, TheFlareColumnRisesFasterUpToEight)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(
        particles->CreateChecked(0x62b, {10.0f, 20.0f, 100.0f}, {}, glm::vec3(0.5f), 0, 1.0f, 0.0f, random).HasValue());
    Particle& column = Only(*particles, 0);
    EXPECT_EQ(column.life, 36);
    EXPECT_EQ(column.light, glm::vec3(1.0f));
    EXPECT_EQ(column.scale, 1.0f);
    EXPECT_EQ(column.velocity, Vec(0, 0, 0x3fe3d70au));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(column.position, Vec(0x41200000u, 0x41a00000u, 0x42cb8f5cu));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(column.velocity.z), 0x400b851fu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(column.scale), 0x3e4ccccdu);
    EXPECT_EQ(column.light, glm::vec3(1.0f));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 4).State());
    column.velocity.z = 7.9f;
    column.life = 5;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(column.velocity.z, 8.0f);
    EXPECT_EQ(column.light, glm::vec3(std::bit_cast<float>(0x3f555555u)));
    const auto drawn = DrawOneParticle(column, glm::vec2(32.0f), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, 0x62b);
    // 0x546f2b..0x546f3c: `fcomp` of 8 against the velocity and `test ah, 1`: a NaN leaves C0 at 1 and the velocity
    // at 8.
    column.velocity.z = std::numeric_limits<float>::quiet_NaN();
    column.life = 6;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(column.velocity.z, 8.0f);

    auto others = std::make_unique<Particles>();
    CrtRandom quiet(Seed);
    ASSERT_TRUE(others->CreateChecked(0x62b, {1, 2, 3}, {}, glm::vec3(0.5f), 3, 1.5f, 0.0f, quiet).HasValue());
    EXPECT_EQ(quiet.State(), CrtRandom(Seed).State());
    const Particle& plain = Only(*others, 0);
    EXPECT_EQ(plain.life, 2); // 0x53ff05: the prefix's life.
    EXPECT_EQ(plain.scale, 1.5f);
    EXPECT_EQ(plain.light, glm::vec3(0.5f));
    ASSERT_TRUE(Step(*others, quiet, 0.0f).HasValue());
    EXPECT_EQ(plain.scale, 1.5f);
    EXPECT_EQ(plain.light, glm::vec3(0.5f));
    EXPECT_EQ(quiet.State(), AfterDraws(Seed, 2).State());
}

// 0x543bf1..0x543c25 and 0x547d42..0x547dc1: hole 6ae of Kalima's gate 0xc5 (0x43c20d), any subtype. Born with one
// rand(): life 30, light 0.1f and turn 136. Its step turns 5; from life 29 to 20 the light times 1.1f; below 20 the
// scale minus 0.08f and, below 10, also the light times 0.7692308f. Bits from x87.py.
TEST(Cl11Particles, TheKalimaGateHoleTurnsGlowsAndShrinks)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x6ae, {1, 2, 3}, {}, glm::vec3(1.0f), 4, 3.0f, 0.0f, random).HasValue());
    Particle& hole = Only(*particles, 0);
    EXPECT_EQ(hole.life, 30);
    EXPECT_EQ(hole.rotation, 136.0f);
    EXPECT_EQ(hole.light, glm::vec3(std::bit_cast<float>(0x3dcccccdu)));
    EXPECT_EQ(hole.scale, 3.0f);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 1).State());
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(hole.life, 29);
    EXPECT_EQ(hole.rotation, 141.0f);
    EXPECT_EQ(hole.light, glm::vec3(std::bit_cast<float>(0x3de147afu)));
    EXPECT_EQ(hole.scale, 3.0f);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 3).State());
    hole.life = 20;
    hole.light = glm::vec3(std::bit_cast<float>(0x3dcccccdu));
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(hole.scale), 0x403ae148u);
    EXPECT_EQ(hole.light, glm::vec3(std::bit_cast<float>(0x3dcccccdu)));
    hole.life = 10;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(hole.scale), 0x4035c290u);
    EXPECT_EQ(hole.light, glm::vec3(std::bit_cast<float>(0x3d9d89d9u)));
    const auto drawn = DrawOneParticle(hole, glm::vec2(32.0f), std::uint8_t{3});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, 0x6ae);
}

// 0x543b82..0x543be5: the fanta 0x6ec/0x6ed of any subtype is born like /0 (life 24, /6 life 32); 0x543aed: /7 lives
// 15, with scale rand() % 30 · 0.01f plus the requested one and the position shifted. 0x547cc2: its step shrinks 0.1f
// and rises 2 before the common part (the z rises 1 and the light is life · 0.04f).
TEST(Cl11Particles, TheFantaTakesEverySubtype)
{
    for (const std::int32_t subtype : {6, 12})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(
            particles->CreateChecked(0x6ec, {1, 2, 3}, {}, glm::vec3(1.0f), subtype, 1.0f, 0.0f, random).HasValue());
        Draws draws;
        const Particle& fanta = Only(*particles, 0);
        EXPECT_EQ(fanta.life, subtype == 6 ? 32 : 24) << subtype;
        EXPECT_EQ(fanta.velocity.x, F32(static_cast<double>(draws.Next() % 10 - 5) * D(0x3ecccccdu))) << subtype;
        EXPECT_EQ(fanta.velocity.z, F32(static_cast<double>(draws.Next() % 10 + 5) * D(0x3e4ccccdu))) << subtype;
        EXPECT_EQ(fanta.start, glm::vec3(1, 2, 3)) << subtype;
        EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State()) << subtype;
    }

    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(0x6ed, {10, 20, 30}, {}, glm::vec3(1.0f), 7, 1.5f, 0.0f, random).HasValue());
    Draws draws;
    Particle& swirl = Only(*particles, 0);
    EXPECT_EQ(swirl.life, 15);
    EXPECT_EQ(swirl.scale, F32(static_cast<double>(draws.Next() % 30) * D(0x3c23d70au) + 1.5));
    glm::vec3 shifted{10, 20, 30};
    for (int axis = 0; axis < 3; ++axis)
        shifted[axis] =
            F32((static_cast<double>(draws.Next() % 10) * D(0x3dcccccdu) - 0.5) + static_cast<double>(shifted[axis]));
    EXPECT_EQ(swirl.position, shifted);
    EXPECT_EQ(swirl.start, shifted);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 6).State());
    swirl.velocity = glm::vec3(0.0f);
    swirl.scale = 1.0f;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(swirl.scale, F32(1.0 - D(0x3dcccccdu)));
    EXPECT_EQ(swirl.position.z, F32(static_cast<double>(F32(static_cast<double>(shifted.z) + 2.0)) + 1.0));
    EXPECT_EQ(swirl.light, glm::vec3(F32(14.0 * D(0x3d23d70au))));
}

// 0x543b82 and 0x547a40..0x547cb4: the fanta 0x6ec/1..4 is born like /0, also without owner (which the step does not
// read). With owner, /1 and /2 lose two life with its action (+0x111) at 1; /3 and /4 shrink the scale by the distance
// to Monster82's left or right hand (0x4010b0) times 0.003f, on the x87 up to the root, before the common step (the
// light, which subtracts d · 0.08f, is overwritten by life · 0.04f); without the hand, nothing. Without the owners'
// states, MissingOwner.
TEST(Cl11Particles, TheOwnedFantasReadTheActionAndTheHandsOfTheirOwner)
{
    const ParticleOwner owner{ParticleOwnerKind::Actor, 7};
    const glm::vec3 left{4, 6, 15};
    const glm::vec3 right{-2, 5, 6};
    for (const std::int32_t subtype : {1, 2, 3, 4})
    {
        for (const std::uint8_t action : {std::uint8_t{0}, std::uint8_t{1}})
        {
            for (const bool hands : {true, false})
            {
                SCOPED_TRACE(testing::Message() << subtype << "/" << int{action} << "/" << hands);
                auto particles = std::make_unique<Particles>();
                CrtRandom random(Seed);
                const auto created = particles->CreateOwnedChecked(0x6ec, {1, 2, 3}, {}, glm::vec3(1.0f), subtype, 3.4f,
                                                                   0.0f, random, 0, owner);
                ASSERT_TRUE(created.HasValue() && created.Value().has_value());
                EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());
                Particle& fanta = Only(*particles, *created.Value());
                EXPECT_EQ(fanta.life, 24);
                EXPECT_EQ(fanta.typedOwner, owner);
                fanta.velocity = glm::vec3(0.0f);
                const auto owners = [&](const ParticleOwner& asked) -> std::optional<ParticleOwnerState>
                {
                    if (asked != owner)
                        return std::nullopt;
                    ParticleOwnerState state{.alive = true, .start = std::nullopt, .action = action};
                    if (hands)
                        state.namedBone = [&](std::string_view name) -> std::optional<glm::vec3>
                        {
                            if (name == "Monster82_LHand")
                                return left;
                            if (name == "Monster82_RHand")
                                return right;
                            return std::nullopt;
                        };
                    return state;
                };
                ASSERT_TRUE(Step(*particles, random, 0.0f, owners).HasValue());
                const std::int32_t life = subtype <= 2 && action == 1 ? 21 : 23;
                EXPECT_EQ(fanta.life, life);
                double scale = 3.4f;
                if (subtype >= 3 && hands)
                {
                    const glm::vec3& hand = subtype == 3 ? left : right;
                    const double dx = 1.0 - hand.x, dy = 2.0 - hand.y, dz = 3.0 - hand.z;
                    const double distance = std::sqrt((dz * dz + dy * dy) + dx * dx);
                    scale = F32(scale - distance * D(0x3b449ba6u));
                }
                EXPECT_EQ(fanta.scale, F32(scale - D(0x3ca3d70au)));
                EXPECT_EQ(fanta.position, glm::vec3(1, 2, 4));
                EXPECT_EQ(fanta.light, glm::vec3(F32(static_cast<double>(life) * D(0x3d23d70au))));
            }
        }
        auto lost = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(
            lost->CreateOwnedChecked(0x6ec, {}, {}, glm::vec3(1.0f), subtype, 1.0f, 0.0f, random, 0, owner).HasValue());
        const auto stepped = Step(*lost, random, 0.0f);
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_EQ(stepped.Error(), ParticleError::MissingOwner);
        auto free = std::make_unique<Particles>();
        ASSERT_TRUE(free->CreateChecked(0x6ec, {}, {}, glm::vec3(1.0f), subtype, 1.0f, 0.0f, random).HasValue());
        EXPECT_EQ(Only(*free, 0).typedOwner, ParticleOwner{});
        EXPECT_TRUE(Step(*free, random, 0.0f).HasValue());
    }
}

// 0x542a94 and 0x545ce3: the blaze 0x5ee is born with life 12 and the requested scale, any subtype, no rand(); its step
// sets the frame (+0x48) to (12 − life) / 3 and 0x5486b6 draws it with 0x548821's fire arguments: a quarter of the
// width, frame % 4 and the turn of +0x20.
TEST(Cl11Particles, TheBlazeLivesTwelveAndWalksItsFourFrames)
{
    for (const std::int32_t subtype : {0, 5})
    {
        SCOPED_TRACE(subtype);
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        const auto created =
            particles->CreateChecked(0x5ee, {1, 2, 3}, {40, 0, 0}, glm::vec3(1.0f), subtype, 3.0f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        Particle& blaze = Only(*particles, *created.Value());
        EXPECT_EQ(blaze.life, 12);
        EXPECT_EQ(blaze.scale, 3.0f);
        EXPECT_EQ(random.State(), CrtRandom(Seed).State());
        for (int step = 1; step <= 11; ++step)
        {
            SCOPED_TRACE(step);
            ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
            EXPECT_EQ(blaze.life, 12 - step);
            EXPECT_EQ(std::bit_cast<std::int32_t>(blaze.field48), step / 3);
            Particle fire = blaze;
            fire.type = fire.texture = 0x5d9;
            fire.subtype = 0;
            const auto drawn = DrawOneParticle(blaze, glm::vec2(64, 64), std::uint8_t{4});
            const auto reference = DrawOneParticle(fire, glm::vec2(64, 64), std::uint8_t{4});
            ASSERT_TRUE(drawn.HasValue());
            ASSERT_TRUE(drawn.Value().has_value());
            ASSERT_TRUE(reference.HasValue());
            ASSERT_TRUE(reference.Value().has_value());
            EXPECT_EQ(drawn.Value()->texture, 0x5ee);
            EXPECT_EQ(drawn.Value()->width, reference.Value()->width);
            EXPECT_EQ(drawn.Value()->height, reference.Value()->height);
            EXPECT_EQ(drawn.Value()->rotation, 40.0f);
            EXPECT_EQ(drawn.Value()->uv, glm::vec2(static_cast<float>((step / 3) % 4) * 0.25f, 0.0f));
            EXPECT_EQ(drawn.Value()->uvSize, glm::vec2(0.25f, 1.0f));
            EXPECT_EQ(drawn.Value()->uvSize, reference.Value()->uvSize);
        }
    }
}

// 0x543939 and 0x5478d7: the cinder 0x6ad/0 (life 24) and /1 (life 15) are born with three rand() (the fall times
// −0.1f, the scale times 0.01f and the turn) and light 0.5; their step raises the vertical speed 0.004f, shrinks 0.02f,
// sets the frame to (23 − life) / 6 and raises the z by the vertical speed times 10; /1 fades with life · 0.1f. Another
// subtype is born with nothing of its own (life 2, no rand()) and shares the step. The common billboard draws it
// (0x54889a).
TEST(Cl11Particles, TheCinderRisesShrinksAndTheJavelinOneFades)
{
    for (const std::int32_t subtype : {0, 1, 2})
    {
        SCOPED_TRACE(subtype);
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        const auto created =
            particles->CreateChecked(0x6ad, {1, 2, 3}, {0, 0, 0}, glm::vec3(0.8f), subtype, 2.0f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        Particle& cinder = Only(*particles, *created.Value());
        if (subtype == 2)
        {
            EXPECT_EQ(cinder.life, 2);
            EXPECT_EQ(cinder.scale, 2.0f);
            EXPECT_EQ(cinder.velocity, glm::vec3(0.0f));
            EXPECT_EQ(cinder.light, glm::vec3(0.8f));
            EXPECT_EQ(random.State(), CrtRandom(Seed).State());
        }
        else
        {
            Draws draws;
            const int fall = draws.Next() % 16 + 32;
            const int size = draws.Next() % 128 + 128;
            const int turn = draws.Next() % 360;
            EXPECT_EQ(cinder.life, subtype == 0 ? 24 : 15);
            EXPECT_EQ(cinder.velocity, glm::vec3(0.0f, F32(static_cast<double>(fall) * D(0xbdcccccdu)), 0.0f));
            EXPECT_EQ(cinder.scale, F32(static_cast<double>(size) * D(0x3c23d70au)));
            EXPECT_EQ(cinder.rotation, static_cast<float>(turn));
            EXPECT_EQ(cinder.light, glm::vec3(0.5f));
            EXPECT_EQ(random.State(), draws.random.State());
        }
        const std::int32_t born = cinder.life;
        for (int step = 1; step < born; ++step)
        {
            SCOPED_TRACE(step);
            const Particle before = cinder;
            ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
            const float rise = F32(static_cast<double>(before.verticalSpeed) + D(0x3b83126fu));
            EXPECT_EQ(cinder.life, born - step);
            EXPECT_EQ(cinder.verticalSpeed, rise);
            EXPECT_EQ(cinder.scale, F32(static_cast<double>(before.scale) - D(0x3ca3d70au)));
            EXPECT_EQ(std::bit_cast<std::int32_t>(cinder.field48), (23 - cinder.life) / 6);
            EXPECT_EQ(cinder.position.z,
                      F32(static_cast<double>(rise) * 10.0 + static_cast<double>(before.position.z)));
            EXPECT_EQ(cinder.position.y,
                      F32(static_cast<double>(before.velocity.y) + static_cast<double>(before.position.y)));
            EXPECT_EQ(cinder.light,
                      subtype == 1 ? glm::vec3(F32(static_cast<double>(cinder.life) * D(0x3dcccccdu))) : before.light);
            const auto drawn = DrawOneParticle(cinder, glm::vec2(64, 64), std::uint8_t{4});
            ASSERT_TRUE(drawn.HasValue());
            ASSERT_TRUE(drawn.Value().has_value());
            EXPECT_EQ(drawn.Value()->texture, 0x6ad);
            EXPECT_EQ(drawn.Value()->rotation, cinder.rotation);
            EXPECT_EQ(drawn.Value()->uv, glm::vec2(0.0f));
            EXPECT_EQ(drawn.Value()->uvSize, glm::vec2(1.0f));
        }
    }
    // 0x524f05 and 0x529c69: with the particles full, neither the cinder nor its rand().
    auto full = std::make_unique<Particles>();
    for (Particle& particle : full->All())
        particle.alive = true;
    CrtRandom random(Seed);
    const auto refused = full->CreateChecked(0x6ad, {1, 2, 3}, {}, glm::vec3(1.0f), 1, 1.0f, 0.0f, random);
    ASSERT_TRUE(refused.HasValue());
    EXPECT_FALSE(refused.Value().has_value());
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
}

// 0x540140 and 0x5449e1: 0x5ac/5 lives 25 without rand() and its step (/1's, through 0x544a19) fades the light by 0.9f
// and shrinks the scale by 0.95f. 0x540151 and 0x544c09: /8 is born and walks like /0 (the cmp by 8, still equal, gives
// it k = 0.5f) and then 0x544dd9 sets its light (scale; scale · 0.5; scale · 0.3f). Both to the common billboard.
TEST(Cl11Particles, TheFlaresFiveAndEightFollowTheTrailAndTheZero)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    const auto five = particles->CreateChecked(0x5ac, {1, 2, 3}, {}, {0.5f, 0.6f, 1.0f}, 5, 0.7f, 0.0f, random);
    ASSERT_TRUE(five.HasValue());
    ASSERT_TRUE(five.Value().has_value());
    Particle& trail = Only(*particles, *five.Value());
    EXPECT_EQ(trail.life, 25);
    EXPECT_EQ(trail.scale, 0.7f);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
    const Particle before = trail;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    EXPECT_EQ(trail.life, 24);
    EXPECT_EQ(trail.position, before.position);
    EXPECT_EQ(trail.scale, F32(static_cast<double>(before.scale) * D(0x3f733333u)));
    for (int channel = 0; channel < 3; ++channel)
        EXPECT_EQ(trail.light[channel], F32(static_cast<double>(before.light[channel]) * D(0x3f666666u)));

    auto eights = std::make_unique<Particles>();
    auto zeros = std::make_unique<Particles>();
    CrtRandom eightRandom(Seed);
    CrtRandom zeroRandom(Seed);
    const auto eight = eights->CreateChecked(0x5ac, {10, 20, 30}, {}, glm::vec3(1.0f), 8, 1.0f, 0.0f, eightRandom);
    const auto zero = zeros->CreateChecked(0x5ac, {10, 20, 30}, {}, glm::vec3(1.0f), 0, 1.0f, 0.0f, zeroRandom);
    ASSERT_TRUE(eight.HasValue() && eight.Value().has_value());
    ASSERT_TRUE(zero.HasValue() && zero.Value().has_value());
    Particle& spark = Only(*eights, *eight.Value());
    const Particle& reference = Only(*zeros, *zero.Value());
    EXPECT_EQ(eightRandom.State(), AfterDraws(Seed, 3).State());
    EXPECT_EQ(eightRandom.State(), zeroRandom.State());
    EXPECT_EQ(spark.life, reference.life);
    EXPECT_EQ(spark.verticalSpeed, reference.verticalSpeed);
    EXPECT_EQ(spark.scale, reference.scale);
    for (int step = 1; step <= 3; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Step(*eights, eightRandom, 0.0f).HasValue());
        ASSERT_TRUE(Step(*zeros, zeroRandom, 0.0f).HasValue());
        EXPECT_EQ(eightRandom.State(), zeroRandom.State());
        EXPECT_EQ(spark.position, reference.position);
        EXPECT_EQ(spark.scale, reference.scale);
        EXPECT_EQ(spark.alive, reference.alive);
        EXPECT_EQ(reference.light, glm::vec3(reference.scale));
        EXPECT_EQ(spark.light, glm::vec3(spark.scale, F32(static_cast<double>(spark.scale) * 0.5),
                                         F32(static_cast<double>(spark.scale) * D(0x3e99999au))));
    }
    for (Particle* flare : {&trail, &spark})
    {
        const auto drawn = DrawOneParticle(*flare, glm::vec2(64, 64), std::uint8_t{4});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->texture, 0x5ac);
        EXPECT_EQ(drawn.Value()->uv, glm::vec2(0.0f));
        EXPECT_EQ(drawn.Value()->uvSize, glm::vec2(1.0f));
    }
}

// 0x540260..0x540316 and 0x544e87: spark 0x5ad rolls the life (rand() & 7) + 20 with any subtype; /3 lives 7 and /4 4,
// both with texture 0x5fd, and /4 turns rand() % 180 − 360. The step enlarges /3 by 1.3f and fades it by 0.9f; /4 jumps
// to the end (0x544edd). Both go to the common billboard with their texture.
TEST(Cl11Particles, TheShineSparksOfTheFootFlashAndBlink)
{
    for (const std::int32_t subtype : {3, 4})
    {
        SCOPED_TRACE(subtype);
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        const auto created =
            particles->CreateChecked(0x5ad, {1, 2, 3}, {}, {1.0f, 0.5f, 0.25f}, subtype, 1.3f, 0.0f, random);
        ASSERT_TRUE(created.HasValue() && created.Value().has_value());
        Particle& spark = Only(*particles, *created.Value());
        Draws draws;
        static_cast<void>(draws.Next());
        EXPECT_EQ(spark.texture, 0x5fd);
        EXPECT_EQ(spark.life, subtype == 3 ? 7 : 4);
        if (subtype == 4)
        {
            EXPECT_EQ(spark.rotation, F32(static_cast<double>(draws.Next() % 180) - 360.0));
        }
        EXPECT_EQ(random.State(), draws.random.State());
        const Particle before = spark;
        ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
        EXPECT_EQ(spark.position, before.position);
        if (subtype == 3)
        {
            EXPECT_EQ(spark.scale, F32(static_cast<double>(before.scale) * D(0x3fa66666u)));
            for (int channel = 0; channel < 3; ++channel)
                EXPECT_EQ(spark.light[channel], F32(static_cast<double>(before.light[channel]) * D(0x3f666666u)));
        }
        else
        {
            EXPECT_EQ(spark.scale, before.scale);
            EXPECT_EQ(spark.light, before.light);
        }
        const auto drawn = DrawOneParticle(spark, glm::vec2(64, 64), std::uint8_t{4});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->texture, 0x5fd);
        EXPECT_EQ(drawn.Value()->rotation, spark.rotation);
    }
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    EXPECT_FALSE(particles->CreateChecked(0x5ad, {}, {}, glm::vec3(1.0f), 5, 1.0f, 0.0f, random).HasValue());
}

// 0x543f49..0x544085 and 0x548000..0x5480ab: mist 0x6f1/1 of 0x1b7 is born like /0 (life 30, turn, scale
// (rand() % 6 + 6) · 0.1f, fall and the shift of ±10, ±10 and ±20) without requesting the scene, whose light it
// overwrites with (0; 0.4f; 0.4f); it lives 50 and rolls another fall: seven rand(). Its step is the mist's (two
// rand()) and also fades the light by 0.95238101f. With the table full, nothing.
TEST(Cl11Particles, TheArmMistIsBornWithoutTheSceneAndFadesEveryStep)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    const auto created = particles->CreateChecked(0x6f1, {100, 200, 300}, {}, glm::vec3(1.0f), 1, 1.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue()) << static_cast<int>(created.Error());
    ASSERT_TRUE(created.Value().has_value());
    Particle& mist = Only(*particles, *created.Value());
    Draws draws;
    EXPECT_EQ(mist.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(mist.scale, F32(static_cast<double>(draws.Next() % 6 + 6) * static_cast<double>(0.1f)));
    static_cast<void>(draws.Next());
    const float x = F32(static_cast<double>(draws.Next() % 20 - 10) + 100.0);
    const float y = F32(static_cast<double>(draws.Next() % 20 - 10) + 200.0);
    const float z = F32(static_cast<double>(draws.Next() % 40 - 20) + 300.0);
    EXPECT_EQ(mist.position, glm::vec3(x, y, z));
    const float cyan = std::bit_cast<float>(0x3ecccccdu);
    EXPECT_EQ(mist.light, glm::vec3(0.0f, cyan, cyan));
    EXPECT_EQ(mist.life, 50);
    EXPECT_EQ(mist.velocity.z, static_cast<float>(-3 - draws.Next() % 3));
    EXPECT_EQ(random.State(), draws.random.State());
    EXPECT_EQ(random.State(), AfterDraws(Seed, 7).State());

    const Particle before = mist;
    ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    EXPECT_EQ(mist.life, 49);
    EXPECT_EQ(mist.scale, F32(static_cast<double>(before.scale) + static_cast<double>(0.03f)));
    EXPECT_EQ(mist.velocity.x, F32(static_cast<double>(draws.Next() % 20 - 10) * static_cast<double>(0.1f)));
    EXPECT_EQ(mist.velocity.y, F32(static_cast<double>(draws.Next() % 20 - 10) * static_cast<double>(0.1f)));
    EXPECT_EQ(mist.velocity.z, F32(static_cast<double>(before.velocity.z) + static_cast<double>(0.1f)));
    for (int channel = 0; channel < 3; ++channel)
        EXPECT_EQ(mist.light[channel], F32(static_cast<double>(before.light[channel]) * D(0x3f73cf3eu)));
    EXPECT_EQ(random.State(), draws.random.State());
    const auto drawn = DrawOneParticle(mist, glm::vec2(64, 64), std::uint8_t{4});
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->texture, 0x6f1);

    for (Particle& particle : particles->All())
        particle.alive = true;
    CrtRandom full(Seed);
    const auto none = particles->CreateChecked(0x6f1, {}, {}, glm::vec3(1.0f), 1, 1.0f, 0.0f, full);
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.State(), Seed);
}

// 0x5443ae and 0x5481ab: wave 0x6eb of any subtype is born with life 7, no rand(); its step fades the light by
// 0.5555556f (0x6ba234) and grows 0.3f (0x6b89dc), and it goes to the common billboard with its texture.
TEST(Cl11Particles, TheShockWaveParticleFadesAndGrows)
{
    for (const std::int32_t subtype : {3, 0, 9})
    {
        SCOPED_TRACE(subtype);
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        const auto created =
            particles->CreateChecked(0x6eb, {1, 2, 3}, {0, 45, 45}, glm::vec3(5.0f), subtype, 0.5f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        Particle& wave = Only(*particles, *created.Value());
        EXPECT_EQ(wave.life, 7);
        EXPECT_EQ(wave.texture, 0x6eb);
        EXPECT_EQ(random.State(), Seed);
        ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
        EXPECT_EQ(wave.life, 6);
        EXPECT_EQ(wave.light, glm::vec3(F32(5.0 * D(0x3f0e38e4u))));
        EXPECT_EQ(wave.scale, F32(0.5 + D(0x3e99999au)));
        EXPECT_EQ(wave.position, glm::vec3(1, 2, 3));
        EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State()); // only the wind
        const auto drawn = DrawOneParticle(wave, glm::vec2(64, 64), std::uint8_t{4});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->texture, 0x6eb);
    }
}

// 0x542d75..0x542ecf: drop 0x607 /1 to /4 lives rand() % 10 + 30 with light 1 and scale (rand() % 6 + 4) · 0.03f; /3
// adds it to the requested one and rolls another equal one for the rise (three rand()). The steps (0x547029, 0x5470a0,
// 0x5470d3, 0x54714a): /1 raises the frame, walks (rand() % 30 − 15) times the scale in x and y, rises
// (rand() % 20 + 10) · scale · 0.3f and grows 0.002f; /2 and /4 rise (rand() % 20 + 10) · 0.3f and grow 0.005f, and /4
// stops at 350 with life at 0; /3 raises the frame and walks like /0 with its rise instead of the scale.
TEST(Cl11Particles, TheDropsBubbleFloatRiseAndStop)
{
    const auto walk = [](int steps, double by, float from)
    { return F32(static_cast<double>(steps) * by + static_cast<double>(from)); };
    for (const std::int32_t subtype : {1, 2, 3, 4})
    {
        SCOPED_TRACE(subtype);
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        const float asked = 0.1f;
        const auto created =
            particles->CreateChecked(0x607, {10, 20, 30}, {}, glm::vec3(0.25f), subtype, asked, 0.0f, random);
        ASSERT_TRUE(created.HasValue() && created.Value().has_value());
        Particle& drop = Only(*particles, *created.Value());
        Draws draws;
        EXPECT_EQ(drop.life, draws.Next() % 10 + 30);
        const double scaleStep = static_cast<double>(draws.Next() % 6 + 4) * D(0x3cf5c28fu);
        EXPECT_EQ(drop.light, glm::vec3(1.0f));
        if (subtype == 3)
        {
            EXPECT_EQ(drop.scale, F32(scaleStep + static_cast<double>(asked)));
            EXPECT_EQ(drop.verticalSpeed, F32(static_cast<double>(draws.Next() % 6 + 4) * D(0x3cf5c28fu)));
        }
        else
        {
            EXPECT_EQ(drop.scale, F32(scaleStep));
        }
        EXPECT_EQ(random.State(), draws.random.State());
        if (subtype == 4)
            drop.position.z = 349.0f;
        const Particle before = drop;
        ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue());
        static_cast<void>(draws.Next());
        static_cast<void>(draws.Next());
        const auto frame = std::bit_cast<std::uint32_t>(drop.field48);
        const auto was = std::bit_cast<std::uint32_t>(before.field48);
        switch (subtype)
        {
        case 1:
        {
            EXPECT_EQ(frame, was + 1u);
            const double scale = static_cast<double>(before.scale);
            EXPECT_EQ(drop.position.x, walk(draws.Next() % 30 - 15, scale, before.position.x));
            EXPECT_EQ(drop.position.y, walk(draws.Next() % 30 - 15, scale, before.position.y));
            EXPECT_EQ(drop.position.z, walk(draws.Next() % 20 + 10, scale * D(0x3e99999au), before.position.z));
            EXPECT_EQ(drop.scale, F32(scale + D(0x3b03126fu)));
            break;
        }
        case 2:
        case 4:
        {
            EXPECT_EQ(frame, was);
            EXPECT_EQ(drop.position.x, before.position.x);
            const float risen = walk(draws.Next() % 20 + 10, D(0x3e99999au), before.position.z);
            EXPECT_EQ(drop.scale, F32(static_cast<double>(before.scale) + D(0x3ba3d70au)));
            if (subtype == 4)
            {
                // 0x54714a: past z 350 it leaves it at 350 and the life at 0, without touching the alive byte.
                ASSERT_GT(risen, 350.0f);
                EXPECT_EQ(drop.position.z, 350.0f);
                EXPECT_EQ(drop.life, 0);
                EXPECT_TRUE(drop.alive);
            }
            else
            {
                EXPECT_EQ(drop.position.z, risen);
                EXPECT_EQ(drop.life, before.life - 1);
            }
            break;
        }
        default:
        {
            EXPECT_EQ(frame, was + 1u);
            const double rise = static_cast<double>(before.verticalSpeed) * 2.5;
            EXPECT_EQ(drop.position.x, walk(draws.Next() % 20 - 10, rise, before.position.x));
            EXPECT_EQ(drop.position.y, walk(draws.Next() % 20 - 10, rise, before.position.y));
            EXPECT_EQ(drop.position.z, walk(draws.Next() % 20 + 10, rise, before.position.z));
            EXPECT_EQ(drop.scale, before.scale);
            break;
        }
        }
        EXPECT_EQ(random.State(), draws.random.State());
    }
    {
        // A /4 that does not pass 350 rises and loses one life, like /2.
        auto low = std::make_unique<Particles>();
        CrtRandom lowRandom(Seed);
        const auto made = low->CreateChecked(0x607, {10, 20, 30}, {}, glm::vec3(0.25f), 4, 0.1f, 0.0f, lowRandom);
        ASSERT_TRUE(made.HasValue() && made.Value().has_value());
        const Particle& drop = low->All()[*made.Value()];
        const Particle before = drop;
        CrtRandom replay = lowRandom;
        ASSERT_TRUE(Step(*low, lowRandom, 0.0f).HasValue());
        static_cast<void>(replay.Next());
        static_cast<void>(replay.Next());
        const float risen = walk(replay.Next() % 20 + 10, D(0x3e99999au), before.position.z);
        ASSERT_LE(risen, 350.0f);
        EXPECT_EQ(drop.position.z, risen);
        EXPECT_EQ(drop.life, before.life - 1);
        EXPECT_TRUE(drop.alive);
        EXPECT_EQ(lowRandom.State(), replay.State());
    }
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    const auto outside = particles->CreateChecked(0x607, {}, {}, glm::vec3(1.0f), 5, 1.0f, 0.0f, random);
    ASSERT_FALSE(outside.HasValue());
    EXPECT_EQ(outside.Error(), ParticleError::UnsupportedSubtype);
}

TEST(Cl11Particles, TheNewTypesGoToTheirBillboards)
{
    using Kind = std::tuple<std::int32_t, std::int32_t, bool>;
    for (const auto& [type, subtype, rotated] :
         {Kind{0x600, 1, false}, Kind{0x5c6, 4, false}, Kind{0x5de, 2, false}, Kind{0x6f4, 4, false},
          Kind{0x5d5, 2, false}, Kind{0x5dc, 13, true}, Kind{0x5d9, 7, true}, Kind{0x5db, 11, true}})
    {
        Particle particle{.alive = true,
                          .type = type,
                          .texture = type,
                          .subtype = subtype,
                          .scale = 1.0f,
                          .light = glm::vec3(1.0f),
                          .rotation = 30.0f};
        const auto drawn = DrawOneParticle(particle, glm::vec2(64, 64), std::uint8_t{4});
        ASSERT_TRUE(drawn.HasValue()) << type << "/" << subtype;
        ASSERT_TRUE(drawn.Value().has_value()) << type << "/" << subtype;
        if (type == 0x5dc || type == 0x5d9 || type == 0x5db)
        {
            EXPECT_EQ(drawn.Value()->rotation, rotated ? 30.0f : 0.0f) << type << "/" << subtype;
        }
    }
}

// 0x542ad3..0x542b06 and 0x546c0f..0x546c9e: 0x5fc/2 of the shadow rain (0x5254f9) lives 25, turns rand() % 360 and
// leaves with (−0.1f; −0.5; −1). Its step, after the common movement (turned with x at 45), turns −12, lowers the
// velocity's z by (rand() & 7) · 0.05f, adds it again unturned with the z retained, fades the light by 0x3f73cf3e and
// leaves flare01 (0x5ac) with the scale times 2/3 in the frame's sprite table (0x5491e0).
TEST(Cl11Particles, TheFallingShineDriftsDownAndLeavesAFlareEachStep)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    const glm::vec3 from{100, 200, 300};
    const float asked = 0.6f;
    const auto created = particles->CreateChecked(0x5fc, from, {10, 20, 30}, glm::vec3(0.5f), 2, asked, 0.0f, random);
    ASSERT_TRUE(created.HasValue() && created.Value().has_value());
    Particle& shine = Only(*particles, *created.Value());
    Draws draws;
    EXPECT_EQ(shine.life, 25);
    EXPECT_EQ(shine.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(shine.angles, glm::vec3(45, 20, 30));
    EXPECT_EQ(shine.scale, asked);
    EXPECT_EQ(shine.velocity, glm::vec3(std::bit_cast<float>(0xbdcccccdu), -0.5f, -1.0f));
    EXPECT_EQ(random.State(), draws.random.State());

    PortableParticleTrigonometry trigonometry;
    const std::int32_t map = 0;
    const float clock = 0.0f;
    const auto step = [&](Client::Particles::FrameSprites* sprites)
    {
        return Client::Particles::StepParticlesChecked(*particles, random, std::cref(clock), &map, trigonometry, {}, {},
                                                       {}, {}, nullptr, {}, {}, {}, {}, sprites);
    };
    const auto expectTheStep = [&](const Particle& before)
    {
        static_cast<void>(draws.Next());
        static_cast<void>(draws.Next());
        const glm::vec3 turned =
            Client::Math::OriginalVectorRotate(before.velocity, Client::Math::OriginalAngleMatrix(before.angles));
        glm::vec3 moved{};
        for (int axis = 0; axis < 3; ++axis)
            moved[axis] = F32(static_cast<double>(turned[axis]) + static_cast<double>(before.position[axis]));
        const double vertical =
            static_cast<double>(before.velocity.z) - (static_cast<double>(draws.Next() & 7) * D(0x3d4ccccdu));
        EXPECT_EQ(shine.life, before.life - 1);
        EXPECT_EQ(shine.rotation, F32(static_cast<double>(before.rotation) - 12.0));
        EXPECT_EQ(shine.velocity, glm::vec3(before.velocity.x, before.velocity.y, F32(vertical)));
        EXPECT_EQ(shine.position, glm::vec3(F32(static_cast<double>(before.velocity.x) + moved.x),
                                            F32(static_cast<double>(before.velocity.y) + moved.y),
                                            F32(vertical + static_cast<double>(moved.z))));
        EXPECT_EQ(shine.light, glm::vec3(F32(static_cast<double>(before.light.x) * D(0x3f73cf3eu))));
        EXPECT_EQ(shine.scale, before.scale);
        EXPECT_EQ(random.State(), draws.random.State());
    };
    auto sprites = std::make_unique<Client::Particles::FrameSprites>();
    const Particle first = shine;
    ASSERT_TRUE(step(sprites.get()).HasValue());
    expectTheStep(first);
    const Client::Particles::Sprite& flare = sprites->Sprites()[0];
    EXPECT_TRUE(flare.alive);
    EXPECT_EQ(flare.texture, 0x5ac);
    EXPECT_EQ(flare.position, shine.position);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(flare.scale), 0x3eccccceu); // 0.6f · 0x3f2aaaab, computed separately
    EXPECT_EQ(flare.light, shine.light);
    EXPECT_EQ(flare.rotation, 0.0f);
    EXPECT_EQ(flare.blend, 0);
    EXPECT_FALSE(sprites->Sprites()[1].alive);

    // Without the sprite table, it cuts before the sprite, with everything before done.
    const Particle second = shine;
    const auto cut = step(nullptr);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_EQ(cut.Error(), ParticleError::MissingSprites);
    expectTheStep(second);

    // With the 3000 slots full, neither the particle nor its rand().
    auto full = std::make_unique<Particles>();
    for (Particle& particle : full->All())
        particle.alive = true;
    CrtRandom untouched(Seed);
    const auto refused = full->CreateChecked(0x5fc, from, {}, glm::vec3(1.0f), 2, asked, 0.0f, untouched);
    ASSERT_TRUE(refused.HasValue());
    EXPECT_FALSE(refused.Value().has_value());
    EXPECT_EQ(untouched.State(), CrtRandom(Seed).State());
}
} // namespace
