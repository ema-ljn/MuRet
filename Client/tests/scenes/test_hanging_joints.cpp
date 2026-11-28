#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// The joints created by what hangs from a player (0x509340 and the hand loop of 0x50ba30): sword 0x224's 614/10, and
// staff 0xc1e's 618/0x11 and sword 0x223's 610/4, owned by the character. The expected values repeat the ASM formulas
// of 0x530860 and 0x535840 with another CrtRandom of the same seed.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::JointRequest;
using Client::Kernel::JointState;

using Client::Math::D;
using Client::Math::F32;

// 0x530860 with the 610/4: the request's angles do not count, the case sets them to 0.
Client::Core::Expected<std::optional<std::size_t>, std::string> CreateSwordTrail(std::span<JointState> joints,
                                                                                 glm::vec3 first, glm::vec3 second,
                                                                                 void* owner, float width,
                                                                                 std::int32_t key, std::int32_t skill)
{
    glm::vec3 angles{0.0f, 0.0f, 45.0f};
    CrtRandom random(9);
    const JointRequest request{0x610, first, second, angles, 4, owner, width, key, skill, 0, -1};
    auto created = Client::Joints::CreateFaunaJoint(joints, request, random);
    EXPECT_EQ(random.State(), CrtRandom(9).State());
    return created;
}

// With angles at 0, the third point of a segment's quad is (x; y; z − width / 2).
glm::vec3 ThirdCorner(const glm::vec3& at, float width)
{
    return {at.x, at.y, F32(D(at.z) - D(F32(D(width) * 0.5)))};
}

// 0x5309d6 and 0x531691..0x53180e: the ownerless 614/10. Speed 50, the width, the repeated texture, life 0, angles at
// 0, light (0.3; 0.3; 1) and 10 points. Nine segments shifted by (rand() & 7) − 4 in x and y from a tenth of what is
// left, each undone after appending it; at the end, the position is the second point.
TEST(HangingJointsTest, TheSwordBeamWalksNineJitteredSamplesTowardItsTarget)
{
    std::array<JointState, 1> joints{};
    glm::vec3 first{1000.0f, 2000.0f, 150.0f};
    glm::vec3 second{1003.5f, 1887.25f, 142.0f};
    glm::vec3 angles{0.0f, 0.0f, 120.0f};
    constexpr float Width = 27.5f;
    CrtRandom random(23);
    const JointRequest request{0x614, first, second, angles, 10, nullptr, Width, -1, 0, 0, -1};

    const auto created = Client::Joints::CreateFaunaJoint(joints, request, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    const JointState& joint = joints[0];
    CrtRandom draws(23);
    const glm::vec3 step{F32((D(second.x) - D(first.x)) * D(0.1f)), F32((D(second.y) - D(first.y)) * D(0.1f)),
                         F32((D(second.z) - D(first.z)) * D(0.1f))};
    std::vector<glm::vec3> appended;
    glm::vec3 p = first;
    for (int sample = 0; sample < 9; ++sample)
    {
        const float dx = F32(static_cast<double>(draws.Next() & 7) + D(step.x) - 4.0);
        const float dy = F32(static_cast<double>(draws.Next() & 7) + D(step.y) - 4.0);
        p = {F32(D(dx) + D(p.x)), F32(D(dy) + D(p.y)), F32(D(step.z) + D(p.z))};
        appended.push_back(p);
        p.x = F32(D(p.x) - D(dx) + D(step.x));
        p.y = F32(D(p.y) - D(dy) + D(step.y));
    }
    EXPECT_EQ(random.State(), draws.State());
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.texture, 0x614);
    EXPECT_EQ(joint.blend, 1);
    EXPECT_EQ(joint.width, Width);
    EXPECT_EQ(joint.speed, 50.0f);
    EXPECT_TRUE(joint.repeatedTexture);
    EXPECT_EQ(joint.life, 0);
    EXPECT_EQ(joint.angles, glm::vec3(0.0f));
    EXPECT_EQ(joint.light, glm::vec3(0.3f, 0.3f, 1.0f));
    EXPECT_EQ(joint.maximumCount, 10);
    EXPECT_EQ(joint.pathIncrement, step);
    EXPECT_EQ(joint.targetPosition, second);
    EXPECT_EQ(joint.position, second);
    EXPECT_EQ(joint.ownerContext, nullptr);
    ASSERT_EQ(joint.count, 9);
    for (std::size_t n = 0; n < appended.size(); ++n)
    {
        SCOPED_TRACE(n);
        EXPECT_EQ(joint.points[n][2], ThirdCorner(appended[appended.size() - 1 - n], Width));
    }
}

