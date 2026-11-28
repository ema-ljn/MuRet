#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Render/ModelBatch.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Render/Pass3D.h"
#include "Render/TextureSlot.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Scenes/GameDrawTail.h"
#include "Joints/JointKernels.h"
#include "Kernel/WorldState.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The 0x614 (LightningJoints.cpp) and 0x61b/0x5bc (HandBoltJoints.cpp) families: the creation in 0x530860 (0x5309d6,
// 0x5335cf), the step in 0x535840 (0x53683e, 0x539ccc) and the common drawing in 0x53c6a0. 06, "Los joints de las
// familias".
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x51c0ffeu;
constexpr float Tenth = std::bit_cast<float>(0x3dcccccdu);

using Client::Math::D;
using Client::Math::F32;

struct Pool
{
    Client::Core::Expected<std::optional<std::size_t>, std::string>
    Create(std::int32_t type, std::int32_t subtype, glm::vec3 from, glm::vec3 to, float width, void* owner = nullptr,
           JointOwnerKind kind = JointOwnerKind::ByType, std::int32_t key = -1, std::int32_t skill = 0,
           const Client::Joints::JointKernels::CreationServices* services = nullptr)
    {
        glm::vec3 angles = startAngles;
        const Client::Kernel::JointRequest request{.type = type,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = owner,
                                                   .scale = width,
                                                   .key = key,
                                                   .skill = skill,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random, kind, services);
    }

    Client::Core::Expected<void, std::string> Step(float clock = 0.0f, Client::Kernel::WorldState* world = nullptr)
    {
        return Client::Joints::StepJoints(joints, random, nullptr, &particles, clock, nullptr, lighting.get(), {}, {},
                                          {}, {}, nullptr, nullptr, world);
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
    glm::vec3 startAngles{};
    Client::Particles::Particles particles;
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
};

ActorState Owner(glm::vec3 position)
{
    ActorState owner;
    owner.alive = true;
    owner.position = position;
    return owner;
}

// A seed whose rand() number `index` (from 0) satisfies `wanted`.
template <typename Wanted> std::uint32_t SeedWhere(int index, Wanted wanted)
{
    for (std::uint32_t seed = 1; seed < 100000; ++seed)
    {
        CrtRandom probe(seed);
        probe.Discard(static_cast<std::uint32_t>(index));
        if (wanted(probe.Next()))
            return seed;
    }
    return 0;
}

int AliveParticles(const Client::Particles::Particles& particles, std::int32_t type)
{
    int found = 0;
    for (const Client::Particles::Particle& particle : particles.All())
        found += particle.alive && particle.type == type ? 1 : 0;
    return found;
}

// 0x5309d6..0x5309ec and 0x5313b1: the 0x614/0 takes the requested width after the first segment, which used the
// retained one; speed 50, 50 points and life 2, without rand().
TEST(LightningJointsTest, TheMonsterBoltKeepsTheRetainedWidthForItsFirstQuad)
{
    Pool pool;
    pool.joints[0].width = 8.0f;
    const auto created = pool.Create(0x614, 0, {100, 200, 300}, {0, 0, 0}, 50.0f);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    const JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.points[0][0], (glm::vec3{96, 200, 300}));
    EXPECT_EQ(joint.points[0][3], (glm::vec3{100, 200, 304}));
    EXPECT_EQ(joint.width, 50.0f);
    EXPECT_EQ(joint.speed, 50.0f);
    EXPECT_EQ(joint.maximumCount, 50);
    EXPECT_EQ(joint.life, 2);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(pool.random.State(), Seed);
}

