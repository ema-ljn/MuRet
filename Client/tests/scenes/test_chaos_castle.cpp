#include "World/ChaosCastle.h"

#include "Core/Expected.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/ObjectState.h"
#include "Scenes/EventMapObjectFrame.h"
#include "Scenes/FaunaDrawing.h"
#include "Scenes/FaunaJointCreation.h"
#include "Scenes/GameDrawTail.h"
#include "Scenes/WorldObjectsDrawing.h"
#include "World/IFishStepServices.h"
#include "World/KalimaParticleKernels.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/SelectionParticleKernels.h"
#include "World/Sprites.h"
#include "World/WorldObjects.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Chaos Castle (los mapas 18 a 23) sin sus paquetes: el preludio 0x405490, el paso 0x4055b0 de los vistos, el
// caso 0x4059d0 de 0x602ed0, el alpha de 0x5ffdf0 y los dos joints que suelta, el meteoro 6ee/9 y la trampa
// 615/2 (06 § Los objetos de Chaos Castle). Los valores esperados repiten las fórmulas del ASM con otro CrtRandom
// de la misma semilla.
namespace
{
using Client::Render::Models::BoneTransform;
using Client::Render::Models::CrtRandom;
using Client::Scenes::GameObjectPass;
using Client::Scenes::JointState;
using Client::World::FishEffectRequest;
using Client::World::Particle;
using Client::World::Particles;
using Client::World::WorldObject;

constexpr std::uint8_t ChaosCastleMap = 18;

double D(float value)
{
    return static_cast<double>(value);
}

float F32(double value)
{
    return static_cast<float>(value);
}

// La primera semilla con la que `condition` acepta los sorteos de un CrtRandom nuevo.
std::uint32_t SeedWhere(const std::function<bool(CrtRandom&)>& condition)
{
    for (std::uint32_t seed = 1; seed < 100000; ++seed)
    {
        CrtRandom random(seed);
        if (condition(random))
            return seed;
    }
    ADD_FAILURE() << "ninguna semilla cumple la condición";
    return 1;
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

WorldObject Object(std::int16_t type)
{
    WorldObject object;
    object.type = type;
    object.identity = 41;
    object.transform.position = {1200.5f, 3400.25f, 75.0f};
    object.transform.angles = {0.0f, 0.0f, 30.0f};
    object.transform.scale = 1.25f;
    return object;
}

// Lo que el caller copia de un pedido de joint: los argumentos se prestan sólo durante la llamada.
struct JointCall
{
    std::int32_t type = 0;
    std::int32_t subtype = 0;
    glm::vec3 first{};
    glm::vec3 second{};
    glm::vec3 angles{};
    void* owner = nullptr;
    float scale = 0;
    std::int32_t key = 0;
    std::int32_t skill = 0;
    std::int32_t skillSerial = 0;
    std::int32_t target = 0;
};

std::function<Client::Core::Expected<void, std::string>(const FishEffectRequest&)>
Recorder(std::vector<JointCall>& calls)
{
    return [&calls](const FishEffectRequest& request) -> Client::Core::Expected<void, std::string>
    {
        calls.push_back({request.type, request.subtype, request.firstPosition, request.secondPosition, request.angles,
                         request.ownerContext, request.scale, request.key, request.skill, request.skillSerial,
                         request.target});
        return {};
    };
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

std::vector<Client::World::Sprite> AliveSprites(Client::World::FrameSprites& sprites)
{
    std::vector<Client::World::Sprite> alive;
    for (const auto& sprite : sprites.Sprites())
        if (sprite.alive)
            alive.push_back(sprite);
    return alive;
}

// El 615/2 recién creado por la trampa, con los dos rand() de la creación ya sacados.
struct TrapBolt
{
    std::array<JointState, 1> joints{};
    glm::vec3 point{200.0f, 300.0f, 40.0f};
    glm::vec3 angles{0.0f, 0.0f, 30.0f};
    CrtRandom random;

    explicit TrapBolt(std::uint32_t seed) : random(seed)
    {
        const FishEffectRequest request{0x615, point, point, angles, 2, nullptr, 63.0f, -1, 0, 0, -1};
        EXPECT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue());
    }
};

// La x, la y o la z corridas como en 0x5376c9..0x537759 y 0x5377d9..0x537816: fild del resto y fadd.
float Jittered(CrtRandom& draws, float axis)
{
    return F32(static_cast<double>(draws.Next() % 20 - 10) + D(axis));
}
} // namespace

// 0x405e00: rand() % 8 en cada celda; con 0 y el área en 0, 3 o 6, la piedra 0x5f6 en el primer rectángulo que
// tiene la celda. Con el área 0xff de la .data, un solo rand() y nada más.
TEST(ChaosCastleTest, EachCellDrawsOnceWithoutItsArea)
{
    const float clock = 0.0f;
    CrtRandom random(7);
    CrtRandom draws(7);
    const Client::World::ChaosCastleCellContext context{
        .area = 0xff, .random = &random, .worldClock = std::cref(clock)};
    for (std::uint8_t x = 20; x < 50; ++x)
    {
        ASSERT_TRUE(Client::World::StepChaosCastleCell(x, 80, context).HasValue());
        static_cast<void>(draws.Next());
    }
    EXPECT_EQ(random.State(), draws.State());

    const Client::World::ChaosCastleCellContext noRandom{.area = 0xff, .worldClock = std::cref(clock)};
    const auto cut = Client::World::StepChaosCastleCell(30, 80, noRandom);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x405e1e"), std::string::npos) << cut.Error();
}

// Fuera de los cuatro rectángulos del área no hay piedra, aunque el rand() % 8 dé 0.
TEST(ChaosCastleTest, AnAreaCellOutsideItsRectanglesDropsNothing)
{
    const float clock = 0.0f;
    const std::uint32_t seed = SeedWhere([](CrtRandom& r) { return r.Next() % 8 == 0; });
    CrtRandom random(seed);
    const Client::World::ChaosCastleCellContext context{.area = 0, .random = &random, .worldClock = std::cref(clock)};
    ASSERT_TRUE(Client::World::StepChaosCastleCell(10, 10, context).HasValue());
    CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    EXPECT_EQ(random.State(), draws.State());
}

// 0x405490: con rand() % 10 en 0 y la cuenta de los 3 vistos distinta de 0, el selector es rand() % cuenta, sin
// meteoro.
TEST(ChaosCastleTest, ThePreludePicksTheSelectorAmongTheSeenTraps)
{
    const std::uint32_t seed = SeedWhere([](CrtRandom& r) { return r.Next() % 10 == 0; });
    std::vector<JointCall> calls;
    CrtRandom random(seed);
    const Client::World::ChaosCastlePreludeContext context{.random = &random, .createJoint = Recorder(calls)};
    const auto selector = Client::World::StepChaosCastlePrelude(5, context);
    ASSERT_TRUE(selector.HasValue()) << selector.Error();
    CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    EXPECT_EQ(selector.Value(), draws.Next() % 5);
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_TRUE(calls.empty());
}

// Sin el primer caso, con el héroe en la zona segura no saca el segundo rand(); fuera de ella, con otro rand() %
// 10 en 0, el meteoro 6ee/9 en (x + rand() % 800 − 400; y + rand() % 800 − 400; z − 150) del héroe, con sus
// ángulos y la escala rand() % 10 + 50.
TEST(ChaosCastleTest, ThePreludeThrowsAMeteorAroundTheHeroOutsideTheSafeZone)
{
    const std::uint32_t seed = SeedWhere(
        [](CrtRandom& r)
        {
            const bool first = r.Next() % 10 == 0;
            return !first && r.Next() % 10 == 0;
        });
    const glm::vec3 hero{12000.5f, 9000.25f, 180.0f};
    const glm::vec3 heroAngles{0.0f, 0.0f, 45.0f};
    {
        std::vector<JointCall> calls;
        CrtRandom random(seed);
        const Client::World::ChaosCastlePreludeContext safe{.random = &random,
                                                            .heroPosition = hero,
                                                            .heroAngles = heroAngles,
                                                            .heroInSafeZone = true,
                                                            .createJoint = Recorder(calls)};
        const auto selector = Client::World::StepChaosCastlePrelude(0, safe);
        ASSERT_TRUE(selector.HasValue());
        EXPECT_EQ(selector.Value(), 0);
        CrtRandom draws(seed);
        static_cast<void>(draws.Next());
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_TRUE(calls.empty());
    }
    std::vector<JointCall> calls;
    CrtRandom random(seed);
    const Client::World::ChaosCastlePreludeContext context{
        .random = &random, .heroPosition = hero, .heroAngles = heroAngles, .createJoint = Recorder(calls)};
    const auto selector = Client::World::StepChaosCastlePrelude(0, context);
    ASSERT_TRUE(selector.HasValue()) << selector.Error();
    EXPECT_EQ(selector.Value(), 0);
    CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    static_cast<void>(draws.Next());
    const float x = F32((static_cast<double>(draws.Next() % 800) + D(hero.x)) - 400.0);
    const float y = F32((static_cast<double>(draws.Next() % 800) + D(hero.y)) - 400.0);
    const float scale = F32(static_cast<double>(draws.Next() % 10) + 50.0);
    EXPECT_EQ(random.State(), draws.State());
    ASSERT_EQ(calls.size(), 1u);
    const JointCall& call = calls[0];
    EXPECT_EQ(call.type, 0x6ee);
    EXPECT_EQ(call.subtype, 9);
    EXPECT_EQ(call.first, glm::vec3(x, y, F32(D(hero.z) - 150.0)));
    EXPECT_EQ(call.second, call.first);
    EXPECT_EQ(call.angles, heroAngles);
    EXPECT_EQ(call.owner, nullptr);
    EXPECT_EQ(call.scale, scale);
    EXPECT_EQ(call.key, -1);
    EXPECT_EQ(call.target, -1);

    CrtRandom cutRandom(seed);
    const Client::World::ChaosCastlePreludeContext noJoint{.random = &cutRandom, .heroPosition = hero};
    const auto cut = Client::World::StepChaosCastlePrelude(0, noJoint);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x405597"), std::string::npos) << cut.Error();
}

// 0x4055b0: sólo el 3 cuenta. La trampa armada de la .data lo prende primero (+0x60 = 10, +0x8a = 1) y se
// desarma; después, el selector baja con cada 3 y sólo el que lo deja en 0 queda con +0x8a.
TEST(ChaosCastleTest, TheSeenTrapsCountAndOnlyTheSelectedOneFires)
{
    std::int32_t selector = 2;
    std::int32_t count = 0;
    std::uint8_t armed = 1;
    WorldObject other = Object(4);
    Client::World::StepChaosCastleSeenObject(other, selector, count, armed);
    EXPECT_EQ(count, 0);
    EXPECT_EQ(armed, 1);
    EXPECT_EQ(other.field60, 0);

    WorldObject first = Object(3);
    Client::World::StepChaosCastleSeenObject(first, selector, count, armed);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(armed, 0);
    EXPECT_EQ(selector, 2);
    EXPECT_EQ(first.field60, 10);
    EXPECT_EQ(first.field8a, 1);

    WorldObject second = Object(3);
    Client::World::StepChaosCastleSeenObject(second, selector, count, armed);
    EXPECT_EQ(selector, 1);
    EXPECT_EQ(second.field60, 10);
    EXPECT_EQ(second.field8a, 0);

    WorldObject third = Object(3);
    Client::World::StepChaosCastleSeenObject(third, selector, count, armed);
    EXPECT_EQ(selector, 0);
    EXPECT_EQ(third.field60, 10);
    EXPECT_EQ(third.field8a, 1);

    WorldObject fourth = Object(3);
    Client::World::StepChaosCastleSeenObject(fourth, selector, count, armed);
    EXPECT_EQ(count, 4);
    EXPECT_EQ(fourth.field60, 0);
    EXPECT_EQ(fourth.field8a, 0);
}

// 0x5ffdff..0x5ffe24: con +0x1a1 del héroe, el alpha baja 0,15f sólo en 18..23.
TEST(ChaosCastleTest, TheHerosFlagFadesTheObjectsOnlyInChaosCastle)
{
    const float fade = std::bit_cast<float>(0x3e19999au);
    EXPECT_EQ(Client::World::ChaosCastlePoseAlpha(18, 1, 1.0f), F32(1.0 - D(fade)));
    EXPECT_EQ(Client::World::ChaosCastlePoseAlpha(23, 1, 0.5f), F32(0.5 - D(fade)));
    EXPECT_EQ(Client::World::ChaosCastlePoseAlpha(18, 0, 1.0f), 1.0f);
    EXPECT_EQ(Client::World::ChaosCastlePoseAlpha(17, 1, 1.0f), 1.0f);
    EXPECT_EQ(Client::World::ChaosCastlePoseAlpha(24, 1, 1.0f), 1.0f);
}

// 0x405abb..0x405c5c: las nubes del 6 al 0xc la primera vez, con el objeto de dueño, y después +0x58 = −2.
TEST(ChaosCastleTest, TheCloudObjectsReleaseTheirMistsOnce)
{
    struct Case
    {
        std::int16_t type;
        int count;
        std::int32_t subtype;
        glm::vec3 light;
    };
    const glm::vec3 dim{std::bit_cast<float>(0x3d4ccccdu), std::bit_cast<float>(0x3d4ccccdu),
                        std::bit_cast<float>(0x3dcccccdu)};
    const std::array<Case, 7> cases{{{6, 10, 0, dim},
                                     {7, 10, 1, dim},
                                     {8, 10, 2, dim},
                                     {9, 5, 3, dim},
                                     {0xa, 5, 4, dim},
                                     {0xb, 5, 5, dim},
                                     {0xc, 7, 7, glm::vec3(std::bit_cast<float>(0x3e99999au))}}};
    for (const Case& c : cases)
    {
        SCOPED_TRACE(c.type);
        WorldObject object = Object(c.type);
        Particles particles;
        CrtRandom random(9);
        const GameObjectPass pass{.map = ChaosCastleMap, .random = &random, .particles = &particles, .scene = 5};
        const auto stepped = Client::Scenes::StepChaosCastleObjectFrame(object, pass);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);

        Particles expected;
        CrtRandom draws(9);
        const Client::World::ParticleOwner owner{Client::World::ParticleOwnerKind::WorldObject, object.identity};
        for (int k = 0; k < c.count; ++k)
            ASSERT_TRUE(expected
                            .CreateOwnedChecked(Client::World::CloudParticle, object.transform.position,
                                                object.transform.angles, c.light, c.subtype, object.transform.scale,
                                                0.0f, draws, 5, owner)
                            .HasValue());
        ExpectSameParticles(particles, expected);
        EXPECT_EQ(random.State(), draws.State());

        const auto again = Client::Scenes::StepChaosCastleObjectFrame(object, pass);
        ASSERT_TRUE(again.HasValue());
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x405c6e..0x405cbc y 0x405d88..0x405d95: lo que cada grupo hace con el área de 0x6c634c.
TEST(ChaosCastleTest, TheAreaShowsAndHidesItsObjects)
{
    const auto shownWith = [](std::int16_t type, std::uint8_t area, std::int16_t before)
    {
        WorldObject object = Object(type);
        object.state.hiddenMesh = before;
        const GameObjectPass pass{.map = ChaosCastleMap, .chaosCastleArea = area};
        EXPECT_TRUE(Client::Scenes::StepChaosCastleObjectFrame(object, pass).HasValue());
        return object.state.hiddenMesh;
    };
    constexpr std::int16_t Shown = -1;
    constexpr std::int16_t Hidden = -2;
    constexpr std::int16_t Other = 0;
    for (const std::int16_t type : std::array<std::int16_t, 6>{0, 3, 4, 5, 0xd, 0x11})
    {
        SCOPED_TRACE(type);
        EXPECT_EQ(shownWith(type, 0xff, Other), Other);
        EXPECT_EQ(shownWith(type, 1, Other), Other);
        EXPECT_EQ(shownWith(type, 2, Other), Hidden);
        EXPECT_EQ(shownWith(type, 8, Other), Hidden);
    }
    for (const std::int16_t type : std::array<std::int16_t, 2>{0x12, 0x15})
    {
        EXPECT_EQ(shownWith(type, 7, Other), Shown);
        EXPECT_EQ(shownWith(type, 8, Other), Shown);
        EXPECT_EQ(shownWith(type, 6, Other), Hidden);
        EXPECT_EQ(shownWith(type, 0xff, Other), Hidden);
    }
    for (const std::int16_t type : std::array<std::int16_t, 2>{0x18, 0x1d})
    {
        EXPECT_EQ(shownWith(type, 4, Other), Shown);
        EXPECT_EQ(shownWith(type, 5, Other), Shown);
        EXPECT_EQ(shownWith(type, 7, Other), Other);
        EXPECT_EQ(shownWith(type, 8, Other), Hidden);
        EXPECT_EQ(shownWith(type, 0xff, Other), Hidden);
    }
    for (const std::int16_t type : std::array<std::int16_t, 2>{0x1e, 0x23})
    {
        EXPECT_EQ(shownWith(type, 1, Other), Shown);
        EXPECT_EQ(shownWith(type, 2, Other), Shown);
        EXPECT_EQ(shownWith(type, 4, Other), Other);
        EXPECT_EQ(shownWith(type, 5, Other), Hidden);
    }
    for (const std::int16_t type : std::array<std::int16_t, 3>{0x16, 0x17, 0x24})
        EXPECT_EQ(shownWith(type, 8, Other), Other);
}

// 0x405cc5..0x405d82: el 3 con +0x8a toma el hueso 1 de la pose global; con +0x60 en 10, el 615/2 de ese punto a
// ese punto (escala rand() % 10 + 60, los ángulos del objeto) y el sonido 0x9c + (rand() & 1). La cuenta baja
// hasta 4 y entonces apaga +0x8a; después cae al área.
TEST(ChaosCastleTest, TheSelectedTrapFiresItsBoltFromItsFirstBone)
{
    const auto pose = Pose(4);
    WorldObject object = Object(3);
    object.field8a = 1;
    object.field60 = 10;
    std::vector<JointCall> calls;
    std::vector<std::int32_t> sounds;
    CrtRandom random(21);
    const GameObjectPass pass{.map = ChaosCastleMap,
                              .random = &random,
                              .retainedPose = &pose,
                              .createJoint = Recorder(calls),
                              .requestSound = [&sounds](std::int32_t sound) { sounds.push_back(sound); },
                              .chaosCastleArea = 2};
    const auto fired = Client::Scenes::StepChaosCastleObjectFrame(object, pass);
    ASSERT_TRUE(fired.HasValue()) << fired.Error();
    CrtRandom draws(21);
    const float scale = F32(static_cast<double>(draws.Next() % 10) + 60.0);
    const std::int32_t sound = 0x9c + (draws.Next() & 1);
    EXPECT_EQ(random.State(), draws.State());
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].type, 0x615);
    EXPECT_EQ(calls[0].subtype, 2);
    EXPECT_EQ(calls[0].first, glm::vec3(10.0f, 100.0f, 50.0f));
    EXPECT_EQ(calls[0].second, calls[0].first);
    EXPECT_EQ(calls[0].angles, object.transform.angles);
    EXPECT_EQ(calls[0].owner, nullptr);
    EXPECT_EQ(calls[0].scale, scale);
    EXPECT_EQ(calls[0].key, -1);
    EXPECT_EQ(calls[0].skill, 0);
    EXPECT_EQ(calls[0].skillSerial, 0);
    EXPECT_EQ(calls[0].target, -1);
    EXPECT_EQ(sounds, std::vector<std::int32_t>{sound});
    EXPECT_EQ(object.field60, 9);
    EXPECT_EQ(object.field8a, 1);
    EXPECT_EQ(object.state.hiddenMesh, Client::World::HiddenObject);

