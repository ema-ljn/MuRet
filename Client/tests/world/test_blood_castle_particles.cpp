#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Render/TextureEnvMode.h"
#include "Particles/BloodCastleParticleKernels.h"
#include "Particles/Particles.h"
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

// The particles of Blood Castle's torch 0x25: spark 6ab/0 and /1, flame 6ac/0, smoke 622/6 and orbit 60f/4 (06 § Los
// objetos de Devil Square y de Blood Castle).
namespace
{
using Client::Crt::CrtRandom;
using Client::Particles::Particle;
using Client::Particles::Particles;
using Client::Render::TextureEnvMode;
using Client::Tests::RecordedParticleDraw;

constexpr std::int32_t Spark = 0x6ab;
constexpr std::int32_t Flame = 0x6ac;
constexpr std::int32_t Cloud = 0x622;
constexpr std::int32_t Orbit = 0x60f;

double Bits(std::uint32_t bits)
{
    return static_cast<double>(std::bit_cast<float>(bits));
}

using Client::Math::F32;

const glm::vec3 Where{1000.5f, 2000.25f, 300.75f};
const glm::vec3 Turned{0.0f, 0.0f, 45.0f};

Particle Created(std::int32_t type, std::int32_t subtype, CrtRandom& random)
{
    Particles particles;
    const auto made = particles.CreateChecked(type, Where, Turned, glm::vec3(1.0f), subtype, 1.0f, 0.0f, random);
    EXPECT_TRUE(made.HasValue());
    EXPECT_EQ(made.HasValue() ? made.Value() : std::nullopt, std::optional<std::size_t>(0));
    return particles.All()[0];
}
} // namespace

// The torch shares the flame family with the siege fragments' 6ac/1 and joint 0x632/6's 6ac/2.
TEST(BloodCastleParticlesTest, TheTorchAndSharedFlameSubtypesBelongToTheFamily)
{
    EXPECT_TRUE(Client::Particles::IsBloodCastleParticle(Spark, 0));
    EXPECT_TRUE(Client::Particles::IsBloodCastleParticle(Spark, 1));
    EXPECT_TRUE(Client::Particles::IsBloodCastleParticle(Flame, 0));
    EXPECT_TRUE(Client::Particles::IsBloodCastleParticle(Flame, 1));
    EXPECT_TRUE(Client::Particles::IsBloodCastleParticle(Flame, 2));
    EXPECT_TRUE(Client::Particles::IsBloodCastleParticle(Cloud, 6));
    EXPECT_FALSE(Client::Particles::IsBloodCastleParticle(Spark, 2));
    EXPECT_FALSE(Client::Particles::IsBloodCastleParticle(Flame, 3));
    EXPECT_FALSE(Client::Particles::IsBloodCastleParticle(Cloud, 8));
    EXPECT_FALSE(Client::Particles::IsBloodCastleParticle(Orbit, 4));
}

