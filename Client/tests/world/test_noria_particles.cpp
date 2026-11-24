#include "Crt/CrtRandom.h"
#include "Render/Models/OriginalRotation.h"
#include "support/ParticleDrawRecorder.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>
#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace
{
TEST(NoriaParticlesTest, ShinySubtypesStartWithoutRandomAndKeepTheCommonPrefix)
{
    for (int subtype : {0, 1})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        Client::Crt::CrtRandom random(7);
        const auto created =
            particles->CreateChecked(0x5fc, {1, 2, 3}, {10, 20, 30}, {0.2f, 0.3f, 0.4f}, subtype, 9.0f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const auto& particle = particles->All()[*created.Value()];
        EXPECT_EQ(particle.life, 18);
        EXPECT_EQ(particle.angles, glm::vec3(45, 20, 30));
        EXPECT_EQ(particle.scale, 9.0f);
        EXPECT_EQ(particle.velocity, glm::vec3(0));
        EXPECT_EQ(random.State(), 7u);
    }
}

TEST(NoriaParticlesTest, ShinyStepUsesLifeSineAndSubtypeOneRotatesTwelveDegrees)
{
    for (int subtype : {0, 1})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        auto& particle = particles->All().front();
        particle.alive = true;
        particle.type = particle.texture = 0x5fc;
        particle.subtype = subtype;
        particle.life = 18;
        particle.scale = 9.0f;
        particle.rotation = 30.0f;
        Client::Crt::CrtRandom random(7);
        const auto stepped = Client::World::StepParticlesChecked(*particles, random);
        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(particle.life, 17);
        EXPECT_NEAR(particle.scale, subtype == 0 ? 0.17364827f : 0.13023620f, 0.0000001f);
        EXPECT_EQ(particle.rotation, subtype == 0 ? 30.0f : 18.0f);
        EXPECT_EQ(random.State(), 0xc40e66b9u); // Sólo dos rand del viento.
    }
}

TEST(NoriaParticlesTest, TheFairySparkKeepsFiveRandomDrawsAndItsStoredMotion)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
    const auto created = particles->CreateChecked(0x5c5, {100, 200, 300}, {0, 0, 0}, {1, 1, 1}, 0, 9.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const auto& particle = particles->All()[*created.Value()];
    EXPECT_FLOAT_EQ(particle.scale, 0.5f);
    EXPECT_EQ(particle.life, 38);
    EXPECT_EQ(particle.angles.z, 95.0f);
    EXPECT_EQ(particle.verticalSpeed, 6.0f);
    EXPECT_NEAR(particle.velocity.x, -2.0920088f, 0.000001f);
    EXPECT_NEAR(particle.velocity.y, -0.1830271f, 0.000001f);
    EXPECT_EQ(particle.velocity.z, 0.0f);
    EXPECT_EQ(random.State(), 0xbca151d2u);
}