// 0x5313c0, 0x531318, 0x530a00, 0x531618 and 0x53163a: the creation's rand(), in their order.
TEST(LightningJointsTest, TheRandomSubtypesDrawInTheirNativeOrder)
{
    struct Case
    {
        std::int32_t subtype;
        float speed;
        std::int32_t life;
        int draws;
    };
    CrtRandom r(Seed);
    const int a = r.Next();
    const int b = r.Next();
    const int c = r.Next();
    const std::vector<Case> cases{
        {1, F32(static_cast<double>(a % 10) + 20.0), b % 8 + 8, 2},
        {0x10, F32(static_cast<double>(a % 10) + 20.0), b % 2 + 2, 2},
        {0x11, F32(static_cast<double>(a % 10) + 20.0), 10, 1},
        {8, static_cast<float>(a % 10 + 6), b % 8 + 8, 2},
        {9, F32(static_cast<double>(a % 20) + 80.0), 30, 2},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(probe.subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(0x614, probe.subtype, {}, {}, 30.0f).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.speed, probe.speed);
        EXPECT_EQ(joint.life, probe.life);
        CrtRandom expected(Seed);
        expected.Discard(static_cast<std::uint32_t>(probe.draws));
        EXPECT_EQ(pool.random.State(), expected.State());
        if (probe.subtype == 9)
        {
            const float gray = F32(static_cast<double>(b % 10) * D(std::bit_cast<float>(0x3d888889u)) + D(Tenth));
            EXPECT_EQ(joint.light, glm::vec3(gray));
        }
        if (probe.subtype == 8)
        {
            EXPECT_EQ(joint.width, 5.0f);
            EXPECT_EQ(joint.maximumCount, 2);
        }
    }
    static_cast<void>(c);
}

// 0x531460 (the /3: the life in the key and the points and speed in the skill), 0x531494/0x5314ed (the /4 and the /5,
// at the target and 600 higher), 0x53139d (the /0xb) and 0x531376 (the /0xd, which becomes a /0xb).
TEST(LightningJointsTest, TheFixedSubtypesWriteTheirFields)
{
    const glm::vec3 halfBlue{0.5f, 0.5f, 1.0f};
    {
        Pool pool;
        ASSERT_TRUE(
            pool.Create(0x614, 3, {1, 2, 3}, {}, 20.0f, nullptr, JointOwnerKind::ByType, 0x1234, 0x10c).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.life, 0x1234);
        EXPECT_EQ(joint.maximumCount, 12);
        EXPECT_EQ(joint.speed, 12.0f);
        EXPECT_EQ(joint.key, -1);
        EXPECT_EQ(joint.light, halfBlue);
        EXPECT_EQ(joint.pathIncrement, (glm::vec3{1, 2, 3}));
    }
    for (const std::int32_t subtype : {4, 5})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        pool.joints[0].velocity = {7, 8, 9};
        ASSERT_TRUE(pool.Create(0x614, subtype, {1, 2, 3}, {10, 20, 30}, 20.0f).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.speed, subtype == 4 ? 60.0f : -60.0f);
        EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.maximumCount, 10);
        EXPECT_EQ(joint.targetPosition, (glm::vec3{10, 20, subtype == 4 ? 30.0f : 630.0f}));
        EXPECT_EQ(joint.pathIncrement, (glm::vec3{1, 2, 3}));
        EXPECT_EQ(pool.random.State(), Seed);
    }
    for (const std::int32_t subtype : {0xb, 0xd})
    {
        SCOPED_TRACE(subtype);
        Pool pool;
        ASSERT_TRUE(pool.Create(0x614, subtype, {}, {}, 20.0f).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.subtype, 0xb);
        EXPECT_EQ(joint.life, 2);
        EXPECT_EQ(joint.light, subtype == 0xb ? glm::vec3(1.0f, 0.5f, Tenth) : glm::vec3(1.0f, Tenth, Tenth));
    }
}