// 0x53c478: the 614/10 shares entry 8 (0x536b24) and the whole step of 0x535840 with the 614/14: two joints equal
// except for the subtype end up equal, with the same rand().
TEST(HangingJointsTest, TheSwordBeamStepsLikeTheFlareBeam)
{
    for (const float width : {27.5f, 50.0f})
    {
        SCOPED_TRACE(width);
        std::array<JointState, 1> sword{};
        glm::vec3 first{1000.0f, 2000.0f, 150.0f};
        glm::vec3 second{1003.5f, 1887.25f, 142.0f};
        glm::vec3 angles{};
        CrtRandom random(41);
        const JointRequest request{0x614, first, second, angles, 10, nullptr, width, -1, 0, 0, -1};
        ASSERT_TRUE(Client::Joints::CreateFaunaJoint(sword, request, random).HasValue());
        std::array<JointState, 1> flare = sword;
        flare[0].subtype = 14;
        CrtRandom other = random;
        Client::Particles::Particles swordParticles;
        Client::Particles::Particles flareParticles;
        Client::Particles::FrameSprites sprites;

        const auto stepped = Client::Joints::StepJoints(sword, random, nullptr, &swordParticles, 0.0f, &sprites);
        const auto reference = Client::Joints::StepJoints(flare, other, nullptr, &flareParticles, 0.0f, &sprites);

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        ASSERT_TRUE(reference.HasValue()) << reference.Error();
        EXPECT_EQ(random.State(), other.State());
        EXPECT_FALSE(sword[0].alive);
        EXPECT_EQ(sword[0].life, flare[0].life);
        EXPECT_EQ(sword[0].position, flare[0].position);
        EXPECT_EQ(sword[0].velocity, flare[0].velocity);
        EXPECT_EQ(sword[0].count, flare[0].count);
        EXPECT_EQ(sword[0].points, flare[0].points);
        EXPECT_EQ(swordParticles.Any(), flareParticles.Any());
    }
}
// 0x532125, 0x5321e0 and 0x5323ba: the 618/0x11 takes the width, speed 0, life 12, the position of the character's
// +0x40 and light (0.8; 0.2; 1), without rand(). It does not write +0x6c: the slot's previous use's stays, and 0x534cb6
// lowers it to 50.
TEST(HangingJointsTest, TheStaffBeamStartsOnTheCharactersPointWithTheRetainedPoints)
{
    Client::Kernel::ActorState actor;
    actor.alive = true;
    actor.effectOrigin = {420.0f, 610.5f, 88.25f};
    for (const std::int32_t retained : {0, 20, 64})
    {
        SCOPED_TRACE(retained);
        std::array<JointState, 1> joints{};
        joints[0].maximumCount = retained;
        glm::vec3 point{1.0f, 2.0f, 3.0f};
        glm::vec3 angles{0.0f, 0.0f, 45.0f};
        CrtRandom random(5);
        const JointRequest request{0x618, point, point, angles, 0x11, &actor, 30.0f, -1, 0, 0, -1};

        const auto created = Client::Joints::CreateFaunaJoint(joints, request, random);

        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(random.State(), CrtRandom(5).State());
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.texture, 0x618);
        EXPECT_EQ(joint.width, 30.0f);
        EXPECT_EQ(joint.speed, 0.0f);
        EXPECT_EQ(joint.life, 12);
        EXPECT_EQ(joint.position, actor.effectOrigin);
        EXPECT_EQ(joint.angles, angles);
        EXPECT_EQ(joint.light, glm::vec3(0.8f, 0.2f, 1.0f));
        EXPECT_EQ(joint.ownerContext, &actor);
        EXPECT_EQ(joint.maximumCount, retained > 50 ? 50 : retained);
        EXPECT_TRUE(joint.tailEnabled);
    }
}

