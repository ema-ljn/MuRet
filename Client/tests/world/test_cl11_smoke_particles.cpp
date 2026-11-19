#include "World/SmokeParticleKernels.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"

#include "support/ParticleDrawRecorder.h"
#include "support/PlayerActors.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <optional>

// Los humos 5f2 de la CL11 (SmokeParticleKernels): oráculos escritos desde el ASM de 0x53fe70 y 0x5446e0, con los
// rand() contados contra Client::Tests::AfterDraws.
namespace
{
using Client::Render::Models::CrtRandom;
using Blend = Client::Tests::RecordedParticleDraw::Blend;
using Client::Tests::AfterDraws;
using Client::World::Particle;
using Client::World::ParticleError;
using Client::World::ParticleOwner;
using Client::World::ParticleOwnerKind;
using Client::World::ParticleOwnerState;
using Client::World::Particles;
using Client::World::PortableParticleTrigonometry;

constexpr std::uint32_t Seed = 0x1234;
constexpr std::int32_t Smoke = 0x5f2;

float F32(double value)
{
    return static_cast<float>(value);
}

double D(std::uint32_t bits)
{
    return static_cast<double>(std::bit_cast<float>(bits));
}

double D(float value)
{
    return static_cast<double>(value);
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

// 0x5446e0 con el reloj, la trigonometría y los dueños; los dos primeros rand() son el viento.
Client::Core::Expected<void, ParticleError> Step(Particles& particles, CrtRandom& random,
                                                 const Client::World::ParticleOwnerStates& owners = {})
{
    PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    return Client::World::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry, {}, {}, {}, {},
                                               nullptr, {}, {}, {}, owners);
}

Particle& Create(Particles& particles, CrtRandom& random, std::int32_t subtype, const glm::vec3& light, float scale,
                 float clock, const glm::vec3& position = {})
{
    const auto created = particles.CreateChecked(Smoke, position, {}, light, subtype, scale, clock, random);
    EXPECT_TRUE(created.HasValue()) << subtype;
    return Only(particles, 0);
}

// 0x5418b1: la /17 guarda la luz en +0x60..+0x68, vive 12 y saca dos rand() (la escala y el ángulo x); el giro es
// _ftol(reloj) % 360. 0x546171: l = vida · 0,125, que pasado 1 se vuelve 1 − l, por la luz guardada, y sube como el
// aliento.
TEST(Cl11SmokeParticles, TheGlowSmokeKeepsItsLightAndFoldsItsFade)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    const glm::vec3 light{1.0f, 0.2f, 0.5f};
    Particle& glow = Create(*particles, random, 17, light, 2.0f, 1234.5f, {0, 0, 100});
    Draws draws;
    EXPECT_EQ(glow.life, 12);
    EXPECT_EQ(glow.rotationFactor, 1.0f);
    EXPECT_EQ(glow.field64, glm::vec2(0.2f, 0.5f));
    EXPECT_EQ(glow.scale, F32((static_cast<double>((draws.Next() & 31) + 8) * 2.0) * D(0x3c23d70au)));
    EXPECT_EQ(glow.angles.x, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(glow.rotation, 154.0f);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());

    const float scale = glow.scale;
    CrtRandom step(Seed);
    ASSERT_TRUE(Step(*particles, step).HasValue());
    const double folded = 1.0 - 11.0 * 0.125;
    EXPECT_EQ(glow.light, glm::vec3(F32(folded), F32(folded * D(0.2f)), F32(folded * D(0.5f))));
    const double rise = D(0x3e4ccccdu);
    EXPECT_EQ(glow.verticalSpeed, F32(rise));
    EXPECT_EQ(glow.position.z, F32(rise + 100.0));
    EXPECT_EQ(glow.scale, F32(D(scale) + D(0x3d4ccccdu)));
    EXPECT_EQ(step.State(), AfterDraws(Seed, 2).State()); // sólo el viento

    glow.life = 5;
    ASSERT_TRUE(Step(*particles, step).HasValue());
    EXPECT_EQ(glow.light, glm::vec3(0.5f, F32(0.5 * D(0.2f)), 0.25f));
}