// 0x5313fe: the /2 aims at bone 33 of the owner character, (0, −150, 0), with the player's model; without an owner or
// without the service, it stops.
TEST(LightningJointsTest, TheOwnerBoneBoltAimsAtTheChestOfItsOwner)
{
    ActorState owner = Owner({500, 600, 700});
    std::size_t askedBone = 0;
    glm::vec3 askedOffset{};
    const Client::Joints::JointKernels::CreationServices services{
        .playerBonePoint = [&](const ActorState& bones, std::size_t bone, const glm::vec3& offset)
        {
            askedBone = bone;
            askedOffset = offset;
            return bones.position + glm::vec3(1, 2, 3);
        }};
    Pool pool;
    ASSERT_TRUE(pool.Create(0x614, 2, {}, {}, 50.0f, &owner, JointOwnerKind::Actor, -1, 0, &services).HasValue());
    const JointState& joint = pool.joints[0];
    EXPECT_EQ(askedBone, 33u);
    EXPECT_EQ(askedOffset, (glm::vec3{0, -150, 0}));
    EXPECT_EQ(joint.targetPosition, (glm::vec3{501, 602, 703}));
    EXPECT_EQ(joint.light, (glm::vec3{1.0f, Tenth, 0.0f}));
    EXPECT_EQ(joint.life, 2);

    Pool unserved;
    const auto cut = unserved.Create(0x614, 2, {}, {}, 50.0f, &owner, JointOwnerKind::Actor);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x531456"), std::string::npos) << cut.Error();
    Pool orphan;
    const auto lonely = orphan.Create(0x614, 2, {}, {}, 50.0f, nullptr, JointOwnerKind::ByType, -1, 0, &services);
    ASSERT_FALSE(lonely.HasValue());
    EXPECT_NE(lonely.Error().find("0x531412"), std::string::npos) << lonely.Error();
}

// 0x531356: the /0xc is born red with life 10. Its step (0x536adf..0x536b22) puts the position at the point (0, −100,
// 0) of the matrix 0x743751c of the owner type's model: nothing writes it in c32bbd5f and it stays zero, so the point
// is the shared model's position. It skips 0x4e9810: "what is left" is 0x53586c's [ebp+8], the position's y minus the
// target's before moving, signed.
TEST(LightningJointsTest, TheRedBoltSitsOnTheSharedModelOfItsOwner)
{
    ActorState owner = Owner({1.0f, 2.0f, 3.0f});
    owner.type = 0x1a5;
    auto world = std::make_unique<Client::Kernel::WorldState>();
    const glm::vec3 origin{700.25f, 800.5f, 900.75f};
    world->PlaceModel(owner.type, {.scale = 1.5f, .origin = origin});
    // Start y minus target y: 40 (< 50 · 1.5, arrives), −500 (arrives: no absolute value), 75 (50 · 1.5 = 75 exactly:
    // with 0x536d71's strict > it does not arrive in that segment) and 80 (does not arrive). With an owner, the
    // creation does not copy the second point (0x530942 jumps to 0x530957): the target is the slot's old one, (0, 0,
    // 0), not the (0, 1000, 0) passed.
    for (const float startY : {40.0f, -500.0f, 75.0f, 80.0f})
    {
        SCOPED_TRACE(startY);
        Pool pool;
        ASSERT_TRUE(
            pool.Create(0x614, 0xc, {10.0f, startY, 30.0f}, {0.0f, 1000.0f, 0.0f}, 30.0f, &owner, JointOwnerKind::Actor)
                .HasValue());
        EXPECT_EQ(pool.joints[0].targetPosition, glm::vec3(0.0f));
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.light, (glm::vec3{1.0f, Tenth, Tenth}));
        EXPECT_EQ(joint.life, 10);
        EXPECT_EQ(joint.speed, 50.0f);
        EXPECT_EQ(joint.maximumCount, 50);
        const std::int32_t count = joint.count;
        CrtRandom twin = pool.random;
        const auto stepped = pool.Step(0.0f, world.get());
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        const bool arrives = startY < 75.0f;
        // Each segment draws the default shake's two rand() (0x536cb3); with width 30 there is no light and no
        // particles.
        const int segments = arrives ? 1 : 50;
        glm::vec3 shaken{};
        for (int segment = 0; segment < segments; ++segment)
        {
            shaken.x = F32(static_cast<double>((twin.Next() & 0x3ff) - 0x200) / 30.0);
            shaken.z = F32(static_cast<double>((twin.Next() & 0x3ff) - 0x200) / 30.0);
        }
        EXPECT_EQ(pool.random.State(), twin.State());
        EXPECT_EQ(joint.velocity.x, shaken.x);
        EXPECT_EQ(joint.velocity.z, shaken.z);
        shaken = {F32(D(joint.angles.x) + D(shaken.x)), F32(D(joint.velocity.y) + D(joint.angles.y)),
                  F32(D(shaken.z) + D(joint.angles.z))};
        // One shift per segment and one from the common tail (CommonTail), up to 49 (0x53553f).
        EXPECT_EQ(joint.count, std::min(count + segments + 1, joint.maximumCount - 1));
        if (arrives)
        {
            EXPECT_EQ(joint.position, origin);
            continue;
        }
        const auto moved =
            Client::Math::OriginalVectorRotate({0.0f, -50.0f, 0.0f}, Client::Math::OriginalAngleMatrix(shaken));
        EXPECT_EQ(joint.position, (glm::vec3{F32(D(moved.x) + D(origin.x)), F32(D(moved.y) + D(origin.y)),
                                             F32(D(moved.z) + D(origin.z))}));
    }

    Pool orphan;
    ASSERT_TRUE(orphan.Create(0x614, 0xc, {}, {}, 30.0f).HasValue());
    const auto lonely = orphan.Step(0.0f, world.get());
    ASSERT_FALSE(lonely.HasValue());
    EXPECT_TRUE(lonely.Error().starts_with("0x536adf")) << lonely.Error();
    Pool unserved;
    ASSERT_TRUE(unserved.Create(0x614, 0xc, {}, {}, 30.0f, &owner, JointOwnerKind::Actor).HasValue());
    const auto cut = unserved.Step();
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x536b1d")) << cut.Error();
}