    object.state.hiddenMesh = Client::Render::Models::NoHiddenMesh;
    for (std::int32_t expected = 8; expected >= 4; --expected)
    {
        ASSERT_TRUE(Client::Scenes::StepChaosCastleObjectFrame(object, pass).HasValue());
        EXPECT_EQ(object.field60, expected);
        EXPECT_EQ(object.field8a, 1);
        object.state.hiddenMesh = Client::Render::Models::NoHiddenMesh;
    }
    ASSERT_TRUE(Client::Scenes::StepChaosCastleObjectFrame(object, pass).HasValue());
    EXPECT_EQ(object.field60, 4);
    EXPECT_EQ(object.field8a, 0);
    EXPECT_EQ(calls.size(), 1u);
    EXPECT_EQ(random.State(), draws.State());
}

// Escondido (+0x58 en −2) o sin +0x8a, no hay trampa. Sin pose, sin quien cree el joint o sin sonido, corta.
TEST(ChaosCastleTest, TheTrapNeedsItsFlagItsPoseItsJointAndItsSound)
{
    const auto pose = Pose(4);
    std::vector<JointCall> calls;
    CrtRandom random(21);
    WorldObject hidden = Object(0);
    hidden.field8a = 1;
    hidden.field60 = 10;
    hidden.state.hiddenMesh = Client::World::HiddenObject;
    const GameObjectPass full{.map = ChaosCastleMap,
                              .random = &random,
                              .retainedPose = &pose,
                              .createJoint = Recorder(calls),
                              .requestSound = [](std::int32_t) {}};
    ASSERT_TRUE(Client::Scenes::StepChaosCastleObjectFrame(hidden, full).HasValue());
    EXPECT_EQ(hidden.field60, 10);
    EXPECT_TRUE(calls.empty());

    WorldObject trap = Object(1);
    trap.field8a = 1;
    trap.field60 = 10;
    const GameObjectPass noPose{.map = ChaosCastleMap, .random = &random};
    const auto withoutPose = Client::Scenes::StepChaosCastleObjectFrame(trap, noPose);
    ASSERT_FALSE(withoutPose.HasValue());
    EXPECT_NE(withoutPose.Error().find("0x4ec110"), std::string::npos) << withoutPose.Error();

    const GameObjectPass noJoint{.map = ChaosCastleMap, .random = &random, .retainedPose = &pose};
    const auto withoutJoint = Client::Scenes::StepChaosCastleObjectFrame(trap, noJoint);
    ASSERT_FALSE(withoutJoint.HasValue());
    EXPECT_NE(withoutJoint.Error().find("0x405d49"), std::string::npos) << withoutJoint.Error();

    const GameObjectPass noSound{
        .map = ChaosCastleMap, .random = &random, .retainedPose = &pose, .createJoint = Recorder(calls)};
    const auto withoutSound = Client::Scenes::StepChaosCastleObjectFrame(trap, noSound);
    ASSERT_FALSE(withoutSound.HasValue());
    EXPECT_NE(withoutSound.Error().find("0x405d69"), std::string::npos) << withoutSound.Error();
}