// 0x53820e..0x53823d: with the character alive, the 618/0x11 returns to its +0x40 and goes on with the common tail and
// life; with the character dead it turns off without tail or life.
TEST(HangingJointsTest, TheStaffBeamFollowsTheCharacterAndDiesWithIt)
{
    Client::Kernel::ActorState actor;
    actor.alive = true;
    actor.effectOrigin = {420.0f, 610.5f, 88.25f};
    std::array<JointState, 1> joints{};
    joints[0].maximumCount = 20;
    glm::vec3 point{};
    glm::vec3 angles{};
    CrtRandom random(5);
    const JointRequest request{0x618, point, point, angles, 0x11, &actor, 30.0f, -1, 0, 0, -1};
    ASSERT_TRUE(Client::Joints::CreateFaunaJoint(joints, request, random).HasValue());
    actor.effectOrigin = {430.0f, 600.0f, 90.0f};

    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

    const JointState& joint = joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.position, actor.effectOrigin);
    EXPECT_EQ(joint.life, 11);
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.points[0][2], ThirdCorner(actor.effectOrigin, 30.0f));
    EXPECT_EQ(random.State(), CrtRandom(5).State());

    actor.alive = false;
    actor.effectOrigin = {};
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, 11);
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.position, glm::vec3(430.0f, 600.0f, 90.0f));
}

// 0x531c43..0x531dcb and 0x532003: sword 0x223's 610/4 owned by the character. The key at 0, blend 3, angles at 0, life
// 0, the width, light at 1, 10 points and ten segments with a tenth of what is left; the target is the second point. No
// rand(). The quad of the retained width (0x530afa) drops out of the tail.
TEST(HangingJointsTest, TheSwordTrailLaysTenStepsTowardItsSecondPoint)
{
    Client::Kernel::ActorState actor;
    actor.alive = true;
    std::array<JointState, 1> joints{};
    joints[0].width = 7.0f;
    const glm::vec3 first{1000.0f, 2000.0f, 150.0f};
    const glm::vec3 second{1003.5f, 1887.25f, 142.0f};
    constexpr float Width = 21.5f;

    const auto created = CreateSwordTrail(joints, first, second, &actor, Width, 77, 0);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    const JointState& joint = joints[0];
    const glm::vec3 step{F32((D(second.x) - D(first.x)) * D(0.1f)), F32((D(second.y) - D(first.y)) * D(0.1f)),
                         F32((D(second.z) - D(first.z)) * D(0.1f))};
    std::vector<glm::vec3> appended;
    glm::vec3 p = first;
    for (int sample = 0; sample < 10; ++sample)
    {
        p = {F32(D(p.x) + D(step.x)), F32(D(p.y) + D(step.y)), F32(D(p.z) + D(step.z))};
        appended.push_back(p);
    }
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.texture, 0x610);
    EXPECT_EQ(joint.key, 0);
    EXPECT_EQ(joint.blend, 3);
    EXPECT_EQ(joint.angles, glm::vec3(0.0f));
    EXPECT_EQ(joint.life, 0);
    EXPECT_EQ(joint.width, Width);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.maximumCount, 10);
    EXPECT_EQ(joint.pathIncrement, step);
    EXPECT_EQ(joint.position, appended.back());
    EXPECT_EQ(joint.targetPosition, second);
    EXPECT_EQ(joint.ownerContext, &actor);
    ASSERT_EQ(joint.count, 9);
    for (std::size_t n = 0; n < appended.size(); ++n)
    {
        SCOPED_TRACE(n);
        EXPECT_EQ(joint.points[n][2], ThirdCorner(appended[appended.size() - 1 - n], Width));
    }
}