// 0x531907..0x5319e5: the /0xf is not drawn, lives 80 and picks up to six nearby characters (at 400 or less), alive,
// without +0x389, of class 2, visible and not the owner; +0x9eb is the count · 15 and +0x24 the second point 150
// higher. Its step does not move (0x53587c); with even life it drops two 0x614/0 and stops at the light of its 0x5ca
// (0x5370e8, Q-C1622); with odd life only the count runs (no drawing, the matrix on the stack is not seen).
TEST(LightningJointsTest, TheChainPicksTheNearMonsters)
{
    std::vector<ActorState> actors(14);
    for (std::size_t i = 0; i < actors.size(); ++i)
    {
        actors[i] = Owner({100.0f + static_cast<float>(i) * 10.0f, 0.0f, 0.0f});
        actors[i].byte88 = 2;
        actors[i].seen = true;
    }
    actors[0].position.x = 600.0f; // lejos
    actors[2].dying = 1;
    actors[3].byte88 = 1;
    actors[4].seen = false;
    actors[5].alive = false;
    // Exactly at 400 it gets in (0x53194f: test ah,0x41); just farther, it does not. There are seven candidates: the
    // seventh (13) stays out by the cap of six (0x531995: cmp [+0x9ec],5; jg).
    actors[6].position = {500.0f, 0.0f, 0.0f};
    actors[7].position = {500.0f, 1.0f, 0.0f};
    const Client::Joints::JointKernels::CreationServices services{.actors = actors};
    Pool pool;
    Pool unserved;
    const auto cut = unserved.Create(0x614, 0xf, {}, {}, 50.0f);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x531922")) << cut.Error();
    ASSERT_TRUE(pool.Create(0x614, 0xf, {100.0f, 0.0f, 0.0f}, {7.0f, 8.0f, 9.0f}, 50.0f, &actors[1],
                            JointOwnerKind::Actor, -1, 0, &services)
                    .HasValue());
    JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.drawEnabled, 0);
    EXPECT_EQ(joint.life, 80);
    EXPECT_EQ(joint.maximumCount, 0);
    EXPECT_EQ(joint.field9ec, 0);
    EXPECT_EQ(joint.volley, 90);
    EXPECT_EQ(joint.chainTargets, (std::array<std::int32_t, 6>{6, 8, 9, 10, 11, 12}));
    EXPECT_EQ(joint.skillSerial, 12);
    EXPECT_EQ(joint.pathIncrement, (glm::vec3{7.0f, 8.0f, 159.0f}));

    const auto stepped = pool.Step();
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_TRUE(stepped.Error().starts_with("0x5370e8")) << stepped.Error();
    EXPECT_EQ(joint.position, (glm::vec3{100.0f, 0.0f, 0.0f}));
    for (std::size_t i = 1; i <= 2; ++i)
    {
        SCOPED_TRACE(i);
        EXPECT_TRUE(pool.joints[i].alive);
        EXPECT_EQ(pool.joints[i].type, 0x614);
        EXPECT_EQ(pool.joints[i].subtype, 0);
        EXPECT_EQ(pool.joints[i].width, i == 1 ? 50.0f : 10.0f);
        EXPECT_EQ(pool.joints[i].targetPosition, joint.pathIncrement);
    }
    pool.joints[1].alive = false;
    pool.joints[2].alive = false;
    joint.life = 79;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(joint.position, (glm::vec3{100.0f, 0.0f, 0.0f}));
    EXPECT_EQ(joint.count, -1);
    EXPECT_EQ(joint.life, 78);
    EXPECT_EQ(pool.random.State(), Seed);
}

