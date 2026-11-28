#include "Events/ChaosCastle/ChaosCastle.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Math/HalfLifeMath.h"
#include "Models/ObjectState.h"
#include "WorldObjects/EventMapObjectFrame.h"
#include "Kernel/PoseBuffer.h"
#include "Joints/FaunaJointCreation.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Particles/KalimaParticleKernels.h"
#include "Particles/ParticleOwner.h"
#include "Particles/Particles.h"
#include "Particles/SelectionParticleKernels.h"
#include "Particles/Sprites.h"
#include "Kernel/WorldObject.h"
#include "Kernel/ObjectPass.h"
#include "Audio/SoundId.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Chaos Castle (maps 18 to 23) without its packets: prelude 0x405490, step 0x4055b0 of the seen ones, case 0x4059d0 of
// 0x602ed0, the alpha of 0x5ffdf0 and the two joints it releases, meteor 0x6ee/9 and trap 0x615/2 (06 § Los objetos
// de Chaos Castle). The expected values repeat the ASM's formulas with another CrtRandom of the same seed.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::GameObjectPass;
using Client::Kernel::JointRequest;
using Client::Kernel::JointState;
using Client::Kernel::WorldObject;
using Client::Math::BoneTransform;
using Client::Particles::Particle;
using Client::Particles::Particles;

constexpr std::uint8_t ChaosCastleMap = 18;

using Client::Math::D;
using Client::Math::F32;

// The first seed with which `condition` accepts the draws of a new CrtRandom.
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

