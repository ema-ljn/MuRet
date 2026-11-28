#include "Particles/Particles.h"

#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Math/Ftol.h"
#include "Particles/PortableParticleTrigonometry.h"
#include "Terrain/TerrainLighting.h"
#include "Terrain/DynamicWorldLight.h"

#include "support/ParticleDrawRecorder.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <tuple>

// The particles requested by the attack step and the killer's joints: spark 0x5c6/12 of 0x815 (0x540a04, 0x545397)
// and lightning 0x5d5/0 and /1 of 0x618 (0x540bbe, 0x540bfe, 0x545419). 06, "El paso del ataque".
namespace
{
using Client::Crt::CrtRandom;
using Client::Particles::Particle;
using Client::Particles::Particles;

constexpr std::uint32_t Seed = 0x1357bdfu;

using Client::Math::F32;

double Bits(std::uint32_t bits)
{
    return static_cast<double>(std::bit_cast<float>(bits));
}

// 0x540a04..0x540b41: texture 0x5fc, +0x38 at 1.0f, the position shifted by (0; −100; 0) turned and then by
// (rand() & 15) − 8 in x and y and 150 in z, velocity ((rand() % 20) · 0.1f − 1; same; −(rand() % 3) − 3), life 40 and
// the requested scale minus (rand() % 15) · 0.1f: six rand(). Light and angles, the requested ones.
TEST(AttackParticlesTest, TheBowSparkTakesSixDrawsAndStartsAboveItsOwner)
{
    Particles particles;
    CrtRandom random(Seed);
    const glm::vec3 point{1000.5f, 2000.25f, 50.0f};
    const glm::vec3 angles{0.0f, 0.0f, 90.0f};
    const glm::vec3 light{0.5f, 0.25f, 1.0f};
    const auto index = particles.CreateChecked(Client::Particles::SparkParticle, point, angles, light,
                                               Client::Particles::BowSparkSubtype, 2.0f, 0.0f, random);
    ASSERT_TRUE(index.HasValue());
    ASSERT_EQ(index.Value(), std::optional<std::size_t>{0});

    CrtRandom expected(Seed);
    const glm::vec3 pushed =
        Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -100.0f, 0.0f), Client::Math::OriginalAngleMatrix(angles));
    glm::vec3 at{};
    for (int k = 0; k < 3; ++k)
        at[k] = F32(static_cast<double>(pushed[k]) + static_cast<double>(point[k]));
    at.x = F32((static_cast<double>(expected.Next() & 15) + static_cast<double>(at.x)) - 8.0);
    at.y = F32((static_cast<double>(expected.Next() & 15) + static_cast<double>(at.y)) - 8.0);
    at.z = F32(static_cast<double>(at.z) + 150.0);
    const double tenth = Bits(0x3dcccccdu);
    const float vx = F32((static_cast<double>(expected.Next() % 20) * tenth) - 1.0);
    const float vy = F32((static_cast<double>(expected.Next() % 20) * tenth) - 1.0);
    const float vz = F32(-static_cast<double>(expected.Next() % 3) - 3.0);
    const float scale = F32(2.0 - (static_cast<double>(expected.Next() % 15) * tenth));

    const Particle& spark = particles.All()[0];
    EXPECT_EQ(spark.texture, 0x5fc);
    EXPECT_EQ(spark.field38, std::bit_cast<std::uint32_t>(1.0f));
    EXPECT_EQ(spark.life, 40);
    EXPECT_EQ(spark.position, at);
    EXPECT_EQ(spark.start, point);
    EXPECT_EQ(spark.velocity, (glm::vec3{vx, vy, vz}));
    EXPECT_EQ(spark.scale, scale);
    EXPECT_EQ(spark.angles, angles);
    EXPECT_EQ(spark.light, light);
    EXPECT_TRUE(spark.moves);
    EXPECT_EQ(random.State(), expected.State());
}

// 0x545397 and 0x545340: after the wind (two rand()) and the common movement, rand() & 1 − 0.6f in the red and the
// green and − 0.8f in the blue, scale − 0.04f and +0x38 − 0.001f. It dies by life at step 40.
TEST(AttackParticlesTest, TheBowSparkFlickersShrinksAndDiesAtForty)
{
    Particles particles;
    CrtRandom random(Seed);
    ASSERT_TRUE(particles
                    .CreateChecked(Client::Particles::SparkParticle, {100.0f, 200.0f, 300.0f}, glm::vec3(0.0f),
                                   glm::vec3(1.0f), Client::Particles::BowSparkSubtype, 2.0f, 0.0f, random)
                    .HasValue());
    Particle& spark = particles.All()[0];
    Client::Particles::PortableParticleTrigonometry trigonometry;
    const float clock = 0.0f;
    const std::int32_t map = 0;
    for (int step = 1; step <= 40; ++step)
    {
        SCOPED_TRACE(step);
        const float scale = spark.scale;
        const float faded = std::bit_cast<float>(spark.field38);
        CrtRandom expected(random.State());
        static_cast<void>(expected.Next());
        static_cast<void>(expected.Next());
        const auto flicker = static_cast<double>(expected.Next() & 1);

        ASSERT_TRUE(Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry)
                        .HasValue());

        EXPECT_EQ(spark.life, 40 - step);
        EXPECT_EQ(spark.alive, step < 40);
        EXPECT_EQ(spark.light, (glm::vec3{F32(flicker - Bits(0x3f19999au)), F32(flicker - Bits(0x3f19999au)),
                                          F32(flicker - Bits(0x3f4ccccdu))}));
        EXPECT_EQ(spark.scale, F32(static_cast<double>(scale) - Bits(0x3d23d70au)));
        EXPECT_EQ(std::bit_cast<float>(spark.field38), F32(static_cast<double>(faded) - Bits(0x3a83126fu)));
        EXPECT_EQ(random.State(), expected.State());
    }
}