// 0x530860 with the pool full: nothing, not even the /1's rand().
TEST(LightningJointsTest, AFullPoolCreatesNothingAndDrawsNothing)
{
    Pool pool;
    for (JointState& joint : pool.joints)
        joint.alive = true;
    const auto created = pool.Create(0x614, 1, {}, {}, 30.0f);
    ASSERT_TRUE(created.HasValue());
    EXPECT_FALSE(created.Value().has_value());
    EXPECT_EQ(pool.random.State(), Seed);
    const auto bolt = pool.Create(0x61b, 0, {}, {}, 30.0f);
    ASSERT_TRUE(bolt.HasValue());
    EXPECT_FALSE(bolt.Value().has_value());
}

// 0x536982 and 0x536e95..0x537042: the /0 follows its owner (80 higher); on arrival, with the width other than 50 there
// are no particles, and with the clock in the second half of the second, no branch either: the shake's two rand() and
// the tail.
TEST(LightningJointsTest, TheMonsterBoltFollowsItsOwnerAndStopsWhenItArrives)
{
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    ASSERT_TRUE(pool.Create(0x614, 0, {1000, 1040, 80}, {}, 10.0f, &owner, JointOwnerKind::Actor).HasValue());
    JointState& joint = pool.joints[0];
    const JointState before = joint;
    ASSERT_TRUE(pool.Step(600.0f).HasValue());
    EXPECT_EQ(joint.targetPosition, (glm::vec3{1000, 1000, 80}));
    CrtRandom expected(Seed);
    expected.Discard(2);
    EXPECT_EQ(pool.random.State(), expected.State());
    // The common movement (speed 50 with the previous angles) and nothing else: it arrives in the first segment.
    const glm::vec3 moved = Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -50.0f, 0.0f),
                                                               Client::Math::OriginalAngleMatrix(before.angles));
    EXPECT_EQ(joint.position, (glm::vec3{F32(D(before.position.x) + D(moved.x)), F32(D(before.position.y) + D(moved.y)),
                                         F32(D(before.position.z) + D(moved.z))}));
    EXPECT_EQ(joint.life, 1);
    EXPECT_EQ(joint.count, 2);
    EXPECT_EQ(AliveParticles(pool.particles, Client::Particles::ThunderParticle), 0);
}

