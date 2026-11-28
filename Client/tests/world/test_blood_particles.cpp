#include "Particles/Particles.h"

#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Particles/PortableParticleTrigonometry.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

// Blood 5e4 of the damaging hit: 0x542b3b (creation), 0x546b04 (step) and 0x5487b9 (drawing). 06, "El paso del
// ataque".
namespace
{
using Client::Crt::CrtRandom;
using Client::Particles::BloodParticle;
using Client::Particles::Particle;
using Client::Particles::Particles;

constexpr std::uint32_t Seed = 0x2468aceu;
const float Brake = std::bit_cast<float>(0x3f733333u);

using Client::Math::F32;

// 0x542b3b..0x542bf3: life 12, scale ((rand() & 3) + 8) · 0.05f overwriting the requested one, turn x rand() % 360,
// velocity (0; −((rand() & 15) + 8); rand() % 6 − 3) turned by the requested angles and light (0.1f; 0; 0), with four
// rand() and the usual prefix (position in +0x14 and +0x6c, +0x4c at 1). It does not read the subtype.
TEST(BloodParticlesTest, TheBloodTakesFourDrawsAndFallsAlongTheStrikersAngles)
{
    for (const glm::vec3 angles : {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 90.0f), glm::vec3(10.0f, 20.0f, 250.0f)})
    {
        SCOPED_TRACE(angles.z);
        Particles particles;
        CrtRandom random(Seed);
        const glm::vec3 point{1000.5f, 2000.25f, 300.0f};
        const auto index =
            particles.CreateChecked(BloodParticle, point, angles, glm::vec3(1.0f), 7, 3.0f, 0.0f, random);
        ASSERT_TRUE(index.HasValue());
        ASSERT_EQ(index.Value(), std::optional<std::size_t>{0});

        CrtRandom expected(Seed);
        const int size = (expected.Next() & 3) + 8;
        const int turn = expected.Next() % 360;
        const int fall = (expected.Next() & 15) + 8;
        const int side = (expected.Next() % 6) - 3;
        const Particle& blood = particles.All()[0];
        EXPECT_TRUE(blood.alive);
        EXPECT_EQ(blood.type, BloodParticle);
        EXPECT_EQ(blood.texture, BloodParticle);
        EXPECT_EQ(blood.subtype, 7);
        EXPECT_EQ(blood.life, 12);
        EXPECT_EQ(blood.scale, F32(size * static_cast<double>(std::bit_cast<float>(0x3d4ccccdu))));
        EXPECT_EQ(blood.angles, (glm::vec3{static_cast<float>(turn), angles.y, angles.z}));
        EXPECT_EQ(blood.velocity, Client::Math::OriginalVectorRotate(
                                      glm::vec3(0.0f, -static_cast<float>(fall), static_cast<float>(side)),
                                      Client::Math::OriginalAngleMatrix(angles)));
        EXPECT_EQ(blood.light, (glm::vec3{std::bit_cast<float>(0x3dcccccdu), 0.0f, 0.0f}));
        EXPECT_EQ(blood.position, point);
        EXPECT_EQ(blood.start, point);
        EXPECT_TRUE(blood.moves);
        EXPECT_EQ(random.State(), expected.State());
        if (angles == glm::vec3(0.0f))
        {
            EXPECT_EQ(blood.velocity, (glm::vec3{0.0f, -static_cast<float>(fall), static_cast<float>(side)}));
        }
    }
}

// 0x5446e0 with 5e4: after the life and the common movement (the velocity turned by its angles, 0x4e98d0), velocity
// times 0.95f, frame (12 − life) / 3 and position plus the unturned velocity; the x with the unrounded product. It goes
// off at step 12, with frame 4. Each step takes the wind's two rand() and nothing else.
TEST(BloodParticlesTest, TheBloodBrakesCountsItsFramesAndDiesAtTwelve)
{
    Particles particles;
    CrtRandom random(Seed);
    ASSERT_TRUE(particles
                    .CreateChecked(BloodParticle, {100.0f, 200.0f, 300.0f}, glm::vec3(0.0f, 0.0f, 45.0f),
                                   glm::vec3(1.0f), 0, 1.0f, 0.0f, random)
                    .HasValue());
    Particle& blood = particles.All()[0];
    Client::Particles::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    for (int step = 1; step <= 12; ++step)
    {
        SCOPED_TRACE(step);
        Particle expected = blood;
        const glm::vec3 turned =
            Client::Math::OriginalVectorRotate(expected.velocity, Client::Math::OriginalAngleMatrix(expected.angles));
        for (int k = 0; k < 3; ++k)
            expected.position[k] = F32(static_cast<double>(turned[k]) + expected.position[k]);
        const double x = static_cast<double>(expected.velocity.x) * static_cast<double>(Brake);
        expected.velocity = {F32(x), F32(static_cast<double>(expected.velocity.y) * static_cast<double>(Brake)),
                             F32(static_cast<double>(expected.velocity.z) * static_cast<double>(Brake))};
        expected.position = {F32(x + expected.position.x),
                             F32(static_cast<double>(expected.velocity.y) + expected.position.y),
                             F32(static_cast<double>(expected.velocity.z) + expected.position.z)};
        CrtRandom wind(random.State());
        static_cast<void>(wind.Next());
        static_cast<void>(wind.Next());

        const auto stepped =
            Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry);

        ASSERT_TRUE(stepped.HasValue());
        EXPECT_EQ(blood.life, 12 - step);
        EXPECT_EQ(blood.alive, step < 12);
        EXPECT_EQ(std::bit_cast<std::int32_t>(blood.field48), step / 3);
        EXPECT_EQ(blood.velocity, expected.velocity);
        EXPECT_EQ(blood.position, expected.position);
        EXPECT_EQ(random.State(), wind.State());
    }
}

// 0x548644 → 0x5487b9: 5e4 goes with its material's blend, without turn, with the frame's cell in the 2 × 2 atlas (u
// from frame % 2 and v from frame / 2, each · 0.5f) and size (0.5f; 0.5f).
TEST(BloodParticlesTest, TheBloodDrawsItsFrameFromATwoByTwoAtlas)
{
    Particle blood;
    blood.alive = true;
    blood.type = blood.texture = BloodParticle;
    blood.scale = 0.5f;
    blood.angles = {30.0f, 0.0f, 0.0f};
    blood.rotation = 12.0f;
    blood.light = glm::vec3(0.1f, 0.0f, 0.0f);
    blood.position = {1.0f, 2.0f, 3.0f};
    for (const std::int32_t frame : {0, 1, 2, 3})
    {
        SCOPED_TRACE(frame);
        blood.field48 = std::bit_cast<float>(frame);
        const auto drawn = Client::Tests::DrawOneParticle(blood, glm::vec2(64.0f), std::uint8_t{4});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->blend, Client::Tests::RecordedParticleDraw::Blend::Alpha);
        EXPECT_EQ(drawn.Value()->texture, BloodParticle);
        EXPECT_EQ(drawn.Value()->width, 32.0f);
        EXPECT_EQ(drawn.Value()->height, 32.0f);
        EXPECT_EQ(drawn.Value()->rotation, 0.0f);
        EXPECT_EQ(drawn.Value()->position, (glm::vec3{1.0f, 2.0f, 3.0f}));
        EXPECT_EQ(drawn.Value()->light, blood.light);
        EXPECT_EQ(drawn.Value()->uv,
                  (glm::vec2{static_cast<float>(frame % 2) * 0.5f, static_cast<float>(frame / 2) * 0.5f}));
        EXPECT_EQ(drawn.Value()->uvSize, glm::vec2(0.5f));
    }
}
} // namespace