// 0x5319ea, 0x531b0c..0x531bd2: el 615/2 toma la textura 0x614, el ancho, 50 puntos y vida 20; corre x e y
// rand() % 10 − 5 y sube la z 800, con los ángulos en 0 y el destino en el segundo punto. Dos rand().
TEST(ChaosCastleTest, TheTrapBoltIsCreatedWithTwoDraws)
{
    TrapBolt bolt(31);
    CrtRandom draws(31);
    const JointState& joint = bolt.joints[0];
    const float x = F32(static_cast<double>(draws.Next() % 10 - 5) + D(bolt.point.x));
    const float y = F32(static_cast<double>(draws.Next() % 10 - 5) + D(bolt.point.y));
    EXPECT_EQ(bolt.random.State(), draws.State());
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.texture, 0x614);
    EXPECT_EQ(joint.blend, 1);
    EXPECT_EQ(joint.drawEnabled, 3);
    EXPECT_EQ(joint.width, 63.0f);
    EXPECT_EQ(joint.maximumCount, 50);
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.position, glm::vec3(x, y, F32(D(bolt.point.z) + 800.0)));
    EXPECT_EQ(joint.angles, glm::vec3(0.0f));
    EXPECT_EQ(joint.targetPosition, bolt.point);
    EXPECT_EQ(joint.pathIncrement, joint.position);
    EXPECT_TRUE(joint.tailEnabled);
    EXPECT_EQ(joint.count, 0);
}