// 0x536f71..0x53703d: with the width at 50, in the first half of the second and with rand() % 16 at 0, the /0 drops a
// /1 from the shifted target (rand() % 100 − 50, rand() % 100 − 50, rand() % 120 − 60) with width rand() % 8 + 6, of
// the same owner. Before: the shake's two rand(), the 0x5ca/0's two (0x536eb9) and the % 8 that does not drop the
// 0x5f2.
TEST(LightningJointsTest, TheMonsterBoltBranchesInTheFirstHalfOfTheSecond)
{
    std::uint32_t seed = 0;
    for (std::uint32_t probe = 1; probe < 100000 && seed == 0; ++probe)
    {
        CrtRandom draws(probe);
        draws.Discard(4);
        const int breath = draws.Next();
        if (breath % 8 != 0 && draws.Next() % 16 == 0)
            seed = probe;
    }
    ASSERT_NE(seed, 0u);
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    pool.random = CrtRandom(seed);
    ASSERT_TRUE(pool.Create(0x614, 0, {1000, 1040, 80}, {}, 50.0f, &owner, JointOwnerKind::Actor).HasValue());
    // The parent in slot 2: the branch lands in 0, which the step already left behind (0x53c670 walks the 500 in
    // order).
    pool.joints[2] = pool.joints[0];
    pool.joints[0] = JointState{};
    ASSERT_TRUE(pool.Step(400.0f).HasValue());
    EXPECT_EQ(AliveParticles(pool.particles, Client::Particles::ThunderParticle), 1);
    CrtRandom expected(seed);
    expected.Discard(6);
    const int x = expected.Next() % 100 - 50;
    const int y = expected.Next() % 100 - 50;
    const int z = expected.Next() % 120 - 60;
    const float width = F32(static_cast<double>(expected.Next() % 8) + 6.0);
    const float speed = F32(static_cast<double>(expected.Next() % 10) + 20.0);
    const std::int32_t life = expected.Next() % 8 + 8;
    const JointState& branch = pool.joints[0];
    ASSERT_TRUE(branch.alive);
    EXPECT_EQ(branch.type, 0x614);
    EXPECT_EQ(branch.subtype, 1);
    EXPECT_EQ(branch.ownerContext, &owner);
    EXPECT_EQ(branch.ownerKind, JointOwnerKind::Actor);
    EXPECT_EQ(branch.width, width);
    EXPECT_EQ(branch.speed, speed);
    EXPECT_EQ(branch.position, (glm::vec3{F32(x + 1000.0), F32(y + 1000.0), F32(z + 80.0)}));
    EXPECT_EQ(branch.targetPosition, glm::vec3(0.0f)); // With an owner, +0x58 keeps the previous use's value.
    EXPECT_EQ(branch.life, life);
    EXPECT_EQ(pool.random.State(), expected.State());
    EXPECT_FALSE(pool.joints[1].alive);
}

// 0x536e9c: with a width other than 50 (the 0x614/0 of width 10 from 0x4f5b70's tail) the arrival jumps whole to
// 0x537045: no 0x5ca, no % 8, no /0 branch even though the clock and the rand() would have dropped it.
TEST(LightningJointsTest, AThinMonsterBoltNeverBranches)
{
    const std::uint32_t seed = SeedWhere(2, [](int value) { return value % 16 == 0; });
    ASSERT_NE(seed, 0u);
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    pool.random = CrtRandom(seed);
    ASSERT_TRUE(pool.Create(0x614, 0, {1000, 1040, 80}, {}, 10.0f, &owner, JointOwnerKind::Actor).HasValue());
    pool.joints[2] = pool.joints[0];
    pool.joints[0] = JointState{};
    ASSERT_TRUE(pool.Step(400.0f).HasValue());
    CrtRandom expected(seed);
    expected.Discard(2);
    EXPECT_EQ(pool.random.State(), expected.State());
    EXPECT_FALSE(pool.joints[0].alive);
    EXPECT_EQ(AliveParticles(pool.particles, Client::Particles::ThunderParticle), 0);
}

