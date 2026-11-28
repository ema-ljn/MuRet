#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Effects/ModelEffects.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The 0x60f and the 0x62b with subtype 0x10, 0xe, 0xf and 0x13 (OrbitJoints.cpp and DropJoints.cpp): creation 0x533677
// (0x5337fa, 0x534132 and 0x533a28) and step 0x539f1b (0x53a5c7, 0x53b18a and 0x53a12c). The /0xe and /0xf expected
// values come from another run (exact Fraction and the sine with 80 digits, the reviewer's x87.py).
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x60fu;

using Client::Math::Bits;
using Client::Math::F32;

struct OrbitWorld
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t type, std::int32_t subtype,
                                                                           void* owner, float scale,
                                                                           JointOwnerKind kind = JointOwnerKind::ByType)
    {
        glm::vec3 from = start;
        glm::vec3 to = second;
        glm::vec3 angles{};
        const Client::Kernel::JointRequest request{.type = type,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = owner,
                                                   .scale = scale,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random, kind);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0.0f, nullptr);
    }

    glm::vec3 start{1000.0f, 2000.0f, 100.0f};
    glm::vec3 second{1010.0f, 2020.0f, 400.0f};
    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
};
} // namespace

// 0x5337fa..0x533855: the shadow rain's /0x10 (0x51bcb0, with the character as owner and width 120), without rand():
// without the common tail, the light at 0.1f, life 60, the target at the second point and the width; twenty times z +
// 50 and a segment. The count stays at 19 and the creation's first segment (the one at z 100) drops off at the 20th.
TEST(OrbitJointsTest, TheShadowColumnRisesTwentyTimesFifty)
{
    for (const std::int32_t type : {0x60f, 0x62b})
    {
        SCOPED_TRACE(type);
        OrbitWorld tested;
        ActorState owner{};
        ASSERT_TRUE(tested.Create(type, 0x10, &owner, 120.0f).HasValue());

        EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
        const JointState& joint = tested.joints[0];
        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(Bits(joint.light.x), 0x3dcccccdu);
        EXPECT_EQ(joint.light.x, joint.light.z);
        EXPECT_EQ(joint.life, 60);
        EXPECT_EQ(joint.maximumCount, 20);
        EXPECT_EQ(joint.width, 120.0f);
        EXPECT_EQ(joint.speed, 0.0f);
        EXPECT_EQ(joint.texture, type);
        EXPECT_EQ(joint.targetPosition, tested.second);
        EXPECT_EQ(joint.position, (glm::vec3{1000.0f, 2000.0f, 1100.0f}));
        EXPECT_EQ(joint.count, 19);
        EXPECT_EQ(joint.points[0][0], (glm::vec3{940.0f, 2000.0f, 1100.0f}));
        EXPECT_EQ(joint.points[19][3], (glm::vec3{1000.0f, 2000.0f, 210.0f}));
    }
}

// 0x53a5c7..0x53a6ae: the character that moved (10, −20, 5) from the target shifts segments 18..0 the same (19, the
// count's, not) and the target becomes its position. The light: with life 60, times 1.04f; with 30, at 0.25; with 15,
// times 0.9090909f; with 5, also times 0.7692308f (0x53b318). Without the common tail or movement: the count stays 19.
TEST(OrbitJointsTest, TheShadowColumnFollowsItsOwner)
{
    constexpr float Dim = std::bit_cast<float>(0x3dcccccdu);
    struct Probe
    {
        std::int32_t life;
        float light;
    };
    for (const Probe& probe :
         {Probe{60, F32(static_cast<double>(Dim) * static_cast<double>(1.04f))}, Probe{30, 0.25f},
          Probe{15, F32(static_cast<double>(Dim) * static_cast<double>(std::bit_cast<float>(0x3f68ba2eu)))},
          Probe{5, F32(static_cast<double>(
                           F32(static_cast<double>(Dim) * static_cast<double>(std::bit_cast<float>(0x3f68ba2eu)))) *
                       static_cast<double>(std::bit_cast<float>(0x3f44ec4fu)))}})
    {
        SCOPED_TRACE(probe.life);
        OrbitWorld tested;
        ActorState owner{};
        ASSERT_TRUE(tested.Create(0x60f, 0x10, &owner, 120.0f).HasValue());
        JointState& joint = tested.joints[0];
        joint.life = probe.life;
        owner.position = tested.second + glm::vec3(10.0f, -20.0f, 5.0f);
        const glm::vec3 first = joint.points[0][1];
        const glm::vec3 eighteenth = joint.points[18][2];
        const glm::vec3 last = joint.points[19][0];

        ASSERT_TRUE(tested.Step().HasValue());

        EXPECT_EQ(joint.targetPosition, owner.position);
        EXPECT_EQ(joint.points[0][1], first + glm::vec3(10.0f, -20.0f, 5.0f));
        EXPECT_EQ(joint.points[18][2], eighteenth + glm::vec3(10.0f, -20.0f, 5.0f));
        EXPECT_EQ(joint.points[19][0], last);
        EXPECT_EQ(joint.position, (glm::vec3{1000.0f, 2000.0f, 1100.0f}));
        EXPECT_EQ(joint.count, 19);
        EXPECT_EQ(Bits(joint.light.y), Bits(probe.light));
        EXPECT_EQ(joint.life, probe.life - 1);
    }

    // Without an owner (0x53a5cc), nothing: neither the segments nor the light.
    OrbitWorld ownerless;
    ASSERT_TRUE(ownerless.Create(0x62b, 0x10, nullptr, 120.0f).HasValue());
    const glm::vec3 kept = ownerless.joints[0].points[3][0];
    ASSERT_TRUE(ownerless.Step().HasValue());
    EXPECT_EQ(ownerless.joints[0].points[3][0], kept);
    EXPECT_EQ(Bits(ownerless.joints[0].light.x), 0x3dcccccdu);
}

