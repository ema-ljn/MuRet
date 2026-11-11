#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "Render/Models/Skinning.h"
#include "Scenes/EventMapObjectFrame.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/BloodCastleParticleKernels.h"
#include "World/Particles.h"
#include "World/PortableParticleTrigonometry.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

// 0x602ed0 con Devil Square (0x603dd7) y con los Blood Castle (0x604152), después del rand() común (06 § Los
// objetos de Devil Square y de Blood Castle).
namespace
{
using Client::Render::Models::BoneTransform;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameObjectPass;
using Client::World::Particle;
using Client::World::Particles;
using Client::World::WorldObject;

float F32(double value)
{
    return static_cast<float>(value);
}

// La pose global de 0x68b181c: huesos sin giro, el k en (10 · k; 100; 50).
Client::Scenes::FaunaDrawCarry Pose(std::size_t bones)
{
    Client::Scenes::FaunaDrawCarry pose;
    for (std::size_t k = 0; k < bones; ++k)
    {
        BoneTransform bone(1.0f);
        bone[3] = glm::vec3(10.0f * static_cast<float>(k), 100.0f, 50.0f);
        pose.bones.push_back(bone);
    }
    return pose;
}

glm::vec3 BonePoint(std::size_t bone, const glm::vec3& offset)
{
    return glm::vec3(10.0f * static_cast<float>(bone), 100.0f, 50.0f) + offset;
}

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.transform.position = {1200.5f, 3400.25f, 75.0f};
    object.transform.angles = {0.0f, 0.0f, 30.0f};
    return object;
}

void ExpectSameParticles(const Particles& actual, const Particles& expected)
{
    for (std::size_t k = 0; k < Client::World::ParticleCount; ++k)
    {
        const Particle& a = actual.All()[k];
        const Particle& e = expected.All()[k];
        ASSERT_EQ(a.alive, e.alive) << k;
        if (!a.alive)
            continue;
        SCOPED_TRACE(k);
        EXPECT_EQ(a.type, e.type);
        EXPECT_EQ(a.subtype, e.subtype);
        EXPECT_EQ(a.position, e.position);
        EXPECT_EQ(a.angles, e.angles);
        EXPECT_EQ(a.light, e.light);
        EXPECT_EQ(a.scale, e.scale);
        EXPECT_EQ(a.life, e.life);
        EXPECT_EQ(a.velocity, e.velocity);
    }
}
} // namespace

// 0x603dfe..0x603ee0: el 2 deja una 68/0 en el hueso 23 corrido (−15; 0; 0) con cada rand() & 3 en 0 (la
// segunda en el 31) y siempre una más en el 23, con los ángulos del objeto, luz 1 y escala 1.
TEST(EventMapObjectFrameTest, TheDevilSquareRainObjectDropsOnItsBones)
{
    const auto pose = Pose(32);
    const glm::vec3 offset{-15.0f, 0.0f, 0.0f};
    bool sawExtra = false;
    bool sawOnlyLast = false;
    for (std::uint32_t seed = 1; seed <= 24; ++seed)
    {
        SCOPED_TRACE(seed);
        const WorldObject object = Object(2);
        Particles particles;
        CrtRandom random(seed);
        const GameObjectPass pass{.map = 9, .random = &random, .particles = &particles, .retainedPose = &pose};
        ASSERT_TRUE(Client::Scenes::StepDevilSquareObjectFrame(object, pass).HasValue());

        Particles expected;
        CrtRandom draws(seed);
        int created = 0;
        const auto rain = [&](std::size_t bone)
        {
            ++created;
            ASSERT_TRUE(expected
                            .CreateChecked(0x68, BonePoint(bone, offset), object.transform.angles, glm::vec3(1.0f), 0,
                                           1.0f, 0.0f, draws)
                            .HasValue());
        };
        if (draws.Next() % 4 == 0)
            rain(23);
        if (draws.Next() % 4 == 0)
            rain(31);
        rain(23);
        sawExtra = sawExtra || created > 1;
        sawOnlyLast = sawOnlyLast || created == 1;
        ExpectSameParticles(particles, expected);
        EXPECT_EQ(random.State(), draws.State());
    }
    EXPECT_TRUE(sawExtra);
    EXPECT_TRUE(sawOnlyLast);

    // Los demás tipos no sacan rand(); sin rand() en el pase, el 2 corta antes de mirar la pose.
    CrtRandom random(7);
    Particles particles;
    const GameObjectPass quiet{.map = 9, .random = &random, .particles = &particles, .retainedPose = &pose};
    ASSERT_TRUE(Client::Scenes::StepDevilSquareObjectFrame(Object(3), quiet).HasValue());
    EXPECT_EQ(random.State(), CrtRandom(7).State());
    const auto cut = Client::Scenes::StepDevilSquareObjectFrame(Object(2), GameObjectPass{.map = 9});
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x603e0e"), std::string::npos) << cut.Error();
}