// 0x5404ad..0x5404ca: /2 duplica escala y triplica el vector local antes de 0x5fcbd0; mismo resto que /0.
TEST(NoriaParticlesTest, ThePushedSparkDoublesTheScaleAndTriplesTheForwardSpeed)
{
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto ordinary = std::make_unique<Client::World::Particles>();
        auto pushed = std::make_unique<Client::World::Particles>();
        Client::Crt::CrtRandom first(seed);
        Client::Crt::CrtRandom second(seed);
        const auto base = ordinary->CreateChecked(0x5c5, {10, 20, 30}, {170, 0, 12}, {1, 1, 1}, 0, 1, 0, first);
        ASSERT_TRUE(base.HasValue());
        ASSERT_TRUE(base.Value().has_value());
        const auto created = pushed->CreateChecked(0x5c5, {10, 20, 30}, {170, 0, 12}, {1, 1, 1}, 2, 1, 0, second);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const auto& original = ordinary->All()[*base.Value()];
        const auto& particle = pushed->All()[*created.Value()];
        EXPECT_FLOAT_EQ(particle.scale, original.scale * 2.0f);
        EXPECT_EQ(particle.life, original.life);
        EXPECT_EQ(particle.angles, original.angles);
        EXPECT_EQ(particle.verticalSpeed, original.verticalSpeed);
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_NEAR(particle.velocity[axis], original.velocity[axis] * 3.0f, 0.00001f);
        EXPECT_EQ(first.State(), second.State());
    }
}
TEST(NoriaParticlesTest, TheFairySparkKeepsThePrefixBeforeMissingTerrainAndContinuesAfterCollision)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto& particle = particles->All().front();
    particle.alive = true;
    particle.type = particle.texture = 0x5c5;
    particle.life = 10;
    particle.position = {10, 20, 100};
    particle.verticalSpeed = 4;
    particle.velocity = {1, 2, 3};
    const auto initial = particle;
    Client::Crt::CrtRandom random(7);
    const auto missing = Client::World::StepParticlesChecked(*particles, random);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Error(), Client::World::ParticleError::MissingTerrainHeight);
    EXPECT_EQ(particle.position, glm::vec3(10, 20, 104));
    EXPECT_EQ(particle.verticalSpeed, 2.0f);
    EXPECT_EQ(particle.life, 9);
    EXPECT_EQ(particle.light, glm::vec3(0.5625f));
    particle = initial;
    int calls = 0;
    const auto stepped = Client::World::StepParticlesChecked(*particles, random, nullptr,
                                                             [&](float x, float y)
                                                             {
                                                                 EXPECT_EQ(x, 10.0f);
                                                                 EXPECT_EQ(y, 20.0f);
                                                                 EXPECT_EQ(particle.position.z, 104.0f);
                                                                 ++calls;
                                                                 return 120.0;
                                                             });
    ASSERT_TRUE(stepped.HasValue());
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(particle.position, glm::vec3(11, 22, 123));
    EXPECT_FLOAT_EQ(particle.verticalSpeed, -1.2f);
    EXPECT_EQ(particle.life, 5);
}
TEST(NoriaParticlesTest, NoriaParticleBillboardsUseTheWholeTextureAndTheStoredRotation)
{
    for (const auto type : {0x5fc, 0x5c5})
    {
        Client::World::Particle particle;
        particle.alive = true;
        particle.type = particle.texture = type;
        particle.subtype = type == Client::World::FairySparkParticle ? 2 : 0;
        particle.scale = 2.0f;
        particle.rotation = 27.0f;
        particle.light = {0.2f, 0.4f, 0.6f};
        const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(128, 64), std::uint8_t{3});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->texture, type);
        EXPECT_EQ(drawn.Value()->rotation, 27.0f);
        EXPECT_EQ(drawn.Value()->uv, glm::vec2(0));
        EXPECT_EQ(drawn.Value()->uvSize, glm::vec2(1));
        EXPECT_EQ(drawn.Value()->light, particle.light);
    }
}
// 0x54038c..0x5403a7: la 5c5/7 (las chispas de la mariposa de la selección) sortea su escala como la /0 y
// después sólo su vida (12..23): sin giro, sin velocidad y sin la escala del argumento.
TEST(NoriaParticlesTest, TheButterflySparkDrawsOnlyItsScaleAndItsLife)
{
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
    Client::Crt::CrtRandom draws(7);
    const auto created =
        particles->CreateChecked(0x5c5, {100, 200, 300}, {0, 0, 30}, {0.5f, 0.5f, 0.5f}, 7, 1.0f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const auto& particle = particles->All()[*created.Value()];
    EXPECT_EQ(particle.scale, static_cast<float>(((draws.Next() & 3) + 4) * static_cast<double>(0.1f)));
    EXPECT_EQ(particle.life, draws.Next() % 12 + 12);
    EXPECT_EQ(particle.angles, glm::vec3(0, 0, 30));
    EXPECT_EQ(particle.verticalSpeed, 0.0f);
    EXPECT_EQ(particle.velocity, glm::vec3(0));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x5403eb..0x540406: la 5c5/1 (las chispas del bastón 0xc18, 0x50f5a8) sortea su escala como la /0 y después sólo su
// vida, (rand() & 7) + 8: sin giro, sin velocidad y sin la escala del argumento.
TEST(NoriaParticlesTest, TheStaffSparkDrawsOnlyItsScaleAndItsLife)
{
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto particles = std::make_unique<Client::World::Particles>();
        Client::Crt::CrtRandom random(seed);
        Client::Crt::CrtRandom draws(seed);
        const auto created =
            particles->CreateChecked(0x5c5, {100, 200, 300}, {0, 0, 30}, {0.8f, 0.48f, 0.32f}, 1, 1.0f, 0.0f, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const auto& particle = particles->All()[*created.Value()];
        EXPECT_EQ(particle.scale, static_cast<float>(((draws.Next() & 3) + 4) * static_cast<double>(0.1f)));
        EXPECT_EQ(particle.life, (draws.Next() & 7) + 8);
        EXPECT_EQ(particle.angles, glm::vec3(0, 0, 30));
        EXPECT_EQ(particle.light, glm::vec3(0.8f, 0.48f, 0.32f));
        EXPECT_EQ(particle.verticalSpeed, 0.0f);
        EXPECT_EQ(particle.velocity, glm::vec3(0));
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x540333..0x54055a: la /3 nace como la /0; la /4 triplica la escala y multiplica por 10 el vector local (0x5404cc);
// las de fuera de la tabla (la /9, la −1) van por el camino de la /1 (0x5403eb). La /5, abajo.
TEST(NoriaParticlesTest, TheOtherFairySparksFollowTheirBranch)
{
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto ordinary = std::make_unique<Client::World::Particles>();
        Client::Crt::CrtRandom first(seed);
        const auto base = ordinary->CreateChecked(0x5c5, {10, 20, 30}, {170, 0, 12}, {1, 1, 1}, 0, 1, 0, first);
        ASSERT_TRUE(base.HasValue());
        ASSERT_TRUE(base.Value().has_value());
        const auto& original = ordinary->All()[*base.Value()];
        for (const int subtype : {3, 4})
        {
            SCOPED_TRACE(subtype);
            auto particles = std::make_unique<Client::World::Particles>();
            Client::Crt::CrtRandom random(seed);
            const auto created =
                particles->CreateChecked(0x5c5, {10, 20, 30}, {170, 0, 12}, {1, 1, 1}, subtype, 1, 0, random);
            ASSERT_TRUE(created.HasValue());
            ASSERT_TRUE(created.Value().has_value());
            const auto& particle = particles->All()[*created.Value()];
            const bool strong = subtype == 4;
            EXPECT_EQ(particle.scale,
                      strong ? static_cast<float>(static_cast<double>(original.scale) * 3.0) : original.scale);
            EXPECT_EQ(particle.life, original.life);
            EXPECT_EQ(particle.angles, original.angles);
            EXPECT_EQ(particle.verticalSpeed, original.verticalSpeed);
            Client::Crt::CrtRandom draws(seed);
            for (int draw = 0; draw < 4; ++draw)
                static_cast<void>(draws.Next());
            const auto forward = static_cast<float>(static_cast<double>(draws.Next() % 20 + 20) *
                                                    static_cast<double>(std::bit_cast<float>(0x3dcccccdu)));
            const float local = strong ? static_cast<float>(static_cast<double>(forward) * 10.0) : forward;
            EXPECT_EQ(particle.velocity,
                      Client::Render::Models::OriginalVectorRotate(
                          glm::vec3(0.0f, local, 0.0f), Client::Render::Models::OriginalAngleMatrix(original.angles)));
            EXPECT_EQ(random.State(), first.State());
        }
        for (const int subtype : {9, -1})
        {
            SCOPED_TRACE(subtype);
            auto particles = std::make_unique<Client::World::Particles>();
            Client::Crt::CrtRandom random(seed);
            Client::Crt::CrtRandom draws(seed);
            const auto created =
                particles->CreateChecked(0x5c5, {10, 20, 30}, {0, 0, 30}, {1, 1, 1}, subtype, 1, 0, random);
            ASSERT_TRUE(created.HasValue());
            ASSERT_TRUE(created.Value().has_value());
            const auto& particle = particles->All()[*created.Value()];
            EXPECT_EQ(particle.scale, static_cast<float>(((draws.Next() & 3) + 4) * static_cast<double>(0.1f)));
            EXPECT_EQ(particle.life, (draws.Next() & 7) + 8);
            EXPECT_EQ(particle.velocity, glm::vec3(0));
            EXPECT_EQ(random.State(), draws.State());
        }
    }
}

// 0x540333 y 0x5403ad..0x5403e9: la /5 (la del paso del 0x140, 0x52a16e) saca la escala ((rand() & 3) + 4) · 0,1f que
// después pisa con la del pedido, lee la posición de su dueño (+0x40 → +0x10) para su partida (+0x6c), vive
// (rand() & 3) + 12 y sale con la subida en 0, la textura 0x5c6 y la velocidad del prefijo (cero). Dos rand(). Sin la
// posición del dueño, CreateChecked la rechaza antes de tocar el lugar; CreateOwnedChecked no la conoce. Con la tabla
// llena, nada.
TEST(NoriaParticlesTest, TheOwnerBirthFairySparkStartsAtItsOwner)
{
    const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::ModelEffect, 3};
    const glm::vec3 ownerAt{500.0f, 600.0f, 70.0f};
    Client::World::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto particles = std::make_unique<Client::World::Particles>();
        Client::Crt::CrtRandom random(seed);
        const auto created =
            particles->CreateTowardChecked(0x5c5, {10, 20, 30}, {170, 0, 12}, {0.5f, 0.5f, 0.5f}, 5, 2.0f,
                                           std::cref(clock), random, trigonometry, owner, ownerAt);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const auto& particle = particles->All()[*created.Value()];
        Client::Crt::CrtRandom draws(seed);
        static_cast<void>(draws.Next()); // 0x540333, la escala que se pisa.
        EXPECT_EQ(particle.life, (draws.Next() & 3) + 12);
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_EQ(particle.scale, 2.0f);
        EXPECT_EQ(particle.start, ownerAt);
        EXPECT_EQ(particle.position, glm::vec3(10, 20, 30));
        EXPECT_EQ(particle.angles, glm::vec3(170, 0, 12));
        EXPECT_EQ(particle.light, glm::vec3(0.5f));
        EXPECT_EQ(particle.texture, 0x5c6);
        EXPECT_EQ(particle.verticalSpeed, 0.0f);
        EXPECT_EQ(particle.velocity, glm::vec3(0));
        EXPECT_EQ(particle.typedOwner, owner);
        if (seed == 1)
        {
            EXPECT_EQ(particle.life, 12 + 3); // el segundo rand() de la semilla 1 es 18467 (& 3 = 3).
        }
    }
    auto particles = std::make_unique<Client::World::Particles>();
    Client::Crt::CrtRandom random(7);
    const auto missing = particles->CreateChecked(0x5c5, {10, 20, 30}, {}, {1, 1, 1}, 5, 1, 0, random);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Error(), Client::World::ParticleError::MissingOwner);
    const auto unknown = particles->CreateOwnedChecked(0x5c5, {10, 20, 30}, {}, {1, 1, 1}, 5, 1, 0, random, 0, owner);
    ASSERT_FALSE(unknown.HasValue());
    EXPECT_EQ(unknown.Error(), Client::World::ParticleError::UnsupportedSubtype);
    EXPECT_EQ(random.State(), 7u);
    EXPECT_FALSE(particles->All().front().alive);
    for (auto& particle : particles->All())
        particle.alive = true;
    const auto none = particles->CreateTowardChecked(0x5c5, {10, 20, 30}, {}, {1, 1, 1}, 5, 2.0f, std::cref(clock),
                                                     random, trigonometry, owner, ownerAt);
    ASSERT_TRUE(none.HasValue());
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(random.State(), 7u);
}

