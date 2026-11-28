#include "Crt/CrtRandom.h"
#include "Math/OriginalRotation.h"
#include "support/ParticleDrawRecorder.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"

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
        auto particles = std::make_unique<Client::Particles::Particles>();
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
        auto particles = std::make_unique<Client::Particles::Particles>();
        auto& particle = particles->All().front();
        particle.alive = true;
        particle.type = particle.texture = 0x5fc;
        particle.subtype = subtype;
        particle.life = 18;
        particle.scale = 9.0f;
        particle.rotation = 30.0f;
        Client::Crt::CrtRandom random(7);
        const auto stepped = Client::Particles::StepParticlesChecked(*particles, random);
        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(particle.life, 17);
        EXPECT_NEAR(particle.scale, subtype == 0 ? 0.17364827f : 0.13023620f, 0.0000001f);
        EXPECT_EQ(particle.rotation, subtype == 0 ? 30.0f : 18.0f);
        EXPECT_EQ(random.State(), 0xc40e66b9u); // Only the wind's two rand.
    }
}

TEST(NoriaParticlesTest, TheFairySparkKeepsFiveRandomDrawsAndItsStoredMotion)
{
    auto particles = std::make_unique<Client::Particles::Particles>();
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

// 0x5404ad..0x5404ca: /2 doubles the scale and triples the local vector before 0x5fcbd0; the rest as /0.
TEST(NoriaParticlesTest, ThePushedSparkDoublesTheScaleAndTriplesTheForwardSpeed)
{
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto ordinary = std::make_unique<Client::Particles::Particles>();
        auto pushed = std::make_unique<Client::Particles::Particles>();
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
    auto particles = std::make_unique<Client::Particles::Particles>();
    auto& particle = particles->All().front();
    particle.alive = true;
    particle.type = particle.texture = 0x5c5;
    particle.life = 10;
    particle.position = {10, 20, 100};
    particle.verticalSpeed = 4;
    particle.velocity = {1, 2, 3};
    const auto initial = particle;
    Client::Crt::CrtRandom random(7);
    const auto missing = Client::Particles::StepParticlesChecked(*particles, random);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Error(), Client::Particles::ParticleError::MissingTerrainHeight);
    EXPECT_EQ(particle.position, glm::vec3(10, 20, 104));
    EXPECT_EQ(particle.verticalSpeed, 2.0f);
    EXPECT_EQ(particle.life, 9);
    EXPECT_EQ(particle.light, glm::vec3(0.5625f));
    particle = initial;
    int calls = 0;
    const auto stepped = Client::Particles::StepParticlesChecked(*particles, random, nullptr,
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
        Client::Particles::Particle particle;
        particle.alive = true;
        particle.type = particle.texture = type;
        particle.subtype = type == Client::Particles::FairySparkParticle ? 2 : 0;
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
// 0x54038c..0x5403a7: 5c5/7 (the selection butterfly's sparks) rolls its scale like /0 and then only its life
// (12..23): without turn, velocity or the argument's scale.
TEST(NoriaParticlesTest, TheButterflySparkDrawsOnlyItsScaleAndItsLife)
{
    auto particles = std::make_unique<Client::Particles::Particles>();
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

// 0x5403eb..0x540406: 5c5/1 (the sparks of staff 0xc18, 0x50f5a8) rolls its scale like /0 and then only its life,
// (rand() & 7) + 8: without turn, velocity or the argument's scale.
TEST(NoriaParticlesTest, TheStaffSparkDrawsOnlyItsScaleAndItsLife)
{
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto particles = std::make_unique<Client::Particles::Particles>();
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

// 0x540333..0x54055a: /3 is born like /0; /4 triples the scale and multiplies the local vector by 10 (0x5404cc); those
// outside the table (/9, −1) take /1's path (0x5403eb). /5, below.
TEST(NoriaParticlesTest, TheOtherFairySparksFollowTheirBranch)
{
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto ordinary = std::make_unique<Client::Particles::Particles>();
        Client::Crt::CrtRandom first(seed);
        const auto base = ordinary->CreateChecked(0x5c5, {10, 20, 30}, {170, 0, 12}, {1, 1, 1}, 0, 1, 0, first);
        ASSERT_TRUE(base.HasValue());
        ASSERT_TRUE(base.Value().has_value());
        const auto& original = ordinary->All()[*base.Value()];
        for (const int subtype : {3, 4})
        {
            SCOPED_TRACE(subtype);
            auto particles = std::make_unique<Client::Particles::Particles>();
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
                      Client::Math::OriginalVectorRotate(glm::vec3(0.0f, local, 0.0f),
                                                         Client::Math::OriginalAngleMatrix(original.angles)));
            EXPECT_EQ(random.State(), first.State());
        }
        for (const int subtype : {9, -1})
        {
            SCOPED_TRACE(subtype);
            auto particles = std::make_unique<Client::Particles::Particles>();
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

// 0x540333 and 0x5403ad..0x5403e9: /5 (that of 0x140's step, 0x52a16e) takes the scale ((rand() & 3) + 4) · 0.1f that
// it then overwrites with the requested one, reads its owner's position (+0x40 → +0x10) for its start (+0x6c), lives
// (rand() & 3) + 12 and leaves with rise at 0, texture 0x5c6 and the prefix's velocity (zero). Two rand(). Without the
// owner's position, CreateChecked rejects it before touching the slot; CreateOwnedChecked does not know it. With the
// table full, nothing.
TEST(NoriaParticlesTest, TheOwnerBirthFairySparkStartsAtItsOwner)
{
    const Client::Particles::ParticleOwner owner{Client::Particles::ParticleOwnerKind::ModelEffect, 3};
    const glm::vec3 ownerAt{500.0f, 600.0f, 70.0f};
    Client::Particles::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto particles = std::make_unique<Client::Particles::Particles>();
        Client::Crt::CrtRandom random(seed);
        const auto created =
            particles->CreateTowardChecked(0x5c5, {10, 20, 30}, {170, 0, 12}, {0.5f, 0.5f, 0.5f}, 5, 2.0f,
                                           std::cref(clock), random, trigonometry, owner, ownerAt);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const auto& particle = particles->All()[*created.Value()];
        Client::Crt::CrtRandom draws(seed);
        static_cast<void>(draws.Next()); // 0x540333, the overwritten scale.
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
            EXPECT_EQ(particle.life, 12 + 3); // seed 1's second rand() is 18467 (& 3 = 3).
        }
    }
    auto particles = std::make_unique<Client::Particles::Particles>();
    Client::Crt::CrtRandom random(7);
    const auto missing = particles->CreateChecked(0x5c5, {10, 20, 30}, {}, {1, 1, 1}, 5, 1, 0, random);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Error(), Client::Particles::ParticleError::MissingOwner);
    const auto unknown = particles->CreateOwnedChecked(0x5c5, {10, 20, 30}, {}, {1, 1, 1}, 5, 1, 0, random, 0, owner);
    ASSERT_FALSE(unknown.HasValue());
    EXPECT_EQ(unknown.Error(), Client::Particles::ParticleError::UnsupportedSubtype);
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

// 0x544f29..0x545040: /5 lights up with life · 0.0625 (it does not overwrite the light like /6), falls 0.3f
// (0x544f4c), moves and subtracts how far the owner moved from its start, which becomes its position (0x544fe4 →
// 0x544fef). The dead owner too: only its position is read. Without owner, MissingOwner.
TEST(NoriaParticlesTest, TheOwnerBirthFairySparkMovesWithItsOwner)
{
    auto particles = std::make_unique<Client::Particles::Particles>();
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
    particle.typedOwner = {Client::Particles::ParticleOwnerKind::ModelEffect, 3};
    const auto initial = particle;
    const glm::vec3 moved{6.0f, 7.0f, 8.0f};
    Client::Particles::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    const auto step = [&](const Client::Particles::ParticleOwnerStates& owners)
    {
        Client::Crt::CrtRandom random(7);
        return Client::Particles::StepParticlesChecked(
            *particles, random, std::cref(clock), &map, trigonometry, [](float, float) { return 0.0; }, {}, {}, {},
            nullptr, {}, {}, {}, owners);
    };
    const auto stepped = step(
        [&](const Client::Particles::ParticleOwner& owner) -> std::optional<Client::Particles::ParticleOwnerState>
        {
            EXPECT_EQ(owner, initial.typedOwner);
            return Client::Particles::ParticleOwnerState{.alive = false, .position = moved, .start = std::nullopt};
        });
    ASSERT_TRUE(stepped.HasValue());
    // 13 · 0.0625 = 0.8125 and 4 − 0.3f (0x3e99999a) = 3.7f (0x406ccccd), computed separately.
    EXPECT_EQ(particle.life, 13);
    EXPECT_EQ(particle.light, glm::vec3(0.8125f));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.verticalSpeed), 0x406ccccdu);
    EXPECT_EQ(particle.position, glm::vec3(12.5f, 24.25f, 110.0f));
    EXPECT_EQ(particle.start, moved);

    // The retained x: 2^24 + 1 does not fit a float and, with the start 1 beyond the owner, returns to 2^24. Through a
    // float it would give 2^24 − 1 (0x4b7fffff).
    particle = initial;
    particle.position.x = 16777216.0f;
    particle.start.x = 7.0f;
    ASSERT_TRUE(
        step([&](const Client::Particles::ParticleOwner&) -> std::optional<Client::Particles::ParticleOwnerState>
             { return Client::Particles::ParticleOwnerState{.alive = true, .position = moved, .start = std::nullopt}; })
            .HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.position.x), 0x4b800000u);

    particle = initial;
    const auto lost = step({});
    ASSERT_FALSE(lost.HasValue());
    EXPECT_EQ(lost.Error(), Client::Particles::ParticleError::MissingOwner);
}

