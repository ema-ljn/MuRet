#include "Particles/Particles.h"

#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Particles/ParticleOwner.h"
#include "Particles/PortableParticleTrigonometry.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <optional>

// Las partículas del conjunto completo (0x50fddb) contra el ASM de c32bbd5f: la 6f1/3 de las botas (0x544088 al crear,
// 0x5480b7 al pasar) y la órbita 60f/0 con su caída a la /1 (0x542f74 al crear, 0x547354 y 0x5472da al pasar).
namespace
{
using Client::Crt::CrtRandom;
using Client::Particles::Particle;
using Client::Particles::ParticleError;
using Client::Particles::ParticleOwner;
using Client::Particles::ParticleOwnerKind;
using Client::Particles::Particles;

constexpr std::int32_t Mist = 0x6f1;
constexpr std::int32_t BootsSubtype = 3;
constexpr std::int32_t Orbit = 0x60f;
constexpr std::int32_t GameScene = 5;
constexpr ParticleOwner SetOwner{ParticleOwnerKind::Actor, 3};

float Float(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

using Client::Math::D;
using Client::Math::F32;

glm::vec3 Faded(const glm::vec3& light, std::uint32_t factorBits)
{
    const double factor = D(Float(factorBits));
    return {F32(D(light.x) * factor), F32(D(light.y) * factor), F32(D(light.z) * factor)};
}

// La 60f/0 y la /3 nacen y giran con las mismas instrucciones: todo igual salvo el subtipo y el dueño.
void ExpectSameOrbit(const Particle& set, const Particle& plain)
{
    EXPECT_EQ(set.alive, plain.alive);
    EXPECT_EQ(set.type, plain.type);
    EXPECT_EQ(set.texture, plain.texture);
    EXPECT_EQ(set.scale, plain.scale);
    EXPECT_EQ(set.position, plain.position);
    EXPECT_EQ(set.angles, plain.angles);
    EXPECT_EQ(set.light, plain.light);
    EXPECT_EQ(set.life, plain.life);
    EXPECT_EQ(set.rotation, plain.rotation);
    EXPECT_EQ(set.moves, plain.moves);
    EXPECT_EQ(set.verticalSpeed, plain.verticalSpeed);
    EXPECT_EQ(set.velocity, plain.velocity);
    EXPECT_EQ(set.start, plain.start);
}

class FullSetOrbitTest : public testing::Test
{
protected:
    // La órbita del conjunto (0x510339): en el personaje, la luz en 1 y la escala 0,19f, con el lugar 3 de dueño.
    void Create(Particles& pool, std::int32_t subtype, ParticleOwner owner, CrtRandom& random)
    {
        const auto made = pool.CreateChecked(Orbit, {100.0f, 200.0f, 300.0f}, {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f),
                                             subtype, Float(0x3e428f5cu), std::cref(m_clock), random, 0, m_trig, owner);
        ASSERT_TRUE(made.HasValue());
        ASSERT_TRUE(made.Value());
        ASSERT_EQ(*made.Value(), 0u);
    }

    auto Step(Particles& pool, CrtRandom& random, const Client::Particles::ParticleActorActions& actions)
    {
        return Client::Particles::StepParticlesChecked(pool, random, std::cref(m_clock), nullptr, m_trig, {}, {},
                                                       actions);
    }

    // El paso con el resolvedor del estado del dueño (el de un efecto con modelo) y sin el de los personajes.
    auto StepWithStates(Particles& pool, CrtRandom& random, const Client::Particles::ParticleOwnerStates& states)
    {
        return Client::Particles::StepParticlesChecked(pool, random, std::cref(m_clock), nullptr, m_trig, {}, {}, {},
                                                       {}, nullptr, {}, {}, {}, states);
    }

    static Client::Particles::ParticleActorActions ActionOf(const std::uint8_t& action)
    {
        return [&action](std::uint64_t identity) -> std::optional<std::uint8_t>
        {
            if (identity != SetOwner.identity)
                return std::nullopt;
            return action;
        };
    }

    float m_clock = 1000.0f;
    Client::Particles::PortableParticleTrigonometry m_trig;
};
} // namespace

// 0x543f49..0x544153: lo de la neblina (giro, escala, caída, la luz de la escena y ±10, ±10, ±20) y, con la /3, la
// textura cra_04 (0x5ae), la luz del argumento, vida 12, otro giro, vz 2, la escala (rand() % 3 + 3) · 0,18f y otro
// corrimiento. Once rand() en el orden del ASM.
TEST(FullSetParticlesTest, TheBootsMistIsBornAsTheMistWithElevenRandomsAndItsOwnLight)
{
    Particles particles;
    CrtRandom random(1);
    const glm::vec3 point{100.0f, 200.0f, 300.0f};
    const glm::vec3 light{Float(0x3f266666u), Float(0x3e99999au), Float(0x3dcccccdu)};

    const auto made =
        particles.CreateKalimaChecked(Mist, point, {}, light, BootsSubtype, 1.0f, 1000.0f, random, GameScene);

    ASSERT_TRUE(made.HasValue());
    ASSERT_TRUE(made.Value());
    const Particle& p = particles.All()[*made.Value()];
    CrtRandom draws(1);
    std::array<int, 11> r{};
    for (int& value : r)
        value = draws.Next();
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_TRUE(p.alive);
    EXPECT_EQ(p.type, Mist);
    EXPECT_EQ(p.subtype, BootsSubtype);
    EXPECT_EQ(p.texture, 0x5ae);
    EXPECT_EQ(p.life, 12);
    EXPECT_EQ(p.light, light);
    EXPECT_EQ(p.rotation, static_cast<float>(r[6] % 360));
    EXPECT_EQ(p.scale, F32(static_cast<double>(r[7] % 3 + 3) * D(Float(0x3e3851ecu))));
    EXPECT_EQ(p.velocity, glm::vec3(0.0f, 0.0f, 2.0f));
    EXPECT_EQ(p.start, point);
    const glm::vec3 first{F32(static_cast<double>(r[3] % 20 - 10) + D(point.x)),
                          F32(static_cast<double>(r[4] % 20 - 10) + D(point.y)),
                          F32(static_cast<double>(r[5] % 40 - 20) + D(point.z))};
    EXPECT_EQ(p.position, glm::vec3(F32(static_cast<double>(r[8] % 20 - 10) + D(first.x)),
                                    F32(static_cast<double>(r[9] % 20 - 10) + D(first.y)),
                                    F32(static_cast<double>(r[10] % 40 - 20) + D(first.z))));
}

// 0x543fcd: sin escena, la /3 corta como la /0, después de los tres rand() del prefijo y sin los de las botas.
TEST(FullSetParticlesTest, TheBootsMistWithoutSceneStopsAfterTheMistPrefix)
{
    Particles particles;
    CrtRandom random(1);

    const auto made = particles.CreateChecked(Mist, {}, {}, glm::vec3(1.0f), BootsSubtype, 1.0f, 0.0f, random);

    ASSERT_FALSE(made.HasValue());
    EXPECT_EQ(made.Error(), ParticleError::MissingScene);
    CrtRandom draws(1);
    for (int call = 0; call < 3; ++call)
        static_cast<void>(draws.Next());
    EXPECT_EQ(random.State(), draws.State());
}

// 0x547fe8..0x5480ed: el viento (dos rand()), la vida y el movimiento común; la escala +0,03f, vx y vy de dos rand(),
// vz la suma retenida con 0,1f; con vida bajo 10, la luz por 0,9090909f; con la /3, la escala −0,038f, vz la suma sin
// redondear menos 0,02f y la luz por 0,98039216f. Vive 12 pasos.
TEST(FullSetParticlesTest, TheBootsMistShrinksSlowsItsRiseAndFadesForTwelveSteps)
{
    Particles particles;
    CrtRandom random(1);
    const glm::vec3 light{0.5f, Float(0x3e75c28fu), Float(0x3f4ccccdu)};
    ASSERT_TRUE(
        particles
            .CreateKalimaChecked(Mist, {100.0f, 200.0f, 300.0f}, {}, light, BootsSubtype, 1.0f, 0.0f, random, GameScene)
            .HasValue());
    Particle expected = particles.All()[0];
    CrtRandom draws = random;

    for (int step = 1; step <= 12; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Particles::StepParticlesChecked(particles, random).HasValue());
        static_cast<void>(draws.Next());
        static_cast<void>(draws.Next());
        --expected.life;
        for (int axis = 0; axis < 3; ++axis)
            expected.position[axis] = F32(D(expected.velocity[axis]) + D(expected.position[axis]));
        expected.scale = F32(D(expected.scale) + D(0.03f));
        for (int axis = 0; axis < 2; ++axis)
            expected.velocity[axis] = F32(static_cast<double>(draws.Next() % 20 - 10) * D(0.1f));
        const double rise = D(expected.velocity.z) + D(0.1f);
        if (expected.life < 10)
            expected.light = Faded(expected.light, 0x3f68ba2eu);
        expected.scale = F32(D(expected.scale) - D(Float(0x3d1ba5e3u)));
        expected.velocity.z = F32(rise - D(Float(0x3ca3d70au)));
        expected.light = Faded(expected.light, 0x3f7afafbu);

        const Particle& p = particles.All()[0];
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_EQ(p.life, expected.life);
        EXPECT_EQ(p.alive, step < 12);
        EXPECT_EQ(p.position, expected.position);
        EXPECT_EQ(p.velocity, expected.velocity);
        EXPECT_EQ(p.scale, expected.scale);
        EXPECT_EQ(p.light, expected.light);
    }
}