// The global pose of 0x68b181c: bones without rotation, bone k at (10 · k, 100, 50).
Client::Kernel::PoseBuffer Pose(std::size_t bones)
{
    Client::Kernel::PoseBuffer pose;
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

// What the caller copies from a joint request: the arguments are lent only during the call.
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

std::function<Client::Core::Expected<void, std::string>(const JointRequest&)> Recorder(std::vector<JointCall>& calls)
{
    return [&calls](const JointRequest& request) -> Client::Core::Expected<void, std::string>
    {
        calls.push_back({request.type, request.subtype, request.firstPosition, request.secondPosition, request.angles,
                         request.ownerContext, request.scale, request.key, request.skill, request.skillSerial,
                         request.target});
        return {};
    };
}

void ExpectSameParticles(const Particles& actual, const Particles& expected)
{
    for (std::size_t k = 0; k < Client::Particles::ParticleCount; ++k)
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

std::vector<Client::Particles::Sprite> AliveSprites(Client::Particles::FrameSprites& sprites)
{
    std::vector<Client::Particles::Sprite> alive;
    for (const auto& sprite : sprites.Sprites())
        if (sprite.alive)
            alive.push_back(sprite);
    return alive;
}

// The 0x615/2 just created by the trap, with the creation's two rand() already drawn.
struct TrapBolt
{
    std::array<JointState, 1> joints{};
    glm::vec3 point{200.0f, 300.0f, 40.0f};
    glm::vec3 angles{0.0f, 0.0f, 30.0f};
    CrtRandom random;

    explicit TrapBolt(std::uint32_t seed) : random(seed)
    {
        const JointRequest request{0x615, point, point, angles, 2, nullptr, 63.0f, -1, 0, 0, -1};
        EXPECT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    }
};

// The x, y or z shifted as in 0x5376c9..0x537759 and 0x5377d9..0x537816: fild of the remainder and fadd.
float Jittered(CrtRandom& draws, float axis)
{
    return F32(static_cast<double>(draws.Next() % 20 - 10) + D(axis));
}
} // namespace

// 0x405e00: rand() % 8 on each cell; with 0 and the area at 0, 3 or 6, the stone 0x5f6 in the first rectangle that
// holds the cell. With the .data's area 0xff, a single rand() and nothing more.
TEST(ChaosCastleTest, EachCellDrawsOnceWithoutItsArea)
{
    const float clock = 0.0f;
    CrtRandom random(7);
    CrtRandom draws(7);
    const Client::Events::ChaosCastle::ChaosCastleCellContext context{
        .area = 0xff, .random = &random, .worldClock = std::cref(clock)};
    for (std::uint8_t x = 20; x < 50; ++x)
    {
        ASSERT_TRUE(Client::Events::ChaosCastle::StepChaosCastleCell(x, 80, context).HasValue());
        static_cast<void>(draws.Next());
    }
    EXPECT_EQ(random.State(), draws.State());

    const Client::Events::ChaosCastle::ChaosCastleCellContext noRandom{.area = 0xff, .worldClock = std::cref(clock)};
    const auto cut = Client::Events::ChaosCastle::StepChaosCastleCell(30, 80, noRandom);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x405e1e"), std::string::npos) << cut.Error();
}

// 0x405e00 with area 0 and a cell of the first rectangle: with rand() % 8 at 0, the stone 0x5f6/0 (0x405f24, born like
// 0x5f3/0 at 0x54271f) at (x · 100 + rand() % 30 − 15, y · 100 + rand() % 30 − 15, hero z), with angles at 0, light
// 1, scale 1.5 and no owner, and with rand() % 5 at 0 the shake (rand() % 3 − 3) · 0.1f. The expected values come from
// castle.py (0x405e00 and 0x54271f in a separate x87), not from CreateChecked.
struct CastleStone
{
    glm::vec3 position;  // after the (rand() & 15) − 8 of 0x542726 and 0x542744
    std::uint32_t scale; // ((rand() & 31) + 32) · 1.5 · 0.01f
    float turn;          // the x angle, rand() % 360
};

struct CastleRow
{
    std::uint32_t seed;
    std::uint32_t state;
    std::uint32_t shake; // the bits of 0x7d325c8; 0 if it did not shake
    std::array<CastleStone, 3> stones;
    std::size_t count;
};

TEST(ChaosCastleTest, ACellOfTheAreaDropsItsStone)
{
    constexpr std::array<CastleRow, 3> Rows{{
        {1, 0x4db793afu, 0xbdcccccdu, {{{{3183, 7493, 125.5f}, 0x3f1d70a4u, 131}}}, 1},
        {2,
         0x6c4b7231u,
         0xbe4ccccdu,
         {{{{2407, 7484, 125.5f}, 0x3f6e147bu, 151}, {{4014, 7509, 125.5f}, 0x3ef5c28fu, 15}}},
         2},
        {3,
         0x12928b6cu,
         0,
         {{{{2306, 7508, 125.5f}, 0x3f5ae147u, 62},
           {{2493, 7480, 125.5f}, 0x3f2147aeu, 239},
           {{3704, 7505, 125.5f}, 0x3ef5c28fu, 241}}},
         3},
    }};
    const float clock = 0.0f;
    for (const CastleRow& row : Rows)
    {
        SCOPED_TRACE(row.seed);
        auto particles = std::make_unique<Client::Particles::Particles>();
        float shake = 0.0f;
        CrtRandom random(row.seed);
        const Client::Events::ChaosCastle::ChaosCastleCellContext context{.area = 0,
                                                                          .heroZ = 125.5f,
                                                                          .random = &random,
                                                                          .particles = particles.get(),
                                                                          .worldClock = std::cref(clock),
                                                                          .cameraShake = &shake};
        for (std::uint8_t x = 23; x <= 44; ++x)
            ASSERT_TRUE(Client::Events::ChaosCastle::StepChaosCastleCell(x, 75, context).HasValue());
        EXPECT_EQ(random.State(), row.state);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(shake), row.shake);
        for (std::size_t slot = 0; slot < row.count; ++slot)
        {
            const auto& made = particles->All()[slot];
            const CastleStone& stone = row.stones[slot];
            ASSERT_TRUE(made.alive) << slot;
            EXPECT_EQ(made.type, 0x5f6);
            EXPECT_EQ(made.subtype, 0);
            EXPECT_EQ(made.life, 32);
            EXPECT_EQ(made.position, stone.position) << slot;
            EXPECT_EQ(std::bit_cast<std::uint32_t>(made.scale), stone.scale) << slot;
            EXPECT_EQ(made.angles, (glm::vec3{stone.turn, 0.0f, 0.0f})) << slot;
            EXPECT_EQ(made.velocity, (glm::vec3{0.0f, 3.0f, 0.0f}));
            EXPECT_EQ(made.light, glm::vec3(1.0f));
        }
        EXPECT_FALSE(particles->All()[row.count].alive);
    }
}