// 0x5404f9..0x54053c: /6 (0x529f71's) takes /0's five rand(), then the life (rand() & 3) + 4 and the local vector
// (0; (rand() % 20 + 20) · 0.2f; 0); it takes the requested scale, texture 0x5c6 and rise at 0. With
// CreateOwnedChecked it keeps its owner.
TEST(NoriaParticlesTest, TheOwnedFairySparkDrawsSevenTimesAndKeepsItsOwner)
{
    for (const std::uint32_t seed : {1u, 7u, 99u})
    {
        SCOPED_TRACE(seed);
        auto particles = std::make_unique<Client::Particles::Particles>();
        Client::Crt::CrtRandom random(seed);
        const Client::Particles::ParticleOwner owner{Client::Particles::ParticleOwnerKind::ModelEffect, 4};
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
        EXPECT_EQ(particle.velocity, Client::Math::OriginalVectorRotate(glm::vec3(0.0f, forward, 0.0f),
                                                                        Client::Math::OriginalAngleMatrix(angles)));
        EXPECT_EQ(particle.typedOwner, owner);
        EXPECT_EQ(random.State(), draws.State());
        if (seed == 1)
        {
            // Fixed values, computed apart from the CRT's RNG: life 4, turn 214, forward 7.6f.
            EXPECT_EQ(particle.life, 4);
            EXPECT_EQ(particle.angles.z, 214.0f);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(forward), 0x40f33333u);
            EXPECT_EQ(random.State(), 752224798u);
        }
    }
}