// 0x534132..0x5341f7: the /0xe and /0xf with blend 4, texture 0x613, the width, vz (rand() % 150) · 0.01f, vy rand() %
// 500 − 250, speed 30, the light (0.2f; 0.4f; 0.8f), life 20 and z + rand() % 25. With a width over 10, vz is drawn
// again: (rand() % 250 + 150) · 0.01f; with exactly 10, it is not.
TEST(OrbitJointsTest, TheWhirlBornsWithThreeOrFourDraws)
{
    constexpr double Hundredth = static_cast<double>(0.01f);
    for (const float width : {10.0f, 40.0f})
    {
        SCOPED_TRACE(width);
        OrbitWorld tested;
        Client::Effects::ModelEffect effect{};
        ASSERT_TRUE(tested.Create(0x62b, 0xf, &effect, width, JointOwnerKind::ModelEffect).HasValue());
        CrtRandom replay{Seed};
        float lift = F32(static_cast<double>(replay.Next() % 150) * Hundredth);
        const auto turn = static_cast<float>(replay.Next() % 500 - 250);
        if (width > 10.0f)
            lift = F32(static_cast<double>(replay.Next() % 250 + 150) * Hundredth);
        const auto height = static_cast<float>(replay.Next() % 25);

        EXPECT_EQ(tested.random.State(), replay.State());
        const JointState& joint = tested.joints[0];
        EXPECT_EQ(joint.blend, 4);
        EXPECT_EQ(joint.texture, 0x613);
        EXPECT_TRUE(joint.tailEnabled);
        EXPECT_EQ(joint.width, width);
        EXPECT_EQ(joint.velocity, (glm::vec3{0.0f, turn, lift}));
        EXPECT_EQ(joint.speed, 30.0f);
        EXPECT_EQ(Bits(joint.light.x), 0x3e4ccccdu);
        EXPECT_EQ(Bits(joint.light.y), 0x3ecccccdu);
        EXPECT_EQ(Bits(joint.light.z), 0x3f4ccccdu);
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.maximumCount, 20);
        EXPECT_EQ(joint.position.z, F32(static_cast<double>(height) + 100.0));
        EXPECT_EQ(joint.targetPosition, tested.second);
        EXPECT_EQ(joint.ownerContext, &effect);
    }
}