// 0x536d4d: the /3 far from its target (more than 150) sets its life to 0 and the common tail kills it.
TEST(LightningJointsTest, TheFarThirdSubtypeDiesAfterItsSegment)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x614, 3, {}, {}, 20.0f, nullptr, JointOwnerKind::ByType, 5, 1).HasValue());
    JointState& joint = pool.joints[0];
    joint.targetPosition = {0, 1000, 0};
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -1);
    CrtRandom expected(Seed);
    expected.Discard(2);
    EXPECT_EQ(pool.random.State(), expected.State());
}

// 0x5335cf..0x533672: the 0x61b repeats the texture; both with speed 40, width 60, 50 points and life 2; the /0 white,
// the /3 still with life 20, and the others red (0x61b) or blue (0x5bc). The first segment uses the retained width.
TEST(HandBoltJointsTest, TheBoltsWriteTheirLightSpeedAndWidth)
{
    struct Case
    {
        std::int32_t type;
        std::int32_t subtype;
        glm::vec3 light;
        float speed;
        std::int32_t life;
    };
    const std::vector<Case> cases{
        {0x61b, 0, glm::vec3(1.0f), 40.0f, 2}, {0x61b, 1, {std::bit_cast<float>(0x3eb33333u), Tenth, 1.0f}, 40.0f, 2},
        {0x61b, 3, glm::vec3(1.0f), 0.0f, 20}, {0x5bc, 1, {0.0f, std::bit_cast<float>(0x3e99999au), 1.0f}, 40.0f, 2},
        {0x5bc, 0, glm::vec3(1.0f), 40.0f, 2},
    };
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(testing::Message() << probe.type << "/" << probe.subtype);
        Pool pool;
        pool.joints[0].width = 4.0f;
        ASSERT_TRUE(pool.Create(probe.type, probe.subtype, {10, 20, 30}, {}, 10.0f).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.repeatedTexture, probe.type == 0x61b);
        EXPECT_EQ(joint.light, probe.light);
        EXPECT_EQ(joint.speed, probe.speed);
        EXPECT_EQ(joint.life, probe.life);
        EXPECT_EQ(joint.width, 60.0f);
        EXPECT_EQ(joint.maximumCount, 50);
        EXPECT_EQ(joint.texture, probe.type);
        EXPECT_EQ(joint.points[0][0], (glm::vec3{8, 20, 30}));
        EXPECT_EQ(pool.random.State(), Seed);
    }
}

// 0x539ce1..0x539ec9: a segment far from the owner: the target 80 above the owner, the shake accumulated and damped at
// 0.8f (with the z in the x87), the dynamic light ((rand() & 3) + 4) · 0.05f and the advance of 40 with the shaken
// matrix.
TEST(HandBoltJointsTest, AFarBoltShakesGlowsAndAdvances)
{
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    ASSERT_TRUE(pool.Create(0x61b, 1, {500, 500, 0}, {}, 50.0f, &owner, JointOwnerKind::Actor).HasValue());
    JointState& joint = pool.joints[0];
    joint.maximumCount = 1;
    joint.velocity = {2, 5, 3};
    glm::vec3 position = joint.position;
    const glm::vec3 common = Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -40.0f, 0.0f),
                                                                Client::Math::OriginalAngleMatrix(joint.angles));
    for (int axis = 0; axis < 3; ++axis)
        position[axis] = F32(D(position[axis]) + D(common[axis]));
    glm::vec3 angles = joint.angles;
    const glm::vec3 target{1000, 1000, 80};
    static_cast<void>(Client::Math::MoveToward(position, angles, target, 25.0f));

    ASSERT_TRUE(pool.Step().HasValue());

    CrtRandom expected(Seed);
    const double damping = D(std::bit_cast<float>(0x3f4ccccdu));
    const float vx = F32(F32(static_cast<double>((expected.Next() & 0xff) - 0x80) / 60.0 + 2.0) * damping);
    const float vy = F32(5.0 * damping);
    const double roll = (static_cast<double>((expected.Next() & 0xff) - 0x80) / 60.0 + 3.0) * damping;
    static_cast<void>(expected.Next());
    EXPECT_EQ(pool.random.State(), expected.State());
    EXPECT_EQ(joint.targetPosition, target);
    EXPECT_EQ(joint.velocity, (glm::vec3{vx, vy, F32(roll)}));
    const glm::vec3 shaken{F32(D(angles.x) + D(vx)), F32(D(vy) + D(angles.y)), F32(roll + D(angles.z))};
    const glm::vec3 moved =
        Client::Math::OriginalVectorRotate(glm::vec3(0.0f, -40.0f, 0.0f), Client::Math::OriginalAngleMatrix(shaken));
    EXPECT_EQ(joint.position, (glm::vec3{F32(D(moved.x) + D(position.x)), F32(D(moved.y) + D(position.y)),
                                         F32(D(moved.z) + D(position.z))}));
    EXPECT_EQ(joint.life, 1);
    EXPECT_TRUE(joint.alive);
}

