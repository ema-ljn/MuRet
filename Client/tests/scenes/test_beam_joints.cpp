#include "Scenes/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "World/Particles.h"
#include "World/Sprites.h"
#include "Terrain/TerrainLighting.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// El 0x618 (BeamJoints.cpp): el alta 0x532125..0x5323ce y el paso 0x5381f3..0x538704, con los subtipos que el lote de
// joints de la CL11 agregó a la /0, /1, /2, /3, /4 y /0x11 de antes.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Scenes::JointOwnerKind;
using Client::Scenes::JointState;

constexpr std::uint32_t Seed = 0x618u;

glm::vec3 Bits(std::uint32_t red, std::uint32_t green, std::uint32_t blue)
{
    return {std::bit_cast<float>(red), std::bit_cast<float>(green), std::bit_cast<float>(blue)};
}

struct BeamWorld
{
    BeamWorld()
    {
        actor.alive = true;
        actor.position = {1000.0f, 1000.0f, 100.0f};
        actor.effectOrigin = {10.0f, 20.0f, 30.0f};
        actor.bonePoint4c = {40.0f, 50.0f, 60.0f};
        effect.alive = true;
        effect.point40 = {11.0f, 21.0f, 31.0f};
        effect.point4c = {41.0f, 51.0f, 61.0f};
    }

    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype, void* owner,
                                                                           JointOwnerKind kind = JointOwnerKind::ByType)
    {
        glm::vec3 from = start;
        glm::vec3 to = target;
        glm::vec3 angles{0.0f, 0.0f, 0.0f};
        const Client::World::FishEffectRequest request{.type = 0x618,
                                                       .firstPosition = from,
                                                       .secondPosition = to,
                                                       .angles = angles,
                                                       .subtype = subtype,
                                                       .ownerContext = owner,
                                                       .scale = 20.0f,
                                                       .key = -1,
                                                       .skill = 0,
                                                       .skillSerial = 0,
                                                       .target = -1};
        return Client::Scenes::CreateFaunaJoint(joints, request, random, kind);
    }

    Client::Core::Expected<void, std::string> Step(float worldClock = 0.0f)
    {
        return Client::Scenes::StepJoints(joints, random, nullptr, &particles, worldClock, &sprites, lighting.get(),
                                          [this](Client::Audio::SoundId sound) { sounds.push_back(sound.Value()); });
    }

    ActorState actor;
    Client::World::ModelEffect effect;
    glm::vec3 start{1300.0f, 1000.0f, 100.0f};
    glm::vec3 target{1300.0f, 1300.0f, 100.0f};
    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
    Client::World::Particles particles;
    Client::World::FrameSprites sprites;
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    std::vector<std::int32_t> sounds;
};

// 0x532238..0x5322c9 y 0x5322cc..0x5323ce: los que siguen al dueño nacen quietos, con vida 999.999.999, en su +0x40 o
// su +0x4c, con los puntos y la luz de su subtipo, sin rand(). 0x530a32..0x530a55: la /0xa, /0xb, /0xe y /0xf nacen
// con la cuenta en −1 y sin el primer tramo (0x530afa), así que el punto 0 conserva lo del lugar; las otras, en 0.
TEST(BeamJointsTest, TheFollowersBornOnTheirOwnersPoint)
{
    struct Case
    {
        std::int32_t subtype;
        bool effectOwner;
        glm::vec3 position;
        std::int32_t points;
        glm::vec3 light;
    };
    const glm::vec3 violet = Bits(0x3f000000u, 0x3dcccccdu, 0x3f800000u);
    const glm::vec3 orange = Bits(0x3f800000u, 0x3e99999au, 0x3dcccccdu);
    for (const Case& value :
         {Case{2, false, {10.0f, 20.0f, 30.0f}, 20, violet}, Case{3, false, {40.0f, 50.0f, 60.0f}, 20, violet},
          Case{5, true, {11.0f, 21.0f, 31.0f}, 10, violet},
          Case{8, false, {10.0f, 20.0f, 30.0f}, 20, Bits(0x3f800000u, 0, 0x3f000000u)},
          Case{0xa, true, {11.0f, 21.0f, 31.0f}, 5, orange}, Case{0xb, true, {41.0f, 51.0f, 61.0f}, 5, orange},
          Case{0xe, false, {10.0f, 20.0f, 30.0f}, 15, Bits(0x3f800000u, 0x3dcccccdu, 0x3dcccccdu)},
          Case{0xf, false, {40.0f, 50.0f, 60.0f}, 15, Bits(0x3f800000u, 0x3dcccccdu, 0x3dcccccdu)}})
    {
        SCOPED_TRACE(value.subtype);
        BeamWorld tested;
        const glm::vec3 stale{7.0f, 8.0f, 9.0f};
        tested.joints[0].points[0].fill(stale);
        const auto created = value.effectOwner
                                 ? tested.Create(value.subtype, &tested.effect, JointOwnerKind::ModelEffect)
                                 : tested.Create(value.subtype, &tested.actor);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        const JointState& joint = tested.joints[0];
        EXPECT_EQ(joint.position, value.position);
        EXPECT_EQ(joint.speed, 0.0f);
        EXPECT_EQ(joint.life, 999999999);
        EXPECT_EQ(joint.maximumCount, value.points);
        EXPECT_EQ(joint.light, value.light);
        EXPECT_EQ(joint.width, 20.0f);
        const bool skipsTheQuad =
            value.subtype == 0xa || value.subtype == 0xb || value.subtype == 0xe || value.subtype == 0xf;
        EXPECT_EQ(joint.count, skipsTheQuad ? -1 : 0);
        for (const glm::vec3& corner : joint.points[0])
            EXPECT_EQ(corner == stale, skipsTheQuad);
        EXPECT_EQ(tested.random.State(), CrtRandom(Seed).State());
    }
}