// 0x544f29..0x545040: la /5 se ilumina con la vida · 0,0625 (no pisa la luz como la /6), cae 0,3f (0x544f4c), se
// mueve y resta lo que el dueño se corrió desde su partida, que pasa a ser su posición (0x544fe4 → 0x544fef). El
// dueño muerto también: sólo se lee su posición. Sin dueño, MissingOwner.
TEST(NoriaParticlesTest, TheOwnerBirthFairySparkMovesWithItsOwner)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto& particle = particles->All().front();
    particle.alive = true;
    particle.type = 0x5c5;
    particle.texture = 0x5c6;
    particle.subtype = 5;
    particle.life = 14;
    particle.light = glm::vec3(0.5f);
    particle.position = {10.5f, 20.25f, 100.0f};
    particle.start = {5.0f, 5.0f, 5.0f};
    particle.velocity = {1.0f, 2.0f, 3.0f};
    particle.verticalSpeed = 4.0f;
    particle.typedOwner = {Client::World::ParticleOwnerKind::ModelEffect, 3};
    const auto initial = particle;
    const glm::vec3 moved{6.0f, 7.0f, 8.0f};
    Client::World::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    const auto step = [&](const Client::World::ParticleOwnerStates& owners)
    {
        Client::Crt::CrtRandom random(7);
        return Client::World::StepParticlesChecked(
            *particles, random, std::cref(clock), &map, trigonometry, [](float, float) { return 0.0; }, {}, {}, {},
            nullptr, {}, {}, {}, owners);
    };
    const auto stepped = step(
        [&](const Client::World::ParticleOwner& owner) -> std::optional<Client::World::ParticleOwnerState>
        {
            EXPECT_EQ(owner, initial.typedOwner);
            return Client::World::ParticleOwnerState{.alive = false, .position = moved, .start = std::nullopt};
        });
    ASSERT_TRUE(stepped.HasValue());
    // 13 · 0,0625 = 0,8125 y 4 − 0,3f (0x3e99999a) = 3,7f (0x406ccccd), calculados aparte.
    EXPECT_EQ(particle.life, 13);
    EXPECT_EQ(particle.light, glm::vec3(0.8125f));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.verticalSpeed), 0x406ccccdu);
    EXPECT_EQ(particle.position, glm::vec3(12.5f, 24.25f, 110.0f));
    EXPECT_EQ(particle.start, moved);

    // La x retenida: 2^24 + 1 no entra en un float y, con la partida 1 más allá del dueño, vuelve a 2^24. Por un float
    // daría 2^24 − 1 (0x4b7fffff).
    particle = initial;
    particle.position.x = 16777216.0f;
    particle.start.x = 7.0f;
    ASSERT_TRUE(
        step([&](const Client::World::ParticleOwner&) -> std::optional<Client::World::ParticleOwnerState>
             { return Client::World::ParticleOwnerState{.alive = true, .position = moved, .start = std::nullopt}; })
            .HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.position.x), 0x4b800000u);

    particle = initial;
    const auto lost = step({});
    ASSERT_FALSE(lost.HasValue());
    EXPECT_EQ(lost.Error(), Client::World::ParticleError::MissingOwner);
}