// 0x542f74 y 0x5471bd: mientras su personaje no cae, la /0 es la /3 con el lugar del personaje de dueño.
TEST_F(FullSetOrbitTest, TheSetOrbitIsBornAndTurnsLikeTheOrbitOfSubtypeThree)
{
    Particles set;
    Particles plain;
    CrtRandom setRandom(1);
    CrtRandom plainRandom(1);
    Create(set, 0, SetOwner, setRandom);
    Create(plain, 3, {}, plainRandom);
    EXPECT_EQ(set.All()[0].subtype, 0);
    EXPECT_EQ(set.All()[0].typedOwner, SetOwner);
    EXPECT_EQ(setRandom.State(), plainRandom.State());
    ExpectSameOrbit(set.All()[0], plain.All()[0]);

    const std::uint8_t action = 0;
    for (int step = 1; step <= 60; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Step(set, setRandom, ActionOf(action)).HasValue());
        ASSERT_TRUE(Step(plain, plainRandom, {}).HasValue());
        EXPECT_EQ(set.All()[0].subtype, 0);
        EXPECT_EQ(setRandom.State(), plainRandom.State());
        ExpectSameOrbit(set.All()[0], plain.All()[0]);
    }
    EXPECT_FALSE(set.All()[0].alive);
}

// 0x547354..0x5473a9: con la acción del dueño en [0xd, 0x21] o [0x38, 0x3c], después de girar, la /1 con la vida en 20
// si tenía más, la velocidad en 0 y la partida en la posición. Fuera de esos rangos sigue siendo la /0.
TEST_F(FullSetOrbitTest, TheSetOrbitFallsWithTheActionsOfItsOwner)
{
    struct Case
    {
        std::uint8_t action;
        bool falls;
    };
    constexpr std::array cases{Case{0xc, false},  Case{0xd, true},  Case{0x21, true}, Case{0x22, false},
                               Case{0x37, false}, Case{0x38, true}, Case{0x3c, true}, Case{0x3d, false}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.action));
        Particles set;
        Particles plain;
        CrtRandom setRandom(1);
        CrtRandom plainRandom(1);
        Create(set, 0, SetOwner, setRandom);
        Create(plain, 3, {}, plainRandom);

        ASSERT_TRUE(Step(set, setRandom, ActionOf(probe.action)).HasValue());
        ASSERT_TRUE(Step(plain, plainRandom, {}).HasValue());

        const Particle& fallen = set.All()[0];
        const Particle& turned = plain.All()[0];
        EXPECT_EQ(setRandom.State(), plainRandom.State());
        EXPECT_EQ(fallen.position, turned.position);
        EXPECT_EQ(fallen.light, turned.light);
        if (!probe.falls)
        {
            EXPECT_EQ(fallen.subtype, 0);
            ExpectSameOrbit(fallen, turned);
            continue;
        }
        EXPECT_EQ(fallen.subtype, 1);
        EXPECT_EQ(fallen.life, 20);
        EXPECT_EQ(fallen.velocity, glm::vec3(0.0f));
        EXPECT_EQ(fallen.start, turned.position);
        EXPECT_EQ(fallen.typedOwner, SetOwner);
    }
}