// 0x540bbe and 0x540bfe: /0 with the prefix's life (2) and scale 0.15f, /1 with life 10 and scale 0; both with the
// requested light plus 0.5. No rand(). Their step (0x545419) turns with the clock and requests the dynamic light of
// radius 3; without it, it cuts.
TEST(AttackParticlesTest, TheLightningTakesNoDrawsAndLightsTheGround)
{
    const glm::vec3 point{1200.0f, 3400.0f, 200.0f};
    const glm::vec3 light{0.4f, 0.3f, 0.2f};
    for (const std::int32_t subtype :
         {Client::Particles::LightningTrailSubtype, Client::Particles::LightningHitSubtype})
    {
        SCOPED_TRACE(subtype);
        Particles particles;
        CrtRandom random(Seed);
        ASSERT_TRUE(particles
                        .CreateChecked(Client::Particles::LightningParticle, point, glm::vec3(0.0f), light, subtype,
                                       1.0f, 0.0f, random)
                        .HasValue());
        EXPECT_EQ(random.State(), CrtRandom(Seed).State());
        Particle& bolt = particles.All()[0];
        const bool hit = subtype == Client::Particles::LightningHitSubtype;
        EXPECT_EQ(bolt.life, hit ? 10 : 2);
        EXPECT_EQ(bolt.scale, hit ? 0.0f : std::bit_cast<float>(0x3e19999au));
        EXPECT_EQ(bolt.light,
                  (glm::vec3{F32(static_cast<double>(light.x) + 0.5), F32(static_cast<double>(light.y) + 0.5),
                             F32(static_cast<double>(light.z) + 0.5)}));

        Client::Particles::PortableParticleTrigonometry trigonometry;
        const float clock = 123456.75f;
        const std::int32_t map = 0;
        auto lighting = std::make_unique<Client::Terrain::TerrainLighting>();
        ASSERT_TRUE(Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry, {},
                                                            {}, {}, {}, lighting.get())
                        .HasValue());
        EXPECT_EQ(bolt.rotation, F32(static_cast<double>(Client::Math::FtolLow(static_cast<double>(clock)) % 1000) *
                                     Bits(0x3a83126fu)));
        const double glow = static_cast<double>(bolt.life) * Bits(0x3dcccccdu);
        const glm::vec3 color{F32(glow * 0.5), F32(glow), F32(static_cast<double>(F32(glow)) * Bits(0x3f4ccccdu))};
        auto expectedLighting = std::make_unique<Client::Terrain::TerrainLighting>();
        ASSERT_TRUE(
            Client::Terrain::AddDynamicLight(expectedLighting.get(), bolt.position, color, 3, "test").HasValue());
        EXPECT_EQ(lighting->cells, expectedLighting->cells);

        EXPECT_FALSE(Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), &map, trigonometry)
                         .HasValue());
    }
}

// 0x548644: 0x5d5 goes to the common billboard (0x54889a) with its texture, and 0x5c6/12 with its creation's 0x5fc.
TEST(AttackParticlesTest, TheLightningAndTheBowSparkUseTheCommonBillboard)
{
    for (const auto& [type, subtype, texture] : {std::tuple{Client::Particles::LightningParticle, 0, 0x5d5},
                                                 std::tuple{static_cast<std::int32_t>(Client::Particles::SparkParticle),
                                                            Client::Particles::BowSparkSubtype, 0x5fc}})
    {
        SCOPED_TRACE(type);
        Particle particle;
        particle.alive = true;
        particle.type = type;
        particle.subtype = subtype;
        particle.texture = texture;
        particle.scale = 1.0f;
        particle.light = glm::vec3(0.5f);
        particle.position = {1.0f, 2.0f, 3.0f};
        const auto drawn = Client::Tests::DrawOneParticle(particle, glm::vec2(64.0f), std::uint8_t{4});
        ASSERT_TRUE(drawn.HasValue());
        ASSERT_TRUE(drawn.Value().has_value());
        EXPECT_EQ(drawn.Value()->texture, texture);
        EXPECT_EQ(drawn.Value()->position, particle.position);
    }
}
} // namespace