// 0x5436fe..0x543832: life from the first rand(), turn at 0 and four more rand(); subtype 0 with 0.02f + 0.5 and
// 0.4f/0.4f/0.2f, 1 with 0.1f + 1 and 0.2f/0.2f/0.1f.
TEST(BloodCastleParticlesTest, TheSparkIsCreatedWithFiveDraws)
{
    for (const int subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        CrtRandom random(7);
        const Particle p = Created(Spark, subtype, random);
        CrtRandom draws(7);
        const int life = draws.Next() % 5 + 20;
        const int size = draws.Next() % 10;
        const int forward = draws.Next() % 10 + 5;
        const int side = draws.Next() % 10 - 5;
        const int rise = draws.Next() % 10 + 5;
        const bool narrow = subtype == 0;
        const double sideStep = narrow ? Bits(0x3ecccccdu) : Bits(0x3e4ccccdu);
        EXPECT_EQ(p.life, life);
        EXPECT_EQ(p.rotation, 0.0f);
        EXPECT_EQ(p.scale, narrow ? F32(size * Bits(0x3ca3d70au) + 0.5) : F32(size * Bits(0x3dcccccdu) + 1.0));
        EXPECT_EQ(p.velocity.x, F32(forward * sideStep));
        EXPECT_EQ(p.velocity.y, F32(side * sideStep));
        EXPECT_EQ(p.velocity.z, F32(rise * (narrow ? Bits(0x3e4ccccdu) : Bits(0x3dcccccdu))));
        EXPECT_EQ(p.texture, Spark);
        EXPECT_EQ(p.position, Where);
        EXPECT_EQ(p.angles, Turned);
        EXPECT_TRUE(p.moves);
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x543a5d..0x543ae0 and the 6ec case (0x543ae3..0x543bdc): life rand() % 5 + 25 is overwritten by 24, and the x and
// the z are rolled twice; the y at 0, scale 0.5 and the turn from the second rand().
TEST(BloodCastleParticlesTest, TheFlameDrawsItsSpeedTwiceAndLivesTwentyFourSteps)
{
    CrtRandom random(11);
    const Particle p = Created(Flame, 0, random);
    CrtRandom draws(11);
    static_cast<void>(draws.Next());
    const int turn = draws.Next() % 360;
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    const int side = draws.Next() % 10 - 5;
    const int rise = draws.Next() % 10 + 5;
    EXPECT_EQ(p.life, 24);
    EXPECT_EQ(p.scale, 0.5f);
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.velocity, glm::vec3(F32(side * Bits(0x3ecccccdu)), 0.0f, F32(rise * Bits(0x3e4ccccdu))));
    EXPECT_EQ(p.start, Where);
    EXPECT_EQ(random.State(), draws.State());
}

// 0x53c042 creates 6ac/2 with scale 3 (0x40400000), but 0x543a7b overwrites it with 0.5: only /1 uses it (0x543a84).
// The rest is /0's, with the same six rand() (Q-C393).
TEST(BloodCastleParticlesTest, TheBrightFlameIsCreatedLikeTheFlameAndIgnoresItsScale)
{
    Particles particles;
    CrtRandom random(11);
    const glm::vec3 light{1.0f, 0.5f, 0.25f};
    const float scale = std::bit_cast<float>(0x40400000u);
    const auto made = particles.CreateChecked(Flame, Where, Turned, light, 2, scale, 0.0f, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), std::optional<std::size_t>(0));
    const Particle& p = particles.All()[0];
    CrtRandom flameRandom(11);
    const Particle flame = Created(Flame, 0, flameRandom);
    EXPECT_EQ(p.subtype, 2);
    EXPECT_EQ(p.life, 24);
    EXPECT_EQ(p.scale, 0.5f);
    EXPECT_EQ(p.rotation, flame.rotation);
    EXPECT_EQ(p.velocity, flame.velocity);
    EXPECT_EQ(p.start, Where);
    EXPECT_EQ(p.light, light);
    EXPECT_EQ(p.texture, Flame);
    EXPECT_EQ(random.State(), flameRandom.State());
}

// 0x5479d5..0x547a0f: /2 reads the subtype before the product and leaves the light at life · 0.08f − 1, which with life
// at 12 or less is negative (the fixed function clamps it to 0 in glColor). The rest of the step is /0's.
TEST(BloodCastleParticlesTest, TheBrightFlameLightIsLifeTimesEightHundredthsMinusOne)
{
    for (const int life : {23, 13, 12, 1})
    {
        SCOPED_TRACE(life);
        Particle p;
        p.alive = true;
        p.type = p.texture = Flame;
        p.subtype = 2;
        p.life = life;
        p.scale = 0.5f;
        p.rotation = 123.0f;
        p.position = {10.5f, 20.25f, 30.125f};
        p.velocity = {1.5f, 0.0f, 0.7f};
        Particle plain = p;
        plain.subtype = 0;
        CrtRandom random(23);
        CrtRandom plainRandom(23);
        Client::Particles::StepBloodCastleParticle(p, random);
        Client::Particles::StepBloodCastleParticle(plain, plainRandom);
        EXPECT_EQ(p.light, glm::vec3(F32(static_cast<double>(life) * Bits(0x3da3d70au) - 1.0)));
        EXPECT_EQ(p.light.x < 0.0f, life <= 12);
        EXPECT_EQ(p.position, plain.position);
        EXPECT_EQ(p.velocity, plain.velocity);
        EXPECT_EQ(p.scale, plain.scale);
        EXPECT_EQ(p.rotation, plain.rotation);
        EXPECT_EQ(random.State(), plainRandom.State());
    }
}