// 0x539ed4..0x539f11: on arrival, once every two, three unused rand() and the fire 0x5d9/0 at the position, with the
// joint's angles and light; the other time, nothing.
TEST(HandBoltJointsTest, AnArrivingBoltDropsFireEveryOtherTime)
{
    for (const bool even : {true, false})
    {
        SCOPED_TRACE(even);
        const std::uint32_t seed = SeedWhere(2, [even](int value) { return (value % 2 == 0) == even; });
        ASSERT_NE(seed, 0u);
        ActorState owner = Owner({1000, 1000, 0});
        Pool pool;
        pool.random = CrtRandom(seed);
        ASSERT_TRUE(pool.Create(0x5bc, 1, {1000, 1000, 80}, {}, 50.0f, &owner, JointOwnerKind::Actor).HasValue());
        JointState& joint = pool.joints[0];
        ASSERT_TRUE(pool.Step().HasValue());
        CrtRandom expected(seed);
        expected.Discard(3);
        if (even)
        {
            expected.Discard(3);
            Client::Particles::Particles reference;
            ASSERT_TRUE(reference
                            .CreateChecked(Client::Particles::FireParticle, joint.position, joint.angles, joint.light,
                                           0, 1.0f, 0.0f, expected)
                            .HasValue());
        }
        EXPECT_EQ(pool.random.State(), expected.State());
        EXPECT_EQ(AliveParticles(pool.particles, Client::Particles::FireParticle), even ? 1 : 0);
        EXPECT_EQ(joint.count, 2);
        EXPECT_EQ(joint.life, 1);
    }
}

// 0x53c6a0: the 0x61b is drawn with its texture (JointLaser02, repeated) and the common additive, two faces per
// segment.
TEST(HandBoltJointsTest, TheBoltDrawsWithItsOwnTexture)
{
    ActorState owner = Owner({1000, 1000, 0});
    Pool pool;
    ASSERT_TRUE(pool.Create(0x61b, 0, {0, 0, 0}, {}, 50.0f, &owner, JointOwnerKind::Actor).HasValue());
    JointState& joint = pool.joints[0];
    joint.maximumCount = 3;
    ASSERT_TRUE(pool.Step().HasValue());
    ASSERT_EQ(joint.count, 2);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, pool.joints, 0).HasValue());
    ASSERT_TRUE(batch.State().texture.has_value());
    EXPECT_EQ(*batch.State().texture, Client::Render::TextureSlot{0x61b});
    EXPECT_EQ(batch.State().blend, Client::Render::ModelBlend::Additive);
    EXPECT_EQ(batch.Vertices().size(), 2u * 12u);
}
} // namespace
