#include "World/Particles.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/EffectParticleKernels.h"
#include "World/KalimaParticleKernels.h"
#include "World/ParticleOwner.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/TerrainLighting.h"
#include "World/WorldObjectStepServices.h"

#include "support/ParticleDrawRecorder.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>
#include <tuple>
#include <utility>

// Las partículas de la CL11 (0x53fe70, 0x5446e0 y 0x548550): oráculos escritos desde el ASM, con los rand() contados
// contra Client::Tests::AfterDraws.
namespace
{
using Client::Render::Models::CrtRandom;
using Client::Tests::AfterDraws;
using Client::Tests::DrawOneParticle;
using Client::World::Particle;
using Client::World::ParticleError;
using Client::World::ParticleOwner;
using Client::World::ParticleOwnerKind;
using Client::World::ParticleOwnerState;
using Client::World::Particles;
using Client::World::PortableParticleTrigonometry;

constexpr std::uint32_t Seed = 0x1234;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(std::uint32_t bits)
{
    return static_cast<double>(std::bit_cast<float>(bits));
}

// Los rand() de la semilla, en orden.
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

// 0x5446e0 con todos los servicios del juego menos la luz dinámica y los dueños.
Client::Core::Expected<void, ParticleError> Step(Particles& particles, CrtRandom& random, float clock,
                                                 const Client::World::ParticleOwnerStates& owners = {},
                                                 Client::World::TerrainLighting* lighting = nullptr)
{
    PortableParticleTrigonometry trigonometry;
    const std::int32_t map = 0;
    return Client::World::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry, {}, {}, {}, {},
                                               lighting, {}, {}, {}, owners);
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

    // 0x5473f1 con la vida 14: la escala del seno de +0x50 (0), y +0x50 = (15 − 14) · 6.
    CrtRandom stepRandom(Seed);
    ASSERT_TRUE(Step(*particles, stepRandom, 0.0f).HasValue());
    EXPECT_EQ(ring.life, 14);
    EXPECT_EQ(ring.scale, F32(std::sin(0.0) * 3.0 + 1.5));
    EXPECT_EQ(ring.verticalSpeed, 6.0f);
    EXPECT_EQ(stepRandom.State(), AfterDraws(Seed, 2).State()); // sólo el viento
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
    // (15 − 5) · 6 + 40 = 100 > 90; la vida 5 apaga la luz a la mitad.
    EXPECT_EQ(ring.verticalSpeed, 90.0f);
    EXPECT_EQ(ring.light, glm::vec3(0.5f));
    EXPECT_EQ(ring.scale, F32(std::sin(40.0 * D(0x3c8efa35u)) * 3.0 + 1.5));
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
    EXPECT_EQ(ring.scale, F32(std::sin(19.0 * D(0x3e32b8c2u)) * 3.0 + 2.0));
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
    static_cast<void>(draws.Next()); // la velocidad común, que la /13 pisa
    EXPECT_EQ(fire.life, 20);
    EXPECT_EQ(fire.velocity.x, 0.0f);
    EXPECT_EQ(fire.velocity.y, 0.0f);
    EXPECT_EQ(fire.velocity.z, F32(static_cast<double>((draws.Next() & 127) + 128) * D(0x3e19999au)));
    static_cast<void>(draws.Next()); // el giro que pisa el final
    EXPECT_EQ(fire.scale, F32(static_cast<double>((draws.Next() & 15) + 150) * D(0x3c449ba6u)));
    EXPECT_EQ(fire.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(random.State(), AfterDraws(Seed, 5).State());
}

TEST(Cl11Particles, EveryFireSubtypeIsCreatedAndStepped)
{
    for (const std::int32_t type : {0x5d9, 0x5db, 0x5dc})
        for (std::int32_t subtype = -1; subtype <= 15; ++subtype)
        {
            if (subtype == Client::World::FireRingSubtype)
                continue;
            auto particles = std::make_unique<Particles>();
            CrtRandom random(Seed);
            ASSERT_TRUE(particles->CreateChecked(type, {}, {}, glm::vec3(1.0f), subtype, 1.0f, 0.0f, random).HasValue())
                << type << "/" << subtype;
            ASSERT_TRUE(Step(*particles, random, 0.0f).HasValue()) << type << "/" << subtype;
            // Lo común de 0x545958: el cuadro (23 − 23) / 6.
            EXPECT_EQ(std::bit_cast<std::int32_t>(Only(*particles, 0).field48), 0);
        }
}

// 0x5410ea y 0x545750: la 5ca/1 vive 10 con la luz 0,5 y su paso la apaga con la vida y la agranda 0,1f.
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

// 0x541112..0x541167 y 0x54577c: la 5ca/2 cambia la textura a 0x61e, vive 10, saca un tercer rand() para la escala
// (+ 0,3f) y queda sin giro; su paso baja la luz 0,01f por canal.
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

    auto lighting = std::make_unique<Client::World::TerrainLighting>();
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

    // El dueño muerto lo deja en vida 5; se movió (1, 2, 3) desde su partida.
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
    // Buscar una semilla con rand() % 50 == 0 y otra sin.
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
    // El paso la apaga, pero su caso corre igual: viento (2) y el verde (1) de cada una.
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
    const double glow = std::sin(1234.0 * D(0x3b03126fu)) * D(0x3e99999au) + D(0x3f333333u);
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
        EXPECT_EQ(splash.position.x, F32(std::cos(2.0) * 20.0 + 5.0));
        EXPECT_EQ(splash.position.y, F32(std::sin(2.0) * 20.0 + 6.0));
        EXPECT_EQ(splash.position.z, F32(static_cast<double>(speed) + 7.0));
        EXPECT_EQ(splash.rotation, F32(static_cast<double>(turn) + 1.0));
        // La 6f4/4 sigue con el apagado de la 6f4 (vida 5 < 8); las otras, por 0,9090909f.
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
} // namespace