// 0x54327b..0x543300: life rand() % 5 + 25, scale 0.2f, and the flame's turn and velocity before the 6ec.
TEST(BloodCastleParticlesTest, TheSmokeIsCreatedLikeTheFlameBeforeTheSixEc)
{
    CrtRandom random(13);
    const Particle p = Created(Cloud, 6, random);
    CrtRandom draws(13);
    const int life = draws.Next() % 5 + 25;
    const int turn = draws.Next() % 360;
    const int forward = draws.Next() % 10 + 5;
    const int rise = draws.Next() % 10 + 5;
    EXPECT_EQ(p.life, life);
    EXPECT_EQ(p.scale, std::bit_cast<float>(0x3e4ccccdu));
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.velocity, glm::vec3(F32(forward * Bits(0x3ecccccdu)), 0.0f, F32(rise * Bits(0x3e4ccccdu))));
    EXPECT_EQ(random.State(), draws.State());
}

// 0x5477e4..0x54784c: position plus velocity, x and y times 0.95f, the light from life times 0.1f, z +0.3f and scale
// +0.07f (subtype 0) or +0.09f, without rand().
TEST(BloodCastleParticlesTest, TheSparkDriftsRisesAndGrowsBySubtype)
{
    for (const int subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        Particle p;
        p.alive = true;
        p.type = p.texture = Spark;
        p.subtype = subtype;
        p.life = 17;
        p.scale = 0.75f;
        p.position = {10.5f, 20.25f, 30.125f};
        p.velocity = {1.5f, -2.25f, 0.7f};
        CrtRandom random(5);
        Client::Particles::StepBloodCastleParticle(p, random);
        const double drag = Bits(0x3f733333u);
        EXPECT_EQ(p.position, glm::vec3(F32(1.5 + 10.5), F32(20.25 - 2.25), F32(30.125 + static_cast<double>(0.7f))));
        EXPECT_EQ(p.velocity.x, F32(1.5 * drag));
        EXPECT_EQ(p.velocity.y, F32(-2.25 * drag));
        EXPECT_EQ(p.velocity.z, F32(static_cast<double>(0.7f) + Bits(0x3e99999au)));
        EXPECT_EQ(p.light, glm::vec3(F32(17.0 * Bits(0x3dcccccdu))));
        EXPECT_EQ(p.scale, F32(0.75 + (subtype == 0 ? Bits(0x3d8f5c29u) : Bits(0x3db851ecu))));
        EXPECT_EQ(random.State(), CrtRandom(5).State());
    }
}