// 0x53820e..0x53830e: la /8 sigue el +0x40 y ensancha 10,1f; la /0xb sigue el +0x4c del efecto. Con el dueño muerto,
// se apaga y sale del paso sin la cola ni la vida.
TEST(BeamJointsTest, TheFollowersTrackTheirOwnerUntilItDies)
{
    BeamWorld tested;
    ASSERT_TRUE(tested.Create(8, &tested.actor).HasValue());
    ASSERT_TRUE(tested.Create(0xb, &tested.effect, JointOwnerKind::ModelEffect).HasValue());
    tested.actor.effectOrigin = {1.0f, 2.0f, 3.0f};
    tested.effect.point4c = {4.0f, 5.0f, 6.0f};

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(tested.joints[0].position, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_EQ(tested.joints[0].width, static_cast<float>(20.0 + static_cast<double>(10.1f)));
    EXPECT_EQ(tested.joints[1].position, glm::vec3(4.0f, 5.0f, 6.0f));
    EXPECT_EQ(tested.joints[1].width, 20.0f);
    // La /8 nació en 0 y la /0xb en −1 (0x530a55): tras un paso, 1 y 0.
    EXPECT_EQ(tested.joints[0].count, 1);
    EXPECT_EQ(tested.joints[1].count, 0);
    for (const JointState& joint : {tested.joints[0], tested.joints[1]})
        EXPECT_EQ(joint.life, 999999998);
    tested.actor.alive = false;
    tested.effect.alive = false;

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(tested.joints[0].count, 1);
    EXPECT_EQ(tested.joints[1].count, 0);
    for (const JointState& joint : {tested.joints[0], tested.joints[1]})
    {
        EXPECT_FALSE(joint.alive);
        EXPECT_EQ(joint.life, 999999998);
    }
    EXPECT_EQ(tested.random.State(), CrtRandom(Seed).State());
}

// 0x5382c9..0x53830e: la /0xe sigue el +0x40 del dueño y deja el 0x5d5/4 de escala 1 con la luz (v; v · 0,1f; v ·
// 0,1f), v = sen(reloj · 0,002f) · 0,3f + 0,8f en la x87 (los bits, de una cuenta aparte: el seno exacto a 64 bits). La
// partícula se repite en un pool gemelo con el mismo CrtRandom.
TEST(BeamJointsTest, TheRedFollowersLeaveTheirLightning)
{
    for (const auto& [clock, light] : {std::pair{0.0f, std::array{0x3f4ccccdu, 0x3da3d70bu}},
                                       std::pair{1000.0f, std::array{0x3f895128u, 0x3ddbb50eu}}})
    {
        SCOPED_TRACE(clock);
        BeamWorld tested;
        ASSERT_TRUE(tested.Create(0xe, &tested.actor).HasValue());
        CrtRandom replay = tested.random;

        ASSERT_TRUE(tested.Step(clock).HasValue());

        const JointState& joint = tested.joints[0];
        EXPECT_EQ(joint.position, glm::vec3(10.0f, 20.0f, 30.0f));
        const glm::vec3 glow{std::bit_cast<float>(light[0]), std::bit_cast<float>(light[1]),
                             std::bit_cast<float>(light[1])};
        auto twin = std::make_unique<Client::World::Particles>();
        ASSERT_TRUE(twin->CreateChecked(0x5d5, joint.position, joint.angles, glow, 4, 1.0f, clock, replay).HasValue());
        const Client::World::Particle& made = tested.particles.All()[0];
        const Client::World::Particle& expected = twin->All()[0];
        ASSERT_TRUE(made.alive);
        EXPECT_EQ(made.type, 0x5d5);
        EXPECT_EQ(made.subtype, 4);
        EXPECT_EQ(made.light, glow);
        EXPECT_EQ(made.position, expected.position);
        EXPECT_EQ(made.scale, expected.scale);
        EXPECT_EQ(made.life, expected.life);
        EXPECT_EQ(made.velocity, expected.velocity);
        EXPECT_EQ(made.rotation, expected.rotation);
        EXPECT_FALSE(tested.particles.All()[1].alive);
        EXPECT_EQ(tested.random.State(), replay.State());
    }
}

// 0x53214b..0x5321db: la /0xc, /0xd y /0x10 sacan el sentido (rand() impar: 1; par: −1), velocidad 10 y los ángulos x e
// y en rand() % 45, con la luz de su subtipo; la /9 nace gris; la /7 conserva la luz del prefijo.
TEST(BeamJointsTest, TheSwervingBeamsDrawTheirSideAndTilt)
{
    for (const std::int32_t subtype : {0xc, 0xd, 0x10})
    {
        SCOPED_TRACE(subtype);
        BeamWorld tested;
        ASSERT_TRUE(tested.Create(subtype, &tested.actor).HasValue());
        CrtRandom expected(Seed);
        const JointState& joint = tested.joints[0];
        EXPECT_EQ(joint.field9ec, expected.Next() % 2 != 0 ? 1 : -1);
        EXPECT_EQ(joint.angles.x, static_cast<float>(expected.Next() % 45));
        EXPECT_EQ(joint.angles.y, static_cast<float>(expected.Next() % 45));
        EXPECT_EQ(tested.random.State(), expected.State());
        EXPECT_EQ(joint.speed, 10.0f);
        EXPECT_EQ(joint.life, 120);
        EXPECT_EQ(joint.maximumCount, 8);
        EXPECT_EQ(joint.light, subtype == 0x10 ? Bits(0x3ecccccdu, 0x3e4ccccdu, 0x3ecccccdu)
                                               : Bits(0x3f333333u, 0x3e99999au, 0x3f800000u));
    }
    BeamWorld tested;
    ASSERT_TRUE(tested.Create(9, nullptr).HasValue());
    ASSERT_TRUE(tested.Create(7, nullptr).HasValue());
    EXPECT_EQ(tested.joints[0].light, glm::vec3(std::bit_cast<float>(0x3e4ccccdu)));
    EXPECT_EQ(tested.joints[1].light, glm::vec3(1.0f));
    for (const JointState& joint : {tested.joints[0], tested.joints[1]})
    {
        EXPECT_EQ(joint.speed, 3.0f);
        EXPECT_EQ(joint.life, 120);
        EXPECT_EQ(joint.maximumCount, 8);
    }
    EXPECT_EQ(tested.random.State(), CrtRandom(Seed).State());
}

// 0x538341..0x538387 y 0x5385d3..0x5386e3: con vida de 100 o más, la /0xc suma 15 al rumbo, (rand() · sentido) % 5 en
// x y en y y 6 en z, después del movimiento común (velocidad 10, con los ángulos de antes); después el rand() & 3 de la
// luz dinámica, el sprite 0x5ac y el 0x5fd girado rand() % 360.
TEST(BeamJointsTest, TheSwervingBeamSpinsAndLeavesItsSprites)
{
    BeamWorld tested;
    ASSERT_TRUE(tested.Create(0xc, &tested.actor).HasValue());
    const JointState before = tested.joints[0];
    CrtRandom expected = tested.random;

    ASSERT_TRUE(tested.Step().HasValue());

    const JointState& joint = tested.joints[0];
    const glm::vec3 moved =
        Client::Math::OriginalVectorRotate({0.0f, -10.0f, 0.0f}, Client::Math::OriginalAngleMatrix(before.angles));
    const auto jitter = [&expected, &before](float axis, float delta)
    {
        const float common = static_cast<float>(static_cast<double>(axis) + static_cast<double>(delta));
        const std::int32_t drawn = expected.Next();
        return static_cast<float>(static_cast<double>(drawn * before.field9ec % 5) + static_cast<double>(common));
    };
    EXPECT_EQ(joint.angles.z, 15.0f);
    EXPECT_EQ(joint.position.x, jitter(before.position.x, moved.x));
    EXPECT_EQ(joint.position.y, jitter(before.position.y, moved.y));
    const float rose = static_cast<float>(static_cast<double>(before.position.z) + static_cast<double>(moved.z));
    EXPECT_EQ(joint.position.z, static_cast<float>(static_cast<double>(rose) + 6.0));
    static_cast<void>(expected.Next()); // la luz dinámica
    const auto turn = static_cast<float>(expected.Next() % 360);
    EXPECT_EQ(tested.random.State(), expected.State());
    int flashes = 0, rings = 0;
    for (const auto& sprite : tested.sprites.Sprites())
    {
        if (!sprite.alive)
            continue;
        flashes += sprite.texture == 0x5ac ? 1 : 0;
        rings += sprite.texture == 0x5fd ? 1 : 0;
        EXPECT_EQ(sprite.rotation, sprite.texture == 0x5fd ? turn : 0.0f);
        EXPECT_EQ(sprite.position, joint.position);
    }
    EXPECT_EQ(flashes, 1);
    EXPECT_EQ(rings, 1);
    EXPECT_EQ(joint.life, 119);
}

// 0x538475..0x5385cd: debajo de vida 100 la /6 va hacia su propio destino (no lee al dueño): la velocidad + 5 hasta 30,
// y a 35 o menos muere, deja el 0x5d5/1 y pide el sonido 0x23; después sigue con la luz y el 0x5d5/3.
TEST(BeamJointsTest, TheOwnerlessBeamGoesToItsOwnTarget)
{
    BeamWorld tested;
    tested.target = {1300.0f, 1020.0f, 100.0f};
    ASSERT_TRUE(tested.Create(6, nullptr).HasValue());
    tested.joints[0].life = 50;
    tested.joints[0].speed = 0.0f;
    CrtRandom replay = tested.random;

    const auto stepped = tested.Step();

    EXPECT_EQ(tested.joints[0].targetPosition, glm::vec3(1300.0f, 1020.0f, 100.0f));
    EXPECT_FALSE(tested.joints[0].alive);
    EXPECT_EQ(tested.sounds, std::vector<std::int32_t>{0x23});
    // 0x5385d3..0x538704: después, la luz ((rand() & 3) + 8) · 0,03f y el 0x5d5/3 de escala 0,05f con su luz
    // (0x5386ff). El 0x5d5/1 del choque, el rand() de la luz y el 0x5d5/3, en ese orden, en un pool gemelo con el mismo
    // CrtRandom.
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    const JointState& joint = tested.joints[0];
    auto twin = std::make_unique<Client::World::Particles>();
    ASSERT_TRUE(
        twin->CreateChecked(0x5d5, joint.position, joint.angles, joint.light, 1, 1.0f, 0.0f, replay).HasValue());
    static_cast<void>(replay.Next());
    ASSERT_TRUE(twin->CreateChecked(0x5d5, joint.position, joint.angles, joint.light, 3,
                                    std::bit_cast<float>(0x3d4ccccdu), 0.0f, replay)
                    .HasValue());
    EXPECT_EQ(tested.random.State(), replay.State());
    for (std::size_t slot = 0; slot < 2; ++slot)
    {
        SCOPED_TRACE(slot);
        const Client::World::Particle& made = tested.particles.All()[slot];
        const Client::World::Particle& expected = twin->All()[slot];
        ASSERT_TRUE(made.alive);
        EXPECT_EQ(made.type, expected.type);
        EXPECT_EQ(made.subtype, expected.subtype);
        EXPECT_EQ(made.scale, expected.scale);
        EXPECT_EQ(made.position, expected.position);
        EXPECT_EQ(made.light, expected.light);
        EXPECT_EQ(made.life, expected.life);
        EXPECT_EQ(made.velocity, expected.velocity);
    }
    EXPECT_FALSE(tested.particles.All()[2].alive);
}
} // namespace