// 0x5404f9..0x54053c: la /6 (la de 0x529f71) saca los cinco rand() de la /0, después la vida (rand() & 3) + 4 y el
// vector local (0; (rand() % 20 + 20) · 0,2f; 0); toma la escala del pedido, la textura 0x5c6 y la subida en 0. Con
// CreateOwnedChecked guarda a su dueño.
TEST(NoriaParticlesTest, TheOwnedFairySparkDrawsSevenTimesAndKeepsItsOwner)
{
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto particles = std::make_unique<Client::World::Particles>();
        Client::Crt::CrtRandom random(seed);
        const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::ModelEffect, 4};
        const auto created = particles->CreateOwnedChecked(0x5c5, {10, 20, 30}, {170, 0, 12}, {0.5f, 0.5f, 0.5f}, 6,
                                                           1.5f, 0.0f, random, 0, owner);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const auto& particle = particles->All()[*created.Value()];
        Client::Crt::CrtRandom draws(seed);
        static_cast<void>(draws.Next());
        static_cast<void>(draws.Next());
        const auto yaw = static_cast<float>(draws.Next() % 360);
        static_cast<void>(draws.Next());
        static_cast<void>(draws.Next());
        const int life = (draws.Next() & 3) + 4;
        const auto forward = static_cast<float>(static_cast<double>(draws.Next() % 20 + 20) *
                                                static_cast<double>(std::bit_cast<float>(0x3e4ccccdu)));
        const glm::vec3 angles{170.0f, 0.0f, yaw};
        EXPECT_EQ(particle.life, life);
        EXPECT_EQ(particle.scale, 1.5f);
        EXPECT_EQ(particle.texture, 0x5c6);
        EXPECT_EQ(particle.verticalSpeed, 0.0f);
        EXPECT_EQ(particle.angles, angles);
        EXPECT_EQ(particle.velocity,
                  Client::Render::Models::OriginalVectorRotate(glm::vec3(0.0f, forward, 0.0f),
                                                               Client::Render::Models::OriginalAngleMatrix(angles)));
        EXPECT_EQ(particle.typedOwner, owner);
        EXPECT_EQ(random.State(), draws.State());
        if (seed == 1)
        {
            // Valores fijos, calculados aparte del RNG de la CRT: vida 4, giro 214, adelante 7,6f.
            EXPECT_EQ(particle.life, 4);
            EXPECT_EQ(particle.angles.z, 214.0f);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(forward), 0x40f33333u);
            EXPECT_EQ(random.State(), 752224798u);
        }
    }
}