// 0x5472da..0x547351: la /1 ya no gira ni lee al dueño; con la vida en 20 o menos, la luz por 0,9090909f, y muere
// cuando se le acaba la vida.
TEST_F(FullSetOrbitTest, TheFallenOrbitStaysWhereItFellAndFades)
{
    Particles set;
    CrtRandom random(1);
    Create(set, 0, SetOwner, random);
    const std::uint8_t action = 0x38;
    ASSERT_TRUE(Step(set, random, ActionOf(action)).HasValue());
    Particle expected = set.All()[0];
    ASSERT_EQ(expected.subtype, 1);
    ASSERT_EQ(expected.life, 20);

    for (int step = 1; step <= 20; ++step)
    {
        SCOPED_TRACE(step);
        CrtRandom wind = random;
        static_cast<void>(wind.Next());
        static_cast<void>(wind.Next());
        ASSERT_TRUE(Step(set, random, {}).HasValue());
        --expected.life;
        expected.light = Faded(expected.light, 0x3f68ba2eu);
        const Particle& p = set.All()[0];
        EXPECT_EQ(random.State(), wind.State());
        EXPECT_EQ(p.subtype, 1);
        EXPECT_EQ(p.life, expected.life);
        EXPECT_EQ(p.alive, step < 20);
        EXPECT_EQ(p.position, expected.position);
        EXPECT_EQ(p.light, expected.light);
    }
}