// 0x60433a..0x6043e9: siete 0x5ac de 0,5 en (0; 0; 2) por los huesos 1, 2, 4, 6, 9, 10 y 11, con la luz
// (a; a · 0,5; 0), a = (sin((z · 20 + T) · 0,001f) + 1) · 0,5.
TEST(EventMapObjectFrameTest, TheBloodCastleStatueGlowsOnSevenBones)
{
    const auto pose = Pose(12);
    WorldObject object = Object(0xb);
    Client::World::FrameSprites sprites;
    CrtRandom random(7);
    const GameObjectPass pass{
        .map = 11, .random = &random, .worldClock = 98765.0f, .sprites = &sprites, .retainedPose = &pose};
    ASSERT_TRUE(Client::Scenes::StepBloodCastleObjectFrame(object, pass).HasValue());
    const double phase = (30.0 * 20.0 + 98765.0) * static_cast<double>(std::bit_cast<float>(0x3a83126fu));
    const auto wave = Client::Render::Models::Detail::SineSumPC53(phase, 1.0);
    ASSERT_TRUE(wave.has_value());
    const double level = *wave * 0.5;
    const glm::vec3 light{F32(level), F32(level * 0.5), 0.0f};
    const std::array<std::size_t, 7> bones{1, 2, 4, 6, 9, 10, 11};
    const auto live = sprites.Sprites();
    for (std::size_t k = 0; k < bones.size(); ++k)
    {
        SCOPED_TRACE(k);
        ASSERT_TRUE(live[k].alive);
        EXPECT_EQ(live[k].texture, 0x5ac);
        EXPECT_EQ(live[k].position, BonePoint(bones[k], {0.0f, 0.0f, 2.0f}));
        EXPECT_EQ(live[k].scale, 0.5f);
        EXPECT_EQ(live[k].light, light);
        EXPECT_EQ(live[k].rotation, 0.0f);
    }
    EXPECT_FALSE(live[bones.size()].alive);
    EXPECT_EQ(random.State(), CrtRandom(7).State());
}

// 0x6042bb..0x60432b: un 0x60f en el hueso 3, v = sin(T · 0,001f) · 0,3f + 0,7f en float, luz v y escala v + 0,5.
TEST(EventMapObjectFrameTest, TheBloodCastleOrbPulsesOnItsThirdBone)
{
    const auto pose = Pose(4);
    WorldObject object = Object(0xd);
    Client::World::FrameSprites sprites;
    const GameObjectPass pass{.map = 14, .worldClock = 4321.0f, .sprites = &sprites, .retainedPose = &pose};
    ASSERT_TRUE(Client::Scenes::StepBloodCastleObjectFrame(object, pass).HasValue());
    const auto wave = Client::Render::Models::Detail::SineProductPC53(
        4321.0 * static_cast<double>(std::bit_cast<float>(0x3a83126fu)), std::bit_cast<float>(0x3e99999au));
    ASSERT_TRUE(wave.has_value());
    const float level = F32(*wave + static_cast<double>(std::bit_cast<float>(0x3f333333u)));
    const auto live = sprites.Sprites();
    ASSERT_TRUE(live[0].alive);
    EXPECT_FALSE(live[1].alive);
    EXPECT_EQ(live[0].texture, 0x60f);
    EXPECT_EQ(live[0].position, BonePoint(3, glm::vec3(0.0f)));
    EXPECT_EQ(live[0].light, glm::vec3(level));
    EXPECT_EQ(live[0].scale, F32(static_cast<double>(level) + 0.5));
}