// 0x54228c y 0x542307: la /19 y la /20 viven 40 (la /19 guarda la luz). 0x546573 y 0x54661e: la luz gris vida ·
// 0,025f (por 0,5 la /19) y cuatro rand(): la subida, la x, la y y la escala, que también es el giro.
TEST(Cl11SmokeParticles, TheShakingSmokesDrawFourRandsEachStep)
{
    for (const std::int32_t subtype : {19, 20})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        Particle& smoke = Create(*particles, random, subtype, {0.3f, 0.4f, 0.5f}, 1.5f, 400.0f);
        Draws draws;
        EXPECT_EQ(smoke.life, 40) << subtype;
        EXPECT_EQ(smoke.rotationFactor, subtype == 19 ? 0.3f : 0.0f) << subtype;
        EXPECT_EQ(smoke.scale, F32((static_cast<double>((draws.Next() & 31) + 8) * 1.5) * D(0x3c23d70au))) << subtype;
        EXPECT_EQ(smoke.angles.x, static_cast<float>(draws.Next() % 360)) << subtype;
        EXPECT_EQ(smoke.rotation, 40.0f) << subtype;

        smoke.verticalSpeed = 1.0f;
        smoke.position = {10.0f, 20.0f, 30.0f};
        smoke.scale = 1.0f;
        CrtRandom step(Seed);
        ASSERT_TRUE(Step(*particles, step).HasValue());
        Draws stepped;
        static_cast<void>(stepped.Next());
        static_cast<void>(stepped.Next()); // el viento
        const double gray = 39.0 * D(0x3ccccccdu) * (subtype == 19 ? 0.5 : 1.0);
        EXPECT_EQ(smoke.light, glm::vec3(F32(gray))) << subtype;
        const float rise = F32(static_cast<double>(stepped.Next() % 20) * D(0x3b03126fu) + 1.0);
        EXPECT_EQ(smoke.verticalSpeed, rise) << subtype;
        EXPECT_EQ(smoke.position.x, F32(static_cast<double>(stepped.Next() % 100 - 20) * D(0x3c23d70au) + 10.0))
            << subtype;
        EXPECT_EQ(smoke.position.y, F32(static_cast<double>(stepped.Next() % 100 - 20) * D(0x3c23d70au) + 20.0))
            << subtype;
        EXPECT_EQ(smoke.position.z, F32(30.0 + D(rise))) << subtype;
        const float grown = F32(static_cast<double>(stepped.Next() % 10) * D(0x3a83126fu) + 1.0);
        EXPECT_EQ(smoke.scale, grown) << subtype;
        EXPECT_EQ(smoke.rotation, grown) << subtype;
        EXPECT_EQ(step.State(), AfterDraws(Seed, 6).State()) << subtype;
    }
}

// 0x54184b: la /23 nace como el aliento (vida 16) y 0x5460a0 la ilumina (l · 0,1f; l; l · 0,6f) con l = vida · 0,125.
// 0x542450: la /24 vive 32 con la escala (((rand() & 31) + 48) · 0,01f + 1) · escala, y 0x54675f la ilumina con
// l = vida · 0,03125 (l · 0,2f; l · 0,5; l · 0,35f) y sube 0,1f.
TEST(Cl11SmokeParticles, TheWarmBreathAndTheStoneSmokeRiseWithTheirOwnLight)
{
    PortableParticleTrigonometry trigonometry;
    const float clock = 725.9f;
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(
            particles
                ->CreateChecked(Smoke, {}, {}, glm::vec3(1.0f), 23, 1.0f, std::cref(clock), random, 0, trigonometry)
                .HasValue());
        Draws draws;
        Particle& breath = Only(*particles, 0);
        EXPECT_EQ(breath.life, 16);
        EXPECT_EQ(breath.scale, F32(static_cast<double>((draws.Next() & 31) + 48) * D(0x3c23d70au)));
        EXPECT_EQ(breath.angles.x, static_cast<float>(draws.Next() % 360));
        EXPECT_EQ(breath.rotation, 5.0f);
        ASSERT_TRUE(Step(*particles, random).HasValue());
        const double warm = 15.0 * 0.125;
        EXPECT_EQ(breath.light, glm::vec3(F32(warm * D(0x3dcccccdu)), F32(warm), F32(warm * D(0x3f19999au))));
        EXPECT_EQ(breath.verticalSpeed, F32(D(0x3e4ccccdu)));
    }
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        Particle& stone = Create(*particles, random, 24, glm::vec3(1.0f), 2.0f, clock, {0, 0, 50});
        Draws draws;
        EXPECT_EQ(stone.life, 32);
        EXPECT_EQ(stone.scale, F32((static_cast<double>((draws.Next() & 31) + 48) * D(0x3c23d70au) + 1.0) * 2.0));
        EXPECT_EQ(stone.angles.x, static_cast<float>(draws.Next() % 360));
        EXPECT_EQ(stone.rotation, 5.0f);
        const float scale = stone.scale;
        ASSERT_TRUE(Step(*particles, random).HasValue());
        const double ash = 31.0 * 0.03125;
        EXPECT_EQ(stone.light, glm::vec3(F32(ash * D(0x3e4ccccdu)), F32(ash * 0.5), F32(ash * D(0x3eb33333u))));
        EXPECT_EQ(stone.verticalSpeed, F32(D(0x3dcccccdu)));
        EXPECT_EQ(stone.position.z, F32(D(0x3dcccccdu) + 50.0));
        EXPECT_EQ(stone.scale, F32(D(scale) + D(0x3d4ccccdu)));
    }
}