// 0x544f57..0x545040: la /6 cae 0,3f, su luz es 0,9f, se mueve y resta lo que el dueño se corrió desde su partida
// (la x retenida, la y y la z guardadas); la partida pasa a ser la posición del dueño. Sin dueño, MissingOwner.
TEST(NoriaParticlesTest, TheOwnedFairySparkMovesWithItsOwner)
{
    auto particles = std::make_unique<Client::World::Particles>();
    auto& particle = particles->All().front();
    particle.alive = true;
    particle.type = 0x5c5;
    particle.texture = 0x5c6;
    particle.subtype = 6;
    particle.life = 10;
    particle.position = {10.5f, 20.25f, 100.0f};
    particle.start = {5.0f, 5.0f, 5.0f};
    particle.velocity = {1.0f, 2.0f, 3.0f};
    particle.verticalSpeed = 4.0f;
    particle.typedOwner = {Client::World::ParticleOwnerKind::ModelEffect, 4};
    const auto initial = particle;
    const glm::vec3 moved{6.0f, 7.0f, 8.0f};
    Client::World::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    const auto step = [&](const Client::World::ParticleOwnerStates& owners)
    {
        Client::Crt::CrtRandom random(7);
        return Client::World::StepParticlesChecked(
            *particles, random, std::cref(clock), &map, trigonometry, [](float, float) { return 0.0; }, {}, {}, {},
            nullptr, {}, {}, {}, owners);
    };
    const auto stepped = step(
        [&](const Client::World::ParticleOwner& owner) -> std::optional<Client::World::ParticleOwnerState>
        {
            EXPECT_EQ(owner, initial.typedOwner);
            return Client::World::ParticleOwnerState{.alive = true, .position = moved, .start = std::nullopt};
        });
    ASSERT_TRUE(stepped.HasValue());
    const double fall = static_cast<double>(std::bit_cast<float>(0x3e99999au));
    EXPECT_EQ(particle.life, 9);
    EXPECT_EQ(particle.light, glm::vec3(std::bit_cast<float>(0x3f666666u)));
    EXPECT_EQ(particle.verticalSpeed, static_cast<float>(4.0 - fall));
    EXPECT_EQ(particle.position, glm::vec3(12.5f, 24.25f, 110.0f));
    EXPECT_EQ(particle.start, moved);

    // La x queda retenida en la x87 entre la suma y la resta: 2^24 + 1 no entra en un float, pero 2^24 + 2 sí. Por un
    // float daría 2^24.
    particle = initial;
    particle.position.x = 16777216.0f;
    ASSERT_TRUE(
        step([&](const Client::World::ParticleOwner&) -> std::optional<Client::World::ParticleOwnerState>
             { return Client::World::ParticleOwnerState{.alive = true, .position = moved, .start = std::nullopt}; })
            .HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.position.x), 0x4b800001u);

    particle = initial;
    const auto lost = step({});
    ASSERT_FALSE(lost.HasValue());
    EXPECT_EQ(lost.Error(), Client::World::ParticleError::MissingOwner);
}

// 0x544f7b: la /7 cae 0,6 (0x6b8a20) por paso; la /0 y la /1, 2 (0x6b8818).
TEST(NoriaParticlesTest, TheButterflySparkFallsSlowerThanTheFairySpark)
{
    for (const int subtype : {0, 1, 2, 7})
    {
        auto particles = std::make_unique<Client::World::Particles>();
        auto& particle = particles->All().front();
        particle.alive = true;
        particle.type = particle.texture = 0x5c5;
        particle.subtype = subtype;
        particle.life = 10;
        particle.position = {10, 20, 100};
        particle.verticalSpeed = 4;
        Client::Crt::CrtRandom random(7);
        const auto stepped =
            Client::World::StepParticlesChecked(*particles, random, nullptr, [](float, float) { return 0.0; });
        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(particle.position.z, 104.0f);
        EXPECT_EQ(
            particle.verticalSpeed,
            static_cast<float>(4.0 - (subtype == 7 ? static_cast<double>(std::bit_cast<float>(0x3f19999au)) : 2.0)));
        EXPECT_EQ(particle.light, glm::vec3(0.5625f));
    }
}
} // namespace