// 0x53b18a..0x53b234: with an owner, after the common movement (with the angles at 0, (0, −30, 0)), x and y turn around
// the target with a = vy · 0.1f (the /0xf, −a) and the radius speed; z + vz. With life under 20, speed + 5; width + 1,
// vy + 4 and vz + (rand() % 200 + 200) · 0.01f. The common tail follows, with its segment.
TEST(OrbitJointsTest, TheWhirlTurnsAroundItsTarget)
{
    struct Probe
    {
        std::int32_t subtype;
        float turn, speed;
        glm::vec3 target;
        float lift, height;
        std::uint32_t x, y, z;
        float nextSpeed;
    };
    for (
        const Probe& probe :
        {Probe{
             0xe, 123.0f, 30.0f, {1100.5f, 2100.25f, 0.0f}, 1.5f, 300.0f, 0x448d2e25u, 0x4503c25au, 0x4396c000u, 35.0f},
         Probe{
             0xf, 123.0f, 30.0f, {1100.5f, 2100.25f, 0.0f}, 1.5f, 300.0f, 0x448d2e25u, 0x4502c5a6u, 0x4396c000u, 35.0f},
         Probe{0xe,
               -237.0f,
               47.0f,
               {-350.75f, 812.5f, 0.0f},
               2.25f,
               77.0f,
               0xc3ac2418u,
               0x443f7c9fu,
               0x429e8000u,
               52.0f}})
    {
        SCOPED_TRACE(probe.y);
        OrbitWorld tested;
        Client::Effects::ModelEffect effect{};
        ASSERT_TRUE(tested.Create(0x62b, probe.subtype, &effect, 40.0f, JointOwnerKind::ModelEffect).HasValue());
        JointState& joint = tested.joints[0];
        joint.angles = glm::vec3(0.0f);
        joint.velocity = {0.0f, probe.turn, probe.lift};
        joint.speed = probe.speed;
        joint.targetPosition = probe.target;
        joint.position.z = probe.height;
        joint.life = 15;
        CrtRandom replay = tested.random;
        const float lift =
            F32(static_cast<double>(replay.Next() % 200 + 200) * static_cast<double>(0.01f) + probe.lift);

        ASSERT_TRUE(tested.Step().HasValue());

        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_EQ(Bits(joint.position.x), probe.x);
        EXPECT_EQ(Bits(joint.position.y), probe.y);
        EXPECT_EQ(Bits(joint.position.z), probe.z);
        EXPECT_EQ(joint.speed, probe.nextSpeed);
        EXPECT_EQ(joint.width, 41.0f);
        EXPECT_EQ(joint.velocity.y, probe.turn + 4.0f);
        EXPECT_EQ(joint.velocity.z, lift);
        EXPECT_EQ(joint.count, 1);
        EXPECT_EQ(joint.life, 14);
    }

    // With life 20 the speed does not grow; without an owner (0x53b18f) it neither turns nor draws rand(): only the
    // common movement.
    OrbitWorld steady;
    Client::Effects::ModelEffect effect{};
    ASSERT_TRUE(steady.Create(0x60f, 0xe, &effect, 40.0f, JointOwnerKind::ModelEffect).HasValue());
    ASSERT_TRUE(steady.Step().HasValue());
    EXPECT_EQ(steady.joints[0].speed, 30.0f);
    OrbitWorld ownerless;
    ASSERT_TRUE(ownerless.Create(0x60f, 0xe, nullptr, 40.0f).HasValue());
    ownerless.joints[0].angles = glm::vec3(0.0f);
    const glm::vec3 before = ownerless.joints[0].position;
    const CrtRandom drawn = ownerless.random;
    ASSERT_TRUE(ownerless.Step().HasValue());
    EXPECT_EQ(ownerless.random.State(), drawn.State());
    EXPECT_EQ(ownerless.joints[0].position, before - glm::vec3(0.0f, 30.0f, 0.0f));
    EXPECT_EQ(ownerless.joints[0].width, 40.0f);
}

// 0x533a28..0x533a40 and 0x5340fe..0x53412d: the /0x13 (the columns of circle 0xc3, 0x5236ce, with the 0x62b, no owner
// and width 40) is born like the drop but with vz negated and without texture 0x62d. 0x53a12c: with life over 25 it
// stays; with 25 or less, vz − 5 and z + vz (the sum unrounded), x and y at the target.
TEST(OrbitJointsTest, TheSinkingDropFallsWithItsNegatedSpeed)
{
    OrbitWorld tested;
    ASSERT_TRUE(tested.Create(0x62b, 0x13, nullptr, 40.0f).HasValue());
    CrtRandom replay{Seed};
    const auto fall = static_cast<float>(-(replay.Next() % 20 + 35));
    const std::int32_t life = replay.Next() % 50 + 25;
    EXPECT_EQ(tested.random.State(), replay.State());
    JointState& joint = tested.joints[0];
    EXPECT_EQ(joint.texture, 0x62b);
    EXPECT_EQ(joint.velocity.z, fall);
    EXPECT_EQ(joint.life, life);
    EXPECT_EQ(joint.width, 40.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.targetPosition, tested.second);

    joint.life = 26;
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(joint.position, tested.start);
    EXPECT_EQ(joint.velocity.z, fall);

    joint.velocity.z = -35.0f;
    ASSERT_TRUE(tested.Step().HasValue());
    EXPECT_EQ(joint.velocity.z, -40.0f);
    EXPECT_EQ(joint.position, (glm::vec3{1010.0f, 2020.0f, 60.0f}));
    EXPECT_EQ(joint.life, 24);

    // The 0x62b/2 is also the usual drop (positive vz).
    OrbitWorld drop;
    ASSERT_TRUE(drop.Create(0x62b, 2, nullptr, 40.0f).HasValue());
    CrtRandom dropReplay{Seed};
    EXPECT_EQ(drop.joints[0].velocity.z, static_cast<float>(dropReplay.Next() % 20 + 35));
}