// 0x54252c: la /26 vive 25, con la escala ((rand() & 63) + 64) · 0,01f, el giro y la subida ((rand() & 31) + 60) ·
// 0,1f; 0x5467ed la apaga por vida · 0,05f y la agranda 0,15f. 0x5426cf: la /31 conserva la escala del pedido y vive
// 15; 0x546905 la achica 0,05f, la apaga por 0,9090909f y la mata con la escala y el azul en 0 o menos.
TEST(Cl11SmokeParticles, TheFadingSmokeGrowsAndTheShrinkingOneDies)
{
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        Particle& smoke = Create(*particles, random, 26, glm::vec3(1.0f), 1.0f, 0.0f);
        Draws draws;
        EXPECT_EQ(smoke.life, 25);
        EXPECT_EQ(smoke.scale, F32(static_cast<double>((draws.Next() & 63) + 64) * D(0x3c23d70au)));
        EXPECT_EQ(smoke.rotation, static_cast<float>(draws.Next() % 360));
        EXPECT_EQ(smoke.verticalSpeed, F32(static_cast<double>((draws.Next() & 31) + 60) * D(0x3dcccccdu)));
        EXPECT_EQ(random.State(), AfterDraws(Seed, 3).State());
        const float scale = smoke.scale;
        ASSERT_TRUE(Step(*particles, random).HasValue());
        EXPECT_EQ(smoke.light, glm::vec3(F32(24.0 * D(0x3d4ccccdu))));
        EXPECT_EQ(smoke.scale, F32(D(scale) + D(0x3e19999au)));
    }
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        Particle& smoke = Create(*particles, random, 31, glm::vec3(1.0f), 0.12f, 0.0f);
        Draws draws;
        EXPECT_EQ(smoke.life, 15);
        EXPECT_EQ(smoke.scale, 0.12f);
        EXPECT_EQ(smoke.rotation, static_cast<float>(draws.Next() % 360));
        EXPECT_EQ(smoke.verticalSpeed, F32(static_cast<double>((draws.Next() & 31) + 60) * D(0x3dcccccdu)));
        EXPECT_EQ(random.State(), AfterDraws(Seed, 2).State());
        ASSERT_TRUE(Step(*particles, random).HasValue());
        EXPECT_EQ(smoke.scale, F32(D(0.12f) - D(0x3d4ccccdu)));
        EXPECT_EQ(smoke.light, glm::vec3(F32(D(0x3f68ba2eu))));
        EXPECT_TRUE(smoke.alive);

        smoke.scale = 0.04f;
        ASSERT_TRUE(Step(*particles, random).HasValue());
        EXPECT_TRUE(smoke.alive); // el azul sigue arriba de 0
        smoke.light.z = 0.0f;
        ASSERT_TRUE(Step(*particles, random).HasValue());
        EXPECT_FALSE(smoke.alive);
    }
}