// 0x405e00 with the pool full: the stone's two rand() % 30 already came out (they are its arguments), 0x53fe70 does not
// draw its four and the shake's rand() % 5 follows (0x405f2c). With seed 4, a stone that is not born and the shake at
// −0.1f: 26 rand() (castle.py; with the pool empty it would be 29 and no shake).
TEST(ChaosCastleTest, ACellOfTheAreaShakesWithAFullPool)
{
    const float clock = 0.0f;
    auto particles = std::make_unique<Client::Particles::Particles>();
    for (auto& particle : particles->All())
        particle.alive = true;
    float shake = 0.0f;
    CrtRandom random(4);
    const Client::Events::ChaosCastle::ChaosCastleCellContext context{.area = 0,
                                                                      .heroZ = 125.5f,
                                                                      .random = &random,
                                                                      .particles = particles.get(),
                                                                      .worldClock = std::cref(clock),
                                                                      .cameraShake = &shake};
    for (std::uint8_t x = 23; x <= 44; ++x)
        ASSERT_TRUE(Client::Events::ChaosCastle::StepChaosCastleCell(x, 75, context).HasValue());
    EXPECT_EQ(random.State(), 0x31f67436u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(shake), 0xbdcccccdu);
    EXPECT_TRUE(std::ranges::all_of(particles->All(), [](const Particle& particle) { return particle.type == 0; }));
}

// Outside the area's four rectangles there is no stone, even if rand() % 8 gives 0.
TEST(ChaosCastleTest, AnAreaCellOutsideItsRectanglesDropsNothing)
{
    const float clock = 0.0f;
    const std::uint32_t seed = SeedWhere([](CrtRandom& r) { return r.Next() % 8 == 0; });
    CrtRandom random(seed);
    const Client::Events::ChaosCastle::ChaosCastleCellContext context{
        .area = 0, .random = &random, .worldClock = std::cref(clock)};
    ASSERT_TRUE(Client::Events::ChaosCastle::StepChaosCastleCell(10, 10, context).HasValue());
    CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    EXPECT_EQ(random.State(), draws.State());
}

// 0x405490: with rand() % 10 at 0 and the count of the seen 3s other than 0, the selector is rand() % count, with no
// meteor.
TEST(ChaosCastleTest, ThePreludePicksTheSelectorAmongTheSeenTraps)
{
    const std::uint32_t seed = SeedWhere([](CrtRandom& r) { return r.Next() % 10 == 0; });
    std::vector<JointCall> calls;
    CrtRandom random(seed);
    const Client::Events::ChaosCastle::ChaosCastlePreludeContext context{.random = &random,
                                                                         .createJoint = Recorder(calls)};
    const auto selector = Client::Events::ChaosCastle::StepChaosCastlePrelude(5, context);
    ASSERT_TRUE(selector.HasValue()) << selector.Error();
    CrtRandom draws(seed);
    static_cast<void>(draws.Next());
    EXPECT_EQ(selector.Value(), draws.Next() % 5);
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_TRUE(calls.empty());
}

// Without the first case, with the hero in the safe zone it does not draw the second rand(); outside it, with another
// rand() % 10 at 0, meteor 0x6ee/9 at (x + rand() % 800 − 400, y + rand() % 800 − 400, z − 150) of the hero, with its
// angles and scale rand() % 10 + 50.
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
        const Client::Events::ChaosCastle::ChaosCastlePreludeContext safe{.random = &random,
                                                                          .heroPosition = hero,
                                                                          .heroAngles = heroAngles,
                                                                          .heroInSafeZone = true,
                                                                          .createJoint = Recorder(calls)};
        const auto selector = Client::Events::ChaosCastle::StepChaosCastlePrelude(0, safe);
        ASSERT_TRUE(selector.HasValue());
        EXPECT_EQ(selector.Value(), 0);
        CrtRandom draws(seed);
        static_cast<void>(draws.Next());
        EXPECT_EQ(random.State(), draws.State());
        EXPECT_TRUE(calls.empty());
    }
    std::vector<JointCall> calls;
    CrtRandom random(seed);
    const Client::Events::ChaosCastle::ChaosCastlePreludeContext context{
        .random = &random, .heroPosition = hero, .heroAngles = heroAngles, .createJoint = Recorder(calls)};
    const auto selector = Client::Events::ChaosCastle::StepChaosCastlePrelude(0, context);
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
    const Client::Events::ChaosCastle::ChaosCastlePreludeContext noJoint{.random = &cutRandom, .heroPosition = hero};
    const auto cut = Client::Events::ChaosCastle::StepChaosCastlePrelude(0, noJoint);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x405597"), std::string::npos) << cut.Error();
}