// 0x53768f..0x5379ef: el 615/2 rehace sus 50 tramos desde +0x24: 45 que bajan 16 corridos, cuatro que avanzan un
// quinto de lo que falta y el destino. Después dos sprites 0x5fd de 1,5 y la partícula 6ed/0 en el destino, con la
// luz (x · 0,5; y · 0,6f; z), y la cola común con vida de más de 10.
TEST(ChaosCastleTest, TheTrapBoltFallsOnItsTargetAndSparks)
{
    TrapBolt bolt(31);
    JointState& joint = bolt.joints[0];
    CrtRandom draws = bolt.random;
    std::vector<glm::vec3> appended;
    glm::vec3 p = joint.pathIncrement;
    for (int i = 0; i < 45; ++i)
    {
        p.x = Jittered(draws, p.x);
        p.y = Jittered(draws, p.y);
        p.z = F32(D(p.z) - 16.0);
        appended.push_back(p);
    }
    const glm::vec3 target = joint.targetPosition;
    const double fifth = D(std::bit_cast<float>(0x3e4ccccdu));
    const glm::vec3 step{F32((D(target.x) - D(p.x)) * fifth), F32(D(F32(D(target.y) - D(p.y))) * fifth),
                         F32(D(F32(D(target.z) - D(p.z))) * fifth)};
    for (int i = 0; i < 4; ++i)
    {
        p = {F32(D(step.x) + D(p.x)), F32(D(step.y) + D(p.y)), F32(D(step.z) + D(p.z))};
        p.x = Jittered(draws, p.x);
        p.y = Jittered(draws, p.y);
        appended.push_back(p);
    }
    appended.push_back(target);
    appended.push_back(target);
    const glm::vec3 light{0.5f, std::bit_cast<float>(0x3f19999au), 1.0f};
    const float firstTurn = static_cast<float>(draws.Next() % 360);
    const float secondTurn = static_cast<float>(draws.Next() % 360);
    Particles expected;
    const float clock = 0.0f;
    ASSERT_TRUE(
        expected.CreateChecked(Client::World::KalimaFanta, target, glm::vec3(0.0f), light, 0, 1.0f, clock, draws)
            .HasValue());

    Particles particles;
    Client::World::FrameSprites sprites;
    const auto stepped = Client::Scenes::StepJoints(bolt.joints, bolt.random, nullptr, &particles, clock, &sprites);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(bolt.random.State(), draws.State());
    EXPECT_EQ(joint.count, 49);
    EXPECT_EQ(joint.life, 19);
    EXPECT_TRUE(joint.tailEnabled);
    EXPECT_EQ(joint.position, target);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    // Con los ángulos en 0, el tercer punto del cuadro es (x; y; z − ancho / 2): el tramo n es el anteúltimo
    // n-ésimo agregado.
    const float half = F32(D(joint.width) * 0.5);
    for (std::size_t n = 0; n < 50; ++n)
    {
        SCOPED_TRACE(n);
        const glm::vec3& at = appended[appended.size() - 1 - n];
        EXPECT_EQ(joint.points[n][2], glm::vec3(at.x, at.y, F32(D(at.z) - D(half))));
    }
    const auto alive = AliveSprites(sprites);
    ASSERT_EQ(alive.size(), 2u);
    for (std::size_t k = 0; k < alive.size(); ++k)
    {
        EXPECT_EQ(alive[k].texture, 0x5fd);
        EXPECT_EQ(alive[k].position, target);
        EXPECT_EQ(alive[k].scale, 1.5f);
        EXPECT_EQ(alive[k].light, light);
        EXPECT_EQ(alive[k].blend, 0);
        EXPECT_EQ(alive[k].rotation, k == 0 ? firstTurn : secondTurn);
    }
    ExpectSameParticles(particles, expected);
}