// 0x604171..0x6042ac: dos rand() & 1; con cada 0, la fase (+0x84) sube 1 y con ftol(fase + 2) % 4 o ftol(fase) %
// 4 en 0 suelta 6ac/0 y 6ab/0, o 622/6, 6ab/1 y 60f/4 (luz (1; 0,8f; 0,8f), escala 0,19f), en el objeto.
TEST(EventMapObjectFrameTest, TheBloodCastleTorchReleasesItsParticlesOnItsTurns)
{
    bool sawFlame = false;
    bool sawSmoke = false;
    for (std::uint32_t seed = 1; seed <= 12; ++seed)
    {
        for (const float phase : {0.0f, 1.0f, 2.0f, 3.0f})
        {
            SCOPED_TRACE(seed);
            SCOPED_TRACE(phase);
            WorldObject object = Object(0x25);
            object.field84 = phase;
            Particles particles;
            CrtRandom random(seed);
            const float clock = 2500.0f;
            const GameObjectPass pass{.map = 17, .random = &random, .worldClock = clock, .particles = &particles};
            ASSERT_TRUE(Client::Scenes::StepBloodCastleObjectFrame(object, pass).HasValue());

            Particles expected;
            CrtRandom draws(seed);
            Client::World::PortableParticleTrigonometry trigonometry;
            const glm::vec3& at = object.transform.position;
            const glm::vec3& angles = object.transform.angles;
            float turn = phase;
            const auto make = [&](std::int32_t type, std::int32_t subtype) {
                ASSERT_TRUE(
                    expected.CreateChecked(type, at, angles, glm::vec3(1.0f), subtype, 1.0f, clock, draws).HasValue());
            };
            if (draws.Next() % 2 == 0)
            {
                const bool fires = static_cast<std::int32_t>(turn + 2.0f) % 4 == 0;
                turn += 1.0f;
                if (fires)
                {
                    sawFlame = true;
                    make(Client::World::TorchFlameParticle, 0);
                    make(Client::World::TorchSparkParticle, 0);
                }
            }
            if (draws.Next() % 2 == 0)
            {
                const bool smokes = static_cast<std::int32_t>(turn) % 4 == 0;
                turn += 1.0f;
                if (smokes)
                {
                    sawSmoke = true;
                    make(Client::World::CloudParticle, 6);
                    make(Client::World::TorchSparkParticle, 1);
                    const glm::vec3 orbLight{1.0f, std::bit_cast<float>(0x3f4ccccdu),
                                             std::bit_cast<float>(0x3f4ccccdu)};
                    ASSERT_TRUE(expected
                                    .CreateChecked(Client::World::OrbitParticle, at, angles, orbLight, 4,
                                                   std::bit_cast<float>(0x3e428f5cu), std::cref(clock), draws, 0,
                                                   trigonometry)
                                    .HasValue());
                }
            }
            EXPECT_EQ(object.field84, turn);
            ExpectSameParticles(particles, expected);
            EXPECT_EQ(random.State(), draws.State());
        }
    }
    EXPECT_TRUE(sawFlame);
    EXPECT_TRUE(sawSmoke);
}

// Sin pose, sin sprites o sin pool cortan con la dirección de su lectura.
TEST(EventMapObjectFrameTest, TheBloodCastleObjectsCutWithoutTheirBindings)
{
    WorldObject statue = Object(0xb);
    const auto noPose = Client::Scenes::StepBloodCastleObjectFrame(statue, GameObjectPass{.map = 11});
    ASSERT_FALSE(noPose.HasValue());
    EXPECT_NE(noPose.Error().find("0x4ec110"), std::string::npos) << noPose.Error();

    const auto pose = Pose(4);
    WorldObject orb = Object(0xd);
    const auto noSprites =
        Client::Scenes::StepBloodCastleObjectFrame(orb, GameObjectPass{.map = 11, .retainedPose = &pose});
    ASSERT_FALSE(noSprites.HasValue());
    EXPECT_NE(noSprites.Error().find("0x60432b"), std::string::npos) << noSprites.Error();

    WorldObject torch = Object(0x25);
    const auto noRandom = Client::Scenes::StepBloodCastleObjectFrame(torch, GameObjectPass{.map = 11});
    ASSERT_FALSE(noRandom.HasValue());
    EXPECT_NE(noRandom.Error().find("0x604186"), std::string::npos) << noRandom.Error();

    WorldObject other = Object(0x30);
    EXPECT_TRUE(Client::Scenes::StepBloodCastleObjectFrame(other, GameObjectPass{.map = 11}).HasValue());
}