// 0x52b8ff y 0x52b977: el 0x61d suelta la /0 con él de dueño (+0x40), y 0x54735f lee su +0x111 (ModelEffect::animation)
// como la acción de un personaje, por ParticleOwnerStates: con 0 (la del prefijo de 0x518cf0) sigue girando como la /3,
// en [0xd, 0x21] o [0x38, 0x3c] cae a la /1. Lo lee aunque el lugar del efecto esté muerto; sin el resolvedor, o con un
// lugar que no responde, MissingOwner después de girar. Lo esperado sale de orbit.py (0x542ed0 y 0x5446e0..0x5473a9
// en una x87 aparte): con la semilla 1, la fase 17, la subida 0x4028f5c2 y, tras un paso (el viento y la vida 59), el
// punto (0x430ab77f; 0x433df31a; 0x439751ec) y la escala 0x3e6978d4.
TEST_F(FullSetOrbitTest, TheSetOrbitReadsTheActionOfItsModelEffectOwner)
{
    static constexpr ParticleOwner DecalOwner{ParticleOwnerKind::ModelEffect, 5};
    struct Case
    {
        std::uint8_t action;
        bool alive;
        bool falls;
    };
    constexpr std::array cases{Case{0, true, false}, Case{0xd, true, true}, Case{0x3c, false, true},
                               Case{0x22, false, false}};
    const glm::vec3 turned{Float(0x430ab77fu), Float(0x433df31au), Float(0x439751ecu)};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.action));
        const Client::Particles::ParticleOwnerStates states =
            [&probe](const ParticleOwner& owner) -> std::optional<Client::Particles::ParticleOwnerState>
        {
            if (owner != DecalOwner)
                return std::nullopt;
            return Client::Particles::ParticleOwnerState{probe.alive, {}, std::nullopt, probe.action};
        };
        Particles set;
        CrtRandom setRandom(1);
        Create(set, 0, DecalOwner, setRandom);
        EXPECT_EQ(setRandom.State(), 0xe7847115u);
        ASSERT_TRUE(StepWithStates(set, setRandom, states).HasValue());
        const Particle& stepped = set.All()[0];
        EXPECT_EQ(setRandom.State(), 0x3d6c1037u);
        EXPECT_EQ(stepped.position, turned);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(stepped.scale), 0x3e6978d4u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(stepped.verticalSpeed), 0x4028f5c2u);
        EXPECT_EQ(stepped.rotation, 220.0f);
        EXPECT_EQ(stepped.light, glm::vec3(1.0f));
        EXPECT_EQ(stepped.typedOwner, DecalOwner);
        if (!probe.falls)
        {
            EXPECT_EQ(stepped.subtype, 0);
            EXPECT_EQ(stepped.life, 59);
            EXPECT_EQ(stepped.velocity, (glm::vec3{17.0f, 0.0f, 0.0f}));
            EXPECT_EQ(stepped.start, (glm::vec3{100.0f, 200.0f, 300.0f}));
            continue;
        }
        EXPECT_EQ(stepped.subtype, 1);
        EXPECT_EQ(stepped.life, 20);
        EXPECT_EQ(stepped.velocity, glm::vec3(0.0f));
        EXPECT_EQ(stepped.start, turned);
    }
    for (const ParticleOwner owner : {DecalOwner, ParticleOwner{ParticleOwnerKind::ModelEffect, 6}})
    {
        const bool resolver = owner == DecalOwner;
        const Client::Particles::ParticleOwnerStates states =
            [](const ParticleOwner& asked) -> std::optional<Client::Particles::ParticleOwnerState>
        {
            if (asked != DecalOwner)
                return std::nullopt;
            return Client::Particles::ParticleOwnerState{true, {}, std::nullopt, 0};
        };
        Particles set;
        CrtRandom random(1);
        Create(set, 0, owner, random);
        const auto missing = StepWithStates(set, random, resolver ? Client::Particles::ParticleOwnerStates{} : states);
        ASSERT_FALSE(missing.HasValue());
        EXPECT_EQ(missing.Error(), ParticleError::MissingOwner);
    }
}

// 0x54735c: la /0 lee al dueño por +0x40. Sin un personaje del pool (sin el resolvedor, con un lugar que no está o sin
// dueño), MissingOwner después de girar.
TEST_F(FullSetOrbitTest, TheSetOrbitWithoutAnActorOwnerIsMissingItsOwner)
{
    const std::uint8_t action = 0;
    struct Case
    {
        ParticleOwner owner;
        bool resolver;
    };
    constexpr std::array cases{Case{SetOwner, false}, Case{ParticleOwner{ParticleOwnerKind::Actor, 4}, true},
                               Case{ParticleOwner{}, true}};
    for (const Case& probe : cases)
    {
        Particles set;
        CrtRandom random(1);
        Create(set, 0, probe.owner, random);
        const auto stepped =
            Step(set, random, probe.resolver ? ActionOf(action) : Client::Particles::ParticleActorActions{});
        ASSERT_FALSE(stepped.HasValue());
        EXPECT_EQ(stepped.Error(), ParticleError::MissingOwner);
    }
}