// Con vida menor que 4 la luz se multiplica por 0,8333333f antes de los sprites; con 10 o menos no hay cola común.
// Sin sprites o sin partículas corta antes de sacar rand().
TEST(ChaosCastleTest, TheTrapBoltFadesAtTheEndAndNeedsItsPools)
{
    TrapBolt bolt(31);
    JointState& joint = bolt.joints[0];
    joint.life = 3;
    Particles particles;
    Client::World::FrameSprites sprites;
    const float clock = 0.0f;
    ASSERT_TRUE(Client::Scenes::StepJoints(bolt.joints, bolt.random, nullptr, &particles, clock, &sprites).HasValue());
    const float faded = F32(1.0 * D(std::bit_cast<float>(0x3f555555u)));
    EXPECT_EQ(joint.light, glm::vec3(faded));
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.life, 2);
    const auto alive = AliveSprites(sprites);
    ASSERT_EQ(alive.size(), 2u);
    EXPECT_EQ(alive[0].light,
              glm::vec3(F32(D(faded) * 0.5), F32(D(faded) * D(std::bit_cast<float>(0x3f19999au))), faded));

    TrapBolt bare(31);
    const CrtRandom before = bare.random;
    const auto cut = Client::Scenes::StepJoints(bare.joints, bare.random, nullptr, &particles, clock, nullptr);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x53770a"), std::string::npos) << cut.Error();
    EXPECT_EQ(bare.random.State(), before.State());
}

