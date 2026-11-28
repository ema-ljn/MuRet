#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Particles/KalimaParticleKernels.h"
#include "Particles/Particles.h"
#include "Particles/PortableParticleTrigonometry.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>

// The particles released by Castle Siege's objects (map 30) that do not share everything with Kalima's and Crywolf's:
// the fall 6f4/6 of the 0x34 braziers and the drawing of the torches' spark 6ec/0 (06 § Los objetos de Castle Siege).
// 6ec/0's creation and step are 6ed/0's (CrywolfParticlesTest).
namespace
{
using Client::Crt::CrtRandom;
using Client::Particles::Particle;
using Client::Particles::Particles;
using Client::Tests::RecordedParticleDraw;

using Client::Math::D;
using Client::Math::F32;

CrtRandom After(std::uint32_t seed, int draws)
{
    CrtRandom random(seed);
    for (int draw = 0; draw < draws; ++draw)
        static_cast<void>(random.Next());
    return random;
}
} // namespace

// 0x543f04..0x543f47: 6f4/6 is born with life 30, turn rand() % 360, the argument's scale plus 0.6f (0x6b8a20), fall
// −12 − rand() % 5 and light at 0.2f; Kalima's 6f4/0, instead, has scale 1.6f and fall −7 − rand() % 5.
TEST(CastleSiegeParticlesTest, TheBrazierFallAddsSixTenthsToItsScaleAndFallsFaster)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(29);
    CrtRandom draws(29);
    const glm::vec3 position(6799.0f, 13792.0f, 60.0f);
    const float scale = std::bit_cast<float>(0x3fc7ae14u);
    const auto created =
        particles->CreateChecked(Client::Particles::KalimaWaterfallLast, position, {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f),
                                 Client::Particles::BrazierFallSubtype, scale, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& fall = particles->All()[*created.Value()];
    EXPECT_EQ(fall.life, 30);
    EXPECT_EQ(fall.rotation, static_cast<float>(draws.Next() % 360));
    EXPECT_EQ(fall.scale, F32(D(scale) + D(std::bit_cast<float>(0x3f19999au))));
    EXPECT_EQ(fall.velocity, glm::vec3(0.0f, 0.0f, static_cast<float>(-12 - draws.Next() % 5)));
    EXPECT_EQ(fall.light, glm::vec3(std::bit_cast<float>(0x3e4ccccdu)));
    EXPECT_EQ(fall.position, position);
    EXPECT_TRUE(fall.moves);
    EXPECT_EQ(random.State(), draws.State());

    // The other 0x6f4 subtypes, which no ported map releases, still have no creation.
    EXPECT_FALSE(particles
                     ->CreateChecked(Client::Particles::KalimaWaterfallLast, position, {}, glm::vec3(1.0f), 5, scale,
                                     0.0f, random)
                     .HasValue());
    EXPECT_EQ(random.State(), draws.State());
}

// 0x547e7e..0x547fa7: /6 is neither /0 nor /5, which subtract 0.005f from the scale and add 0.1f to the fall; it jumps
// to 0x547fc1, which only changes the light: by 1.1f (0x6b8830) with life above 20 and by 0.8333f (0x6b8b98) below 8.
// The common movement (0x4e98d0) lowers it with its fall turned by the angles. No rand() besides the wind.
TEST(CastleSiegeParticlesTest, TheBrazierFallKeepsItsScaleAndSpeedAndOnlyChangesItsLight)
{
    auto particles = std::make_unique<Particles>();
    CrtRandom random(31);
    const auto created = particles->CreateChecked(Client::Particles::KalimaWaterfallLast, {6799.0f, 13792.0f, 60.0f},
                                                  {0.0f, 0.0f, 45.0f}, glm::vec3(1.0f),
                                                  Client::Particles::BrazierFallSubtype, 1.5f, 0.0f, random);
    ASSERT_TRUE(created.HasValue());
    ASSERT_TRUE(created.Value().has_value());
    const Particle& fall = particles->All()[*created.Value()];
    Particle expected = fall;
    const std::uint32_t born = random.State();
    constexpr double Brighten = static_cast<double>(std::bit_cast<float>(0x3f8ccccdu));
    constexpr double Fade = static_cast<double>(std::bit_cast<float>(0x3f555555u));
    const float clock = 2500.0f;
    Client::Particles::PortableParticleTrigonometry trigonometry;
    constexpr int Steps = 30;
    for (int step = 1; step <= Steps; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Particles::StepParticlesChecked(*particles, random, std::cref(clock), nullptr, trigonometry)
                        .HasValue());
        expected.life -= 1;
        const glm::vec3 turned =
            Client::Math::OriginalVectorRotate(expected.velocity, Client::Math::OriginalAngleMatrix(expected.angles));
        for (int axis = 0; axis < 3; ++axis)
            expected.position[axis] = F32(D(turned[axis]) + D(expected.position[axis]));
        const double factor = expected.life < 8 ? Fade : expected.life > 20 ? Brighten : 1.0;
        if (factor != 1.0)
        {
            for (int axis = 0; axis < 3; ++axis)
                expected.light[axis] = F32(D(expected.light[axis]) * factor);
        }
        EXPECT_EQ(fall.alive, expected.life > 0);
        EXPECT_EQ(fall.life, expected.life);
        EXPECT_EQ(fall.position, expected.position);
        EXPECT_EQ(fall.light, expected.light);
        EXPECT_EQ(fall.scale, expected.scale);
        EXPECT_EQ(fall.velocity, expected.velocity);
        EXPECT_EQ(fall.rotation, expected.rotation);
    }
    EXPECT_EQ(random.State(), After(born, 2 * Steps).State());
}

// 0x548550: 0x6ec and 0x6f4 fall to the default (0x54889a), the common billboard with their texture, the image's width
// and height times the scale, the light and the turn of +0x44, without reading the subtype or requesting another blend.
TEST(CastleSiegeParticlesTest, TheSparkAndTheFallDrawTheCommonBillboard)
{
    for (const auto& [type, subtype] : {std::pair<std::int32_t, std::int32_t>{Client::Particles::CrywolfFanta, 0},
                                        std::pair<std::int32_t, std::int32_t>{Client::Particles::KalimaWaterfallLast,
                                                                              Client::Particles::BrazierFallSubtype}})
    {
        SCOPED_TRACE(type);
        Particle particle;
        particle.alive = true;
        particle.type = particle.texture = type;
        particle.subtype = subtype;
        particle.scale = 1.25f;
        particle.position = {6799.0f, 13792.0f, 60.0f};
        particle.light = glm::vec3(0.75f, 0.5f, 0.25f);
        particle.rotation = 123.0f;
        const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(64.0f, 128.0f), std::uint8_t{3});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        const RecordedParticleDraw& draw = *drawn.Value();
        EXPECT_EQ(draw.texture, type);
        EXPECT_EQ(draw.blend, RecordedParticleDraw::Blend::Additive);
        EXPECT_EQ(draw.position, particle.position);
        EXPECT_EQ(draw.width, 80.0f);
        EXPECT_EQ(draw.height, 160.0f);
        EXPECT_EQ(draw.light, particle.light);
        EXPECT_EQ(draw.rotation, 123.0f);
        EXPECT_EQ(draw.uv, glm::vec2(0.0f));
        EXPECT_EQ(draw.uvSize, glm::vec2(1.0f));
    }
}