// 0x54793f..0x547a38 (6ac) and 0x54748a..0x54756d (622/6): the same with each one's light; four rand(), three to shift
// the position and one for the turn.
TEST(BloodCastleParticlesTest, TheFlameAndTheSmokeJitterAndTurnWithFourDraws)
{
    for (const std::int32_t type : {Flame, Cloud})
    {
        SCOPED_TRACE(type);
        Particle p;
        p.alive = true;
        p.type = p.texture = type;
        p.subtype = type == Flame ? 0 : 6;
        p.life = 19;
        p.scale = 0.5f;
        p.rotation = 123.0f;
        p.position = {10.5f, 20.25f, 30.125f};
        p.velocity = {1.5f, 0.0f, 0.7f};
        CrtRandom random(23);
        Client::Particles::StepBloodCastleParticle(p, random);
        CrtRandom draws(23);
        const int dx = (draws.Next() & 3) - 2;
        const int dy = (draws.Next() & 3) - 2;
        const int dz = (draws.Next() & 3) - 2;
        const int turn = draws.Next() & 1;
        const float x = F32(1.5 + 10.5);
        const float z = F32(30.125 + static_cast<double>(0.7f));
        EXPECT_EQ(p.position.x, F32(dx + static_cast<double>(x)));
        EXPECT_EQ(p.position.y, F32(dy + 20.25));
        EXPECT_EQ(p.position.z, F32(dz * Bits(0x3f4ccccdu) + static_cast<double>(z)));
        EXPECT_EQ(p.velocity,
                  glm::vec3(F32(1.5 * Bits(0x3f733333u)), 0.0f, F32(static_cast<double>(0.7f) + Bits(0x3f19999au))));
        EXPECT_EQ(p.scale, F32(0.5 + Bits(0x3d4ccccdu)));
        EXPECT_EQ(p.light, glm::vec3(F32(19.0 * (type == Flame ? Bits(0x3d23d70au) : Bits(0x3ca3d70au)))));
        EXPECT_EQ(p.rotation, F32((turn + 123.0) + 1.0));
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x542ed0..0x543012 with /4: life 40 and the rise (r % 100) · 0.01f · 5 + 1; then /3's.
TEST(BloodCastleParticlesTest, TheTorchOrbitLivesFortyStepsAndRisesWithFive)
{
    Particles particles;
    CrtRandom random(29);
    const float clock = 1000.0f;
    Client::Particles::PortableParticleTrigonometry trigonometry;
    const glm::vec3 light{1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f4ccccdu)};
    const float scale = std::bit_cast<float>(0x3e428f5cu);
    const auto made =
        particles.CreateChecked(Orbit, Where, Turned, light, 4, scale, std::cref(clock), random, 0, trigonometry);
    ASSERT_TRUE(made.HasValue());
    const Particle& p = particles.All()[0];
    CrtRandom draws(29);
    const int rise = draws.Next() % 100;
    const int phase = draws.Next() % 300 - 150;
    const int size = draws.Next() % 6;
    const int turn = draws.Next() % 360;
    EXPECT_EQ(p.life, 40);
    EXPECT_EQ(p.verticalSpeed, F32((rise * Bits(0x3c23d70au)) * 5.0 + 1.0));
    EXPECT_EQ(p.velocity.x, static_cast<float>(phase));
    EXPECT_EQ(p.scale, F32(size * Bits(0x3c23d70au) + static_cast<double>(scale)));
    EXPECT_EQ(p.rotation, static_cast<float>(turn));
    EXPECT_EQ(p.angles, glm::vec3(0.0f));
    EXPECT_EQ(p.light, light);
    EXPECT_EQ(p.start, Where);
    EXPECT_EQ(random.State(), draws.State());
}

// 0x5472e2..0x547351: /4 turns with radius 40 around the start, rises, shrinks 0.004f and fades by 0.9090909f with life
// at 30 or less (/3, at 20).
TEST(BloodCastleParticlesTest, TheTorchOrbitShrinksFasterAndFadesFromThirty)
{
    for (const int life : {32, 31})
    {
        SCOPED_TRACE(life);
        Particles particles;
        Particle& p = particles.All()[0];
        p.alive = true;
        p.type = p.texture = Orbit;
        p.subtype = 4;
        p.life = life;
        p.scale = 0.25f;
        p.verticalSpeed = 2.5f;
        p.velocity.x = 17.0f;
        p.start = {100.0f, 200.0f, 50.0f};
        p.position = {100.0f, 200.0f, 50.0f};
        p.light = {1.0f, 0.5f, 0.25f};
        CrtRandom random(3);
        const float clock = 0.0f;
        Client::Particles::PortableParticleTrigonometry trigonometry;
        ASSERT_TRUE(Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trigonometry)
                        .HasValue());
        const double argument = (static_cast<double>(life - 1) + 17.0) * Bits(0x3dcccccdu);
        const auto sine = Client::Math::Detail::SineProductPC53(argument, 40.0f);
        const auto cosine = Client::Math::Detail::CosineProductPC53(argument, 40.0f);
        ASSERT_TRUE(sine && cosine);
        EXPECT_EQ(p.life, life - 1);
        EXPECT_EQ(p.position.x, F32(*sine + 100.0));
        EXPECT_EQ(p.position.y, F32(200.0 - *cosine));
        EXPECT_EQ(p.scale, F32(0.25 - Bits(0x3b83126fu)));
        const double fade = Bits(0x3f68ba2eu);
        const glm::vec3 faded{F32(fade), F32(0.5 * fade), F32(0.25 * fade)};
        EXPECT_EQ(p.light, life - 1 <= 30 ? faded : glm::vec3(1.0f, 0.5f, 0.25f));
    }
}