// 0x544f57..0x545040: /6 falls 0.3f, its light is 0.9f, it moves and subtracts how far the owner moved from its start
// (the x retained, the y and the z stored); the start becomes the owner's position. Without owner, MissingOwner.
TEST(NoriaParticlesTest, TheOwnedFairySparkMovesWithItsOwner)
{
    auto particles = std::make_unique<Client::Particles::Particles>();
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
    particle.typedOwner = {Client::Particles::ParticleOwnerKind::ModelEffect, 4};
    const auto initial = particle;
    const glm::vec3 moved{6.0f, 7.0f, 8.0f};
    Client::Particles::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    const auto step = [&](const Client::Particles::ParticleOwnerStates& owners)
    {
        Client::Crt::CrtRandom random(7);
        return Client::Particles::StepParticlesChecked(
            *particles, random, std::cref(clock), &map, trigonometry, [](float, float) { return 0.0; }, {}, {}, {},
            nullptr, {}, {}, {}, owners);
    };
    const auto stepped = step(
        [&](const Client::Particles::ParticleOwner& owner) -> std::optional<Client::Particles::ParticleOwnerState>
        {
            EXPECT_EQ(owner, initial.typedOwner);
            return Client::Particles::ParticleOwnerState{.alive = true, .position = moved, .start = std::nullopt};
        });
    ASSERT_TRUE(stepped.HasValue());
    const double fall = static_cast<double>(std::bit_cast<float>(0x3e99999au));
    EXPECT_EQ(particle.life, 9);
    EXPECT_EQ(particle.light, glm::vec3(std::bit_cast<float>(0x3f666666u)));
    EXPECT_EQ(particle.verticalSpeed, static_cast<float>(4.0 - fall));
    EXPECT_EQ(particle.position, glm::vec3(12.5f, 24.25f, 110.0f));
    EXPECT_EQ(particle.start, moved);

    // The x stays retained on the x87 between the add and the subtract: 2^24 + 1 does not fit a float, but 2^24 + 2
    // does. Through a float it would give 2^24.
    particle = initial;
    particle.position.x = 16777216.0f;
    ASSERT_TRUE(
        step([&](const Client::Particles::ParticleOwner&) -> std::optional<Client::Particles::ParticleOwnerState>
             { return Client::Particles::ParticleOwnerState{.alive = true, .position = moved, .start = std::nullopt}; })
            .HasValue());
    EXPECT_EQ(std::bit_cast<std::uint32_t>(particle.position.x), 0x4b800001u);

    particle = initial;
    const auto lost = step({});
    ASSERT_FALSE(lost.HasValue());
    EXPECT_EQ(lost.Error(), Client::Particles::ParticleError::MissingOwner);
}

// 0x544f7b: /7 falls 0.6 (0x6b8a20) per step; /0 and /1, 2 (0x6b8818).
TEST(NoriaParticlesTest, TheButterflySparkFallsSlowerThanTheFairySpark)
{
    for (const int subtype : {0, 1, 2, 7})
    {
        auto particles = std::make_unique<Client::Particles::Particles>();
        auto& particle = particles->All().front();
        particle.alive = true;
        particle.type = particle.texture = 0x5c5;
        particle.subtype = subtype;
        particle.life = 10;
        particle.position = {10, 20, 100};
        particle.verticalSpeed = 4;
        Client::Crt::CrtRandom random(7);
        const auto stepped =
            Client::Particles::StepParticlesChecked(*particles, random, nullptr, [](float, float) { return 0.0; });
        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(particle.position.z, 104.0f);
        EXPECT_EQ(
            particle.verticalSpeed,
            static_cast<float>(4.0 - (subtype == 7 ? static_cast<double>(std::bit_cast<float>(0x3f19999au)) : 2.0)));
        EXPECT_EQ(particle.light, glm::vec3(0.5625f));
    }
}
} // namespace