// 0x4055b0: only 3 counts. The .data's armed trap turns it on first (+0x60 = 10, +0x8a = 1) and disarms; then the
// selector drops with each 3 and only the one that leaves it at 0 keeps +0x8a.
TEST(ChaosCastleTest, TheSeenTrapsCountAndOnlyTheSelectedOneFires)
{
    std::int32_t selector = 2;
    std::int32_t count = 0;
    std::uint8_t armed = 1;
    WorldObject other = Object(4);
    Client::Events::ChaosCastle::StepChaosCastleSeenObject(other, selector, count, armed);
    EXPECT_EQ(count, 0);
    EXPECT_EQ(armed, 1);
    EXPECT_EQ(other.field60, 0);

    WorldObject first = Object(3);
    Client::Events::ChaosCastle::StepChaosCastleSeenObject(first, selector, count, armed);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(armed, 0);
    EXPECT_EQ(selector, 2);
    EXPECT_EQ(first.field60, 10);
    EXPECT_EQ(first.field8a, 1);

    WorldObject second = Object(3);
    Client::Events::ChaosCastle::StepChaosCastleSeenObject(second, selector, count, armed);
    EXPECT_EQ(selector, 1);
    EXPECT_EQ(second.field60, 10);
    EXPECT_EQ(second.field8a, 0);

    WorldObject third = Object(3);
    Client::Events::ChaosCastle::StepChaosCastleSeenObject(third, selector, count, armed);
    EXPECT_EQ(selector, 0);
    EXPECT_EQ(third.field60, 10);
    EXPECT_EQ(third.field8a, 1);

    WorldObject fourth = Object(3);
    Client::Events::ChaosCastle::StepChaosCastleSeenObject(fourth, selector, count, armed);
    EXPECT_EQ(count, 4);
    EXPECT_EQ(fourth.field60, 0);
    EXPECT_EQ(fourth.field8a, 0);
}

// 0x5ffdff..0x5ffe24: with the hero's +0x1a1, the alpha drops 0.15f only in 18..23.
TEST(ChaosCastleTest, TheHerosFlagFadesTheObjectsOnlyInChaosCastle)
{
    const float fade = std::bit_cast<float>(0x3e19999au);
    EXPECT_EQ(Client::Events::ChaosCastle::ChaosCastlePoseAlpha(18, 1, 1.0f), F32(1.0 - D(fade)));
    EXPECT_EQ(Client::Events::ChaosCastle::ChaosCastlePoseAlpha(23, 1, 0.5f), F32(0.5 - D(fade)));
    EXPECT_EQ(Client::Events::ChaosCastle::ChaosCastlePoseAlpha(18, 0, 1.0f), 1.0f);
    EXPECT_EQ(Client::Events::ChaosCastle::ChaosCastlePoseAlpha(17, 1, 1.0f), 1.0f);
    EXPECT_EQ(Client::Events::ChaosCastle::ChaosCastlePoseAlpha(24, 1, 1.0f), 1.0f);
}