// 0x54196c..0x541a00: la /36 (la de 0x52497d, con el efecto de dueño) tiene la luz (1; 0,3f; 0), vida 25 y cinco
// rand(): el giro, el ángulo x y la partida. 0x545ef9: con la vida en 23 o más, la posición del dueño más la partida;
// después sube con la escala · 0,1f en la z y en la y y se achica 0,08f. El giro suma 0,01f.
TEST(Cl11SmokeParticles, TheTrailSmokeFollowsItsEffectTwoStepsAndThenRises)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    const ParticleOwner owner{ParticleOwnerKind::ModelEffect, 2};
    ASSERT_TRUE(
        particles->CreateOwnedChecked(Smoke, {}, {}, glm::vec3(0.5f), 36, 1.5f, 0.0f, random, 0, owner).HasValue());
    Draws draws;
    Particle& trail = Only(*particles, 0);
    EXPECT_EQ(trail.light, glm::vec3(1.0f, std::bit_cast<float>(0x3e99999au), 0.0f));
    EXPECT_EQ(trail.life, 25);
    const float turn = static_cast<float>(draws.Next() % 360);
    EXPECT_EQ(trail.rotation, turn);
    EXPECT_EQ(trail.angles.x, static_cast<float>(draws.Next() % 360));
    const glm::vec3 start{static_cast<float>(draws.Next() % 50 - 25), static_cast<float>(draws.Next() % 50 - 25),
                          -static_cast<float>(draws.Next() % 50)};
    EXPECT_EQ(trail.start, start);
    EXPECT_EQ(trail.typedOwner, owner);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 5).State());

    const auto owners = [](const ParticleOwner&)
    { return std::optional(ParticleOwnerState{true, {100, 200, 300}, {}}); };
    ASSERT_TRUE(Step(*particles, random, owners).HasValue());
    EXPECT_EQ(trail.position, glm::vec3(100.0f, 200.0f, 300.0f) + start);
    EXPECT_EQ(trail.rotation, F32(D(turn) + D(0x3c23d70au)));
    EXPECT_EQ(Step(*particles, random).Error(), ParticleError::MissingOwner);

    trail.verticalSpeed = 2.0f;
    const glm::vec3 before = trail.position;
    ASSERT_TRUE(Step(*particles, random).HasValue()); // vida 22: ya no lee al dueño
    const double rise = 1.5 * D(0x3dcccccdu) + 2.0;
    EXPECT_EQ(trail.verticalSpeed, F32(rise));
    EXPECT_EQ(trail.position, glm::vec3(before.x, F32(rise + D(before.y)), F32(rise + D(before.z))));
    EXPECT_EQ(trail.scale, F32(1.5 - D(0x3da3d70au)));
}

// 0x541932..0x541967: la /37 (la de 0x5249a0) guarda la luz del pedido como dirección en +0x60..+0x68, la luz pasa a
// blanca y la z y la y retroceden 15 por el azul y el verde; después, lo de la /36. 0x545f68: la z y la y retroceden
// la dirección por 0,8f, la escala crece 0,08f y la luz se apaga por 0,9708738f.
TEST(Cl11SmokeParticles, TheBackTrailTakesTheLightAsItsDirection)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    Particle& trail = Create(*particles, random, 37, {0.5f, 2.0f, 4.0f}, 1.0f, 0.0f, {7.0f, 100.0f, 200.0f});
    EXPECT_EQ(trail.rotationFactor, 0.5f);
    EXPECT_EQ(trail.field64, glm::vec2(2.0f, 4.0f));
    EXPECT_EQ(trail.light, glm::vec3(1.0f));
    EXPECT_EQ(trail.position, glm::vec3(7.0f, 70.0f, 140.0f));
    EXPECT_EQ(trail.life, 25);
    EXPECT_EQ(random.State(), AfterDraws(Seed, 5).State());
    const float turn = trail.rotation;
    ASSERT_TRUE(Step(*particles, random).HasValue());
    EXPECT_EQ(trail.position, glm::vec3(7.0f, F32(70.0 - 2.0 * D(0x3f4ccccdu)), F32(140.0 - 4.0 * D(0x3f4ccccdu))));
    EXPECT_EQ(trail.scale, F32(1.0 + D(0x3da3d70au)));
    EXPECT_EQ(trail.rotation, F32(D(turn) + D(0x3c23d70au)));
    EXPECT_EQ(trail.light, glm::vec3(F32(D(0x3f788b30u))));
}