// 0x53265e, 0x5333f6..0x533449: el meteoro 6ee/9 resta (+0x10 = 3), dibuja sólo la segunda cara, velocidad 10,
// ángulos (−90; 0; 0), vida 20 y 15 puntos. Sin rand().
TEST(ChaosCastleTest, TheMeteorIsCreatedWithoutDraws)
{
    std::array<JointState, 1> joints{};
    glm::vec3 first{300.0f, 400.0f, 30.0f};
    glm::vec3 angles{0.0f, 0.0f, 45.0f};
    const FishEffectRequest request{0x6ee, first, first, angles, 9, nullptr, 57.0f, -1, 0, 0, -1};
    CrtRandom random(5);
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue());
    EXPECT_EQ(random.State(), CrtRandom(5).State());
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.texture, 0x6ee);
    EXPECT_EQ(joint.blend, 3);
    EXPECT_EQ(joint.drawEnabled, 2);
    EXPECT_EQ(joint.width, 57.0f);
    EXPECT_EQ(joint.speed, 10.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.angles, glm::vec3(-90.0f, 0.0f, 0.0f));
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.maximumCount, 15);
    EXPECT_EQ(joint.position, first);
    EXPECT_EQ(joint.pathIncrement, first);
    EXPECT_TRUE(joint.tailEnabled);
}