// 0x405abb..0x405c5c: the clouds from 6 to 0xc the first time, with the object as owner, and then +0x58 = −2.
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
        const auto stepped = Client::WorldObjects::StepChaosCastleObjectFrame(object, pass);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);

        Particles expected;
        CrtRandom draws(9);
        const Client::Particles::ParticleOwner owner{Client::Particles::ParticleOwnerKind::WorldObject,
                                                     object.identity};
        for (int k = 0; k < c.count; ++k)
            ASSERT_TRUE(expected
                            .CreateOwnedChecked(Client::Particles::CloudParticle, object.transform.position,
                                                object.transform.angles, c.light, c.subtype, object.transform.scale,
                                                0.0f, draws, 5, owner)
                            .HasValue());
        ExpectSameParticles(particles, expected);
        EXPECT_EQ(random.State(), draws.State());

        const auto again = Client::WorldObjects::StepChaosCastleObjectFrame(object, pass);
        ASSERT_TRUE(again.HasValue());
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x405c6e..0x405cbc and 0x405d88..0x405d95: what each group does with the area of 0x6c634c.
TEST(ChaosCastleTest, TheAreaShowsAndHidesItsObjects)
{
    const auto shownWith = [](std::int16_t type, std::uint8_t area, std::int16_t before)
    {
        WorldObject object = Object(type);
        object.state.hiddenMesh = before;
        const GameObjectPass pass{.map = ChaosCastleMap, .chaosCastleArea = area};
        EXPECT_TRUE(Client::WorldObjects::StepChaosCastleObjectFrame(object, pass).HasValue());
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

// 0x405cc5..0x405d82: 3 with +0x8a takes bone 1 of the global pose; with +0x60 at 10, the 0x615/2 from that point to
// that point (scale rand() % 10 + 60, the object's angles) and sound 0x9c + (rand() & 1). The count drops to 4 and then
// clears +0x8a; then it falls to the area.
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
                              .requestSound = [&sounds](Client::Audio::SoundId sound)
                              { sounds.push_back(sound.Value()); },
                              .chaosCastleArea = 2};
    const auto fired = Client::WorldObjects::StepChaosCastleObjectFrame(object, pass);
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
    EXPECT_EQ(object.state.hiddenMesh, Client::Kernel::HiddenObject);

    object.state.hiddenMesh = Client::Models::NoHiddenMesh;
    for (std::int32_t expected = 8; expected >= 4; --expected)
    {
        ASSERT_TRUE(Client::WorldObjects::StepChaosCastleObjectFrame(object, pass).HasValue());
        EXPECT_EQ(object.field60, expected);
        EXPECT_EQ(object.field8a, 1);
        object.state.hiddenMesh = Client::Models::NoHiddenMesh;
    }
    ASSERT_TRUE(Client::WorldObjects::StepChaosCastleObjectFrame(object, pass).HasValue());
    EXPECT_EQ(object.field60, 4);
    EXPECT_EQ(object.field8a, 0);
    EXPECT_EQ(calls.size(), 1u);
    EXPECT_EQ(random.State(), draws.State());
}

// Hidden (+0x58 at −2) or without +0x8a, there is no trap. Without a pose, without someone to create the joint or
// without sound, it stops.
TEST(ChaosCastleTest, TheTrapNeedsItsFlagItsPoseItsJointAndItsSound)
{
    const auto pose = Pose(4);
    std::vector<JointCall> calls;
    CrtRandom random(21);
    WorldObject hidden = Object(0);
    hidden.field8a = 1;
    hidden.field60 = 10;
    hidden.state.hiddenMesh = Client::Kernel::HiddenObject;
    const GameObjectPass full{.map = ChaosCastleMap,
                              .random = &random,
                              .retainedPose = &pose,
                              .createJoint = Recorder(calls),
                              .requestSound = [](Client::Audio::SoundId) {}};
    ASSERT_TRUE(Client::WorldObjects::StepChaosCastleObjectFrame(hidden, full).HasValue());
    EXPECT_EQ(hidden.field60, 10);
    EXPECT_TRUE(calls.empty());

    WorldObject trap = Object(1);
    trap.field8a = 1;
    trap.field60 = 10;
    const GameObjectPass noPose{.map = ChaosCastleMap, .random = &random};
    const auto withoutPose = Client::WorldObjects::StepChaosCastleObjectFrame(trap, noPose);
    ASSERT_FALSE(withoutPose.HasValue());
    EXPECT_NE(withoutPose.Error().find("0x4ec110"), std::string::npos) << withoutPose.Error();

    const GameObjectPass noJoint{.map = ChaosCastleMap, .random = &random, .retainedPose = &pose};
    const auto withoutJoint = Client::WorldObjects::StepChaosCastleObjectFrame(trap, noJoint);
    ASSERT_FALSE(withoutJoint.HasValue());
    EXPECT_NE(withoutJoint.Error().find("0x405d49"), std::string::npos) << withoutJoint.Error();

    const GameObjectPass noSound{
        .map = ChaosCastleMap, .random = &random, .retainedPose = &pose, .createJoint = Recorder(calls)};
    const auto withoutSound = Client::WorldObjects::StepChaosCastleObjectFrame(trap, noSound);
    ASSERT_FALSE(withoutSound.HasValue());
    EXPECT_NE(withoutSound.Error().find("0x405d69"), std::string::npos) << withoutSound.Error();
}

// 0x5319ea, 0x531b0c..0x531bd2: the 0x615/2 takes texture 0x614, the width, 50 points and life 20; shifts x and y by
// rand() % 10 − 5 and raises z by 800, with the angles at 0 and the target at the second point. Two rand().
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