// 0x542a01 (/0, vida 55), 0x5429a2 (/1, vida 30) y 0x542943 (/2, vida 55): la 0x5f5 guarda la luz, saca el ángulo x
// (rand() & 3) − 2 y gira _ftol(reloj) % 360. 0x546a9f, 0x546a54 y 0x546a13: la /0 baja el rojo guardado (0,015f y
// 0,005f más con el verde arriba de 0,1f), la /2 el verde y el azul; la luz es vida · 0,0181818f por la guardada, la
// subida suma 0,2f (la /1, 0,1f), la escala 0,03f y el giro el ángulo x.
TEST(Cl11SmokeParticles, ThePuffKeepsItsLightTiltsAndRises)
{
    constexpr std::int32_t Puff = 0x5f5;
    struct Case
    {
        std::int32_t subtype;
        std::int32_t life;
        glm::vec3 kept;
        double rise;
    };
    const glm::vec3 light{0.5f, 0.4f, 0.3f};
    for (const Case& value :
         {Case{0, 55, {F32(D(F32(D(0.5f) - D(0x3c75c28fu))) - D(0x3ba3d70au)), 0.4f, 0.3f}, D(0x3e4ccccdu)},
          Case{1, 30, light, D(0x3dcccccdu)},
          Case{2, 55, {0.5f, F32(D(0.4f) - D(0x3ba3d70au)), F32(D(0.3f) - D(0x3c75c28fu))}, D(0x3e4ccccdu)}})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        ASSERT_TRUE(
            particles->CreateChecked(Puff, {0, 0, 10}, {}, light, value.subtype, 1.0f, 1000.0f, random).HasValue());
        Draws draws;
        Particle& puff = Only(*particles, 0);
        EXPECT_EQ(puff.life, value.life) << value.subtype;
        EXPECT_EQ(puff.rotationFactor, 0.5f) << value.subtype;
        EXPECT_EQ(puff.field64, glm::vec2(0.4f, 0.3f)) << value.subtype;
        const float tilt = F32(static_cast<double>(draws.Next() & 3) - 2.0);
        EXPECT_EQ(puff.angles.x, tilt) << value.subtype;
        EXPECT_EQ(puff.rotation, 280.0f) << value.subtype;
        EXPECT_EQ(random.State(), AfterDraws(Seed, 1).State()) << value.subtype;

        ASSERT_TRUE(Step(*particles, random).HasValue()) << value.subtype;
        const double fade = static_cast<double>(value.life - 1) * D(0x3c94f209u);
        EXPECT_EQ(puff.rotationFactor, value.kept.x) << value.subtype;
        EXPECT_EQ(puff.field64, glm::vec2(value.kept.y, value.kept.z)) << value.subtype;
        EXPECT_EQ(puff.light,
                  glm::vec3(F32(fade * D(value.kept.x)), F32(fade * D(value.kept.y)), F32(fade * D(value.kept.z))))
            << value.subtype;
        EXPECT_EQ(puff.verticalSpeed, F32(value.rise)) << value.subtype;
        EXPECT_EQ(puff.position.z, F32(value.rise + 10.0)) << value.subtype;
        EXPECT_EQ(puff.scale, F32(1.0 + D(0x3cf5c28fu))) << value.subtype;
        EXPECT_EQ(puff.rotation, F32(D(tilt) + 280.0)) << value.subtype;
    }

    // Fuera de 0..2, el prefijo solo: vida 2, sin rand(), y un paso que no hace nada.
    auto particles = std::make_unique<Particles>();
    CrtRandom random(Seed);
    ASSERT_TRUE(particles->CreateChecked(Puff, {}, {}, light, 3, 1.0f, 1000.0f, random).HasValue());
    EXPECT_EQ(Only(*particles, 0).life, 2);
    EXPECT_EQ(random.State(), CrtRandom(Seed).State());
    ASSERT_TRUE(Step(*particles, random).HasValue());
    EXPECT_EQ(Only(*particles, 0).light, light);
    Particle drawn{.alive = true, .type = Puff, .texture = Puff, .subtype = 0, .scale = 1.0f, .light = light};
    const auto made = Client::Tests::DrawOneParticle(drawn, glm::vec2(64, 64), std::uint8_t{4});
    ASSERT_TRUE(made.HasValue());
    ASSERT_TRUE(made.Value().has_value());
    // 0x548895: mezclada con 0x620c10 después del material.
    EXPECT_EQ(made.Value()->blend, Blend::Blended);
}

// Los humos nuevos van al billboard del humo (0x54874c) con la mezcla que deja el material (con el byte 4,
// 0x620a00(0)), salvo la /20 (0x548723) y la /37 (0x54873a), que restan con 0x620b10. La /28..30 leen el terreno al
// nacer (0x5425f9) y siguen sin portar.
TEST(Cl11SmokeParticles, TheNewSmokesDrawAndTheGroundedOnesStayCut)
{
    for (const std::int32_t subtype : {17, 19, 20, 23, 24, 26, 31, 36, 37})
    {
        Particle particle{.alive = true,
                          .type = Smoke,
                          .texture = Smoke,
                          .subtype = subtype,
                          .scale = 1.0f,
                          .light = glm::vec3(1.0f)};
        const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(64, 64), std::uint8_t{4});
        ASSERT_TRUE(drawn.HasValue()) << subtype;
        ASSERT_TRUE(drawn.Value().has_value()) << subtype;
        const bool subtracts = subtype == 20 || subtype == 37;
        EXPECT_EQ(drawn.Value()->blend, subtracts ? Blend::Subtractive : Blend::Alpha) << subtype;
    }
    for (const std::int32_t subtype : {28, 29, 30})
    {
        auto particles = std::make_unique<Particles>();
        CrtRandom random(Seed);
        const auto created = particles->CreateChecked(Smoke, {}, {}, glm::vec3(1.0f), subtype, 1.0f, 0.0f, random);
        ASSERT_FALSE(created.HasValue()) << subtype;
        EXPECT_EQ(created.Error(), ParticleError::UnsupportedSubtype) << subtype;
    }
}
} // namespace