// 0x537d55..0x537ebc and 0x53c393: with the character alive, the target starts at its +0x17c and moves three times a
// third of what is left to its position; the position follows it. In between it appends the tail only if it is not
// full or there is a skill; with life 0 the light drops to two thirds, the common tail appends once more and life −1
// turns it off.
TEST(HangingJointsTest, TheSwordTrailWalksFromTheTrailOriginToTheCharacterAndDies)
{
    struct Case
    {
        std::int32_t skill;
        std::int32_t count;
    };
    for (const Case probe : {Case{0, 9}, Case{0, 7}, Case{1, 9}})
    {
        SCOPED_TRACE(::testing::Message() << "skill " << probe.skill << ", cola " << probe.count);
        Client::Kernel::ActorState actor;
        actor.alive = true;
        actor.position = {420.0f, 610.5f, 88.25f};
        actor.trailOrigin = {400.25f, 650.0f, 80.0f};
        std::array<JointState, 1> joints{};
        constexpr float Width = 21.5f;
        ASSERT_TRUE(CreateSwordTrail(joints, {1000.0f, 2000.0f, 150.0f}, {1003.5f, 1887.25f, 142.0f}, &actor, Width, -1,
                                     probe.skill)
                        .HasValue());
        JointState& joint = joints[0];
        joint.count = probe.count;
        std::array<glm::vec3, 50> corners{};
        for (std::size_t n = 0; n < corners.size(); ++n)
            corners[n] = joint.points[n][2];
        std::int32_t count = probe.count;
        const auto append = [&](const glm::vec3& at)
        {
            count = std::min(count + 1, 9);
            for (auto n = static_cast<std::size_t>(count); n > 0; --n)
                corners[n] = corners[n - 1];
            corners[0] = ThirdCorner(at, Width);
        };
        const double third = D(std::bit_cast<float>(0x3eaaaaabu));
        glm::vec3 delta{};
        for (int axis = 0; axis < 3; ++axis)
            delta[axis] = F32(D(F32(D(actor.position[axis]) - D(actor.trailOrigin[axis]))) * third);
        glm::vec3 target = actor.trailOrigin;
        for (int step = 0; step < 3; ++step)
        {
            target = {F32(D(delta.x) + D(target.x)), F32(D(delta.y) + D(target.y)), F32(D(delta.z) + D(target.z))};
            if (count < 9 || probe.skill != 0)
                append(target);
        }
        append(target);
        CrtRandom random(5);

        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

        EXPECT_EQ(random.State(), CrtRandom(5).State());
        EXPECT_FALSE(joint.alive);
        EXPECT_EQ(joint.life, -1);
        EXPECT_EQ(joint.targetPosition, target);
        EXPECT_EQ(joint.position, target);
        EXPECT_EQ(joint.light, glm::vec3(F32(D(1.0f) * D(std::bit_cast<float>(0x3f2aaaabu)))));
        ASSERT_EQ(joint.count, count);
        for (std::size_t n = 0; n <= static_cast<std::size_t>(count); ++n)
        {
            SCOPED_TRACE(n);
            EXPECT_EQ(joint.points[n][2], corners[n]);
        }
    }
}

// 0x537ec1..0x537ee1: with the character dead, life −1 and the position 50 above the target, without touching the
// light; the common tail appends it and the life turns it off. Without an owner, creation works and the step stops (the
// original reads +0x54 without checking).
TEST(HangingJointsTest, TheSwordTrailOfADeadCharacterRisesFromItsTargetAndAnOwnerlessOneStops)
{
    Client::Kernel::ActorState actor;
    actor.alive = true;
    const glm::vec3 second{1003.5f, 1887.25f, 142.3f};
    std::array<JointState, 1> joints{};
    ASSERT_TRUE(CreateSwordTrail(joints, {1000.0f, 2000.0f, 150.0f}, second, &actor, 21.5f, -1, 0).HasValue());
    actor.alive = false;
    CrtRandom random(5);

    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

    const JointState& joint = joints[0];
    const glm::vec3 risen{second.x, second.y, F32(D(second.z) + 50.0)};
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -2);
    EXPECT_EQ(joint.position, risen);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.count, 9);
    EXPECT_EQ(joint.points[0][2], ThirdCorner(risen, 21.5f));

    std::array<JointState, 1> ownerless{};
    ASSERT_TRUE(CreateSwordTrail(ownerless, {1000.0f, 2000.0f, 150.0f}, second, nullptr, 21.5f, -1, 0).HasValue());
    EXPECT_EQ(ownerless[0].targetPosition, second);
    const auto stepped = Client::Joints::StepJoints(ownerless, random);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x537d49"), std::string::npos) << stepped.Error();
}
} // namespace