// 0x53590e y 0x538ea4..0x538f37: el avance común con la matriz de (−90; 0; 0); después x e y corridas
// (rand() % 10) − 5,0f, la z sube la velocidad y la velocidad 1. Con vida menor que 10 la luz por 0,8695652f.
TEST(ChaosCastleTest, TheMeteorRisesFasterEachStepAndFades)
{
    std::array<JointState, 1> joints{};
    glm::vec3 first{300.0f, 400.0f, 30.0f};
    glm::vec3 angles{};
    const FishEffectRequest request{0x6ee, first, first, angles, 9, nullptr, 57.0f, -1, 0, 0, -1};
    CrtRandom random(5);
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue());
    JointState& joint = joints[0];
    CrtRandom draws = random;
    const auto matrix = Client::Render::Models::OriginalAngleMatrix(glm::vec3(-90.0f, 0.0f, 0.0f));
    const glm::vec3 moved = Client::Render::Models::OriginalVectorRotate(glm::vec3(0.0f, -10.0f, 0.0f), matrix);
    glm::vec3 p{F32(D(moved.x) + D(first.x)), F32(D(moved.y) + D(first.y)), F32(D(moved.z) + D(first.z))};
    p.x = F32((static_cast<double>(draws.Next() % 10) - 5.0) + D(p.x));
    p.y = F32((static_cast<double>(draws.Next() % 10) - 5.0) + D(p.y));
    p.z = F32(10.0 + D(p.z));
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(joint.position, p);
    EXPECT_EQ(joint.speed, 11.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.life, 19);

    joint.life = 9;
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
    EXPECT_EQ(joint.light, glm::vec3(F32(D(std::bit_cast<float>(0x3f5e9bd4u)))));
    EXPECT_EQ(joint.speed, 12.0f);
}