// 0x5488f6: 6ab goes to the common billboard with the material's blend (fi01.jpg, 3 components: additive).
// 0x5489eb..0x548a51: 6ac/0 requests 0x620c10 before the billboard; /2 sets GL_ADD before, draws the common billboard
// with it and goes back to GL_MODULATE.
TEST(BloodCastleParticlesTest, TheFlameDrawsBlendedAndTheSparkAdditive)
{
    Particle spark;
    spark.alive = true;
    spark.type = spark.texture = Spark;
    spark.scale = 0.5f;
    spark.light = glm::vec3(0.75f);
    const auto sparkDrawn = Client::Tests::DrawOneParticle(spark, glm::vec2(64.0f), std::uint8_t{3});
    ASSERT_TRUE(sparkDrawn.HasValue());
    ASSERT_TRUE(sparkDrawn.Value().has_value());
    EXPECT_EQ(sparkDrawn.Value()->blend, RecordedParticleDraw::Blend::Additive);
    EXPECT_EQ(sparkDrawn.Value()->texture, Spark);

    Particle flame = spark;
    flame.type = flame.texture = Flame;
    const auto flameDrawn = Client::Tests::DrawOneParticle(flame, glm::vec2(64.0f), std::uint8_t{4});
    ASSERT_TRUE(flameDrawn.HasValue());
    ASSERT_TRUE(flameDrawn.Value().has_value());
    EXPECT_EQ(flameDrawn.Value()->blend, RecordedParticleDraw::Blend::Blended);

    EXPECT_EQ(flameDrawn.Value()->textureEnv, TextureEnvMode::Modulate);

    Particle bright = flame;
    bright.subtype = 2;
    auto after = TextureEnvMode::Add;
    const auto added =
        Client::Tests::DrawOneParticle(bright, glm::vec2(64.0f), std::uint8_t{4}, 0.0f, 0, std::uint8_t{0}, &after);
    ASSERT_TRUE(added.HasValue());
    ASSERT_TRUE(added.Value().has_value());
    EXPECT_EQ(added.Value()->blend, RecordedParticleDraw::Blend::Blended);
    EXPECT_EQ(added.Value()->textureEnv, TextureEnvMode::Add);
    EXPECT_EQ(added.Value()->width, flameDrawn.Value()->width);
    EXPECT_EQ(added.Value()->height, flameDrawn.Value()->height);
    EXPECT_EQ(added.Value()->rotation, flameDrawn.Value()->rotation);
    EXPECT_EQ(added.Value()->uv, glm::vec2(0.0f));
    EXPECT_EQ(added.Value()->uvSize, glm::vec2(1.0f));
    EXPECT_EQ(after, TextureEnvMode::Modulate);
}

// 6ac/2 created, stepped and drawn: with life at 24 and the step leaving it at 23, the billboard carries light
// 23 · 0.08f − 1 and GL_ADD, and the mode goes back to GL_MODULATE.
TEST(BloodCastleParticlesTest, TheBrightFlameIsCreatedSteppedAndDrawnWithAdd)
{
    Particles particles;
    CrtRandom random(31);
    const float clock = 0.0f;
    const auto made = particles.CreateChecked(Flame, Where, Turned, glm::vec3(1.0f), 2,
                                              std::bit_cast<float>(0x40400000u), clock, random);
    ASSERT_TRUE(made.HasValue());
    ASSERT_EQ(made.Value(), std::optional<std::size_t>(0));
    Client::Particles::PortableParticleTrigonometry trigonometry;
    ASSERT_TRUE(
        Client::Particles::StepParticlesChecked(particles, random, std::cref(clock), nullptr, trigonometry).HasValue());
    Particle p = particles.All()[0];
    ASSERT_TRUE(p.alive);
    EXPECT_EQ(p.life, 23);
    EXPECT_EQ(p.light, glm::vec3(F32(23.0 * Bits(0x3da3d70au) - 1.0)));
    auto after = TextureEnvMode::Add;
    const auto drawn =
        Client::Tests::DrawOneParticle(p, glm::vec2(64.0f), std::uint8_t{4}, 0.0f, 0, std::uint8_t{0}, &after);
    ASSERT_TRUE(drawn.HasValue());
    ASSERT_TRUE(drawn.Value().has_value());
    EXPECT_EQ(drawn.Value()->textureEnv, TextureEnvMode::Add);
    EXPECT_EQ(drawn.Value()->blend, RecordedParticleDraw::Blend::Blended);
    EXPECT_EQ(drawn.Value()->light, p.light);
    EXPECT_EQ(after, TextureEnvMode::Modulate);
}