// 0x53768f..0x5379ef: the 0x615/2 rebuilds its 50 segments from +0x24: 45 that drop 16 shifted, four that advance a
// fifth of what is left and the target. Then two 0x5fd sprites of 1.5 and particle 0x6ed/0 at the target, with the
// light (x · 0.5, y · 0.6f, z), and the common tail with life over 10.
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
        expected.CreateChecked(Client::Particles::KalimaFanta, target, glm::vec3(0.0f), light, 0, 1.0f, clock, draws)
            .HasValue());

    Particles particles;
    Client::Particles::FrameSprites sprites;
    const auto stepped = Client::Joints::StepJoints(bolt.joints, bolt.random, nullptr, &particles, clock, &sprites);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(bolt.random.State(), draws.State());
    EXPECT_EQ(joint.count, 49);
    EXPECT_EQ(joint.life, 19);
    EXPECT_TRUE(joint.tailEnabled);
    EXPECT_EQ(joint.position, target);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    // With the angles at 0, the frame's third point is (x, y, z − width / 2): segment n is the n-th added counting back
    // from the last.
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

// With life under 4 the light is multiplied by 0.8333333f before the sprites; with 10 or less there is no common tail.
// Without sprites or particles it stops before drawing rand().
TEST(ChaosCastleTest, TheTrapBoltFadesAtTheEndAndNeedsItsPools)
{
    TrapBolt bolt(31);
    JointState& joint = bolt.joints[0];
    joint.life = 3;
    Particles particles;
    Client::Particles::FrameSprites sprites;
    const float clock = 0.0f;
    ASSERT_TRUE(Client::Joints::StepJoints(bolt.joints, bolt.random, nullptr, &particles, clock, &sprites).HasValue());
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
    const auto cut = Client::Joints::StepJoints(bare.joints, bare.random, nullptr, &particles, clock, nullptr);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x53770a"), std::string::npos) << cut.Error();
    EXPECT_EQ(bare.random.State(), before.State());
}

// 0x53265e, 0x5333f6..0x533449: meteor 0x6ee/9 subtracts (+0x10 = 3), draws only the second face, speed 10, angles
// (−90, 0, 0), life 20 and 15 points. No rand().
TEST(ChaosCastleTest, TheMeteorIsCreatedWithoutDraws)
{
    std::array<JointState, 1> joints{};
    glm::vec3 first{300.0f, 400.0f, 30.0f};
    glm::vec3 angles{0.0f, 0.0f, 45.0f};
    const JointRequest request{0x6ee, first, first, angles, 9, nullptr, 57.0f, -1, 0, 0, -1};
    CrtRandom random(5);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
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

// 0x53590e and 0x538ea4..0x538f37: the common advance with the matrix of (−90, 0, 0); then x and y shifted by
// (rand() % 10) − 5.0f, z rises by the speed and the speed by 1. With life under 10 the light times 0.8695652f.
TEST(ChaosCastleTest, TheMeteorRisesFasterEachStepAndFades)
{
    std::array<JointState, 1> joints{};
    glm::vec3 first{300.0f, 400.0f, 30.0f};
    glm::vec3 angles{};
    const JointRequest request{0x6ee, first, first, angles, 9, nullptr, 57.0f, -1, 0, 0, -1};
    CrtRandom random(5);
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    JointState& joint = joints[0];
    CrtRandom draws = random;
    const auto matrix = Client::Math::OriginalAngleMatrix(glm::vec3(-90.0f, 0.0f, 0.0f));
    const glm::vec3 moved = Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -10.0f, 0.0f), matrix);
    glm::vec3 p{F32(D(moved.x) + D(first.x)), F32(D(moved.y) + D(first.y)), F32(D(moved.z) + D(first.z))};
    p.x = F32((static_cast<double>(draws.Next() % 10) - 5.0) + D(p.x));
    p.y = F32((static_cast<double>(draws.Next() % 10) - 5.0) + D(p.y));
    p.z = F32(10.0 + D(p.z));
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_EQ(joint.position, p);
    EXPECT_EQ(joint.speed, 11.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.life, 19);

    joint.life = 9;
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    EXPECT_EQ(joint.light, glm::vec3(F32(D(std::bit_cast<float>(0x3f5e9bd4u)))));
    EXPECT_EQ(joint.speed, 12.0f);
}
