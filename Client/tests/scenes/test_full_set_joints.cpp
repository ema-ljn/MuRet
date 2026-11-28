#include "Joints/FaunaJointCreation.h"
#include "Kernel/HelperTable.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRetainedMath.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Pets/HelperTable.h"
#include "Joints/JointKernels.h"
#include "Kernel/JointRequest.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

// The full set's joints (0x5102c3..0x5104cb) and the 5ac/3's (0x52aa5d), owned by the character: the 60f/0 and
// 60f/0x12 with their fall to the 60f/1, the 610/7 and the 619/9. The expected values repeat the ASM formulas of
// 0x530860 and 0x535840 with another CrtRandom of the same seed.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::HelperState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointRequest;
using Client::Kernel::JointState;
using Client::Math::Detail::CosineProductPC53;
using Client::Math::Detail::SineProductPC53;

using Client::Math::D;
using Client::Math::F32;

float Float(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

glm::vec3 Faded(const glm::vec3& light, std::uint32_t factorBits)
{
    const double factor = D(Float(factorBits));
    return {F32(D(light.x) * factor), F32(D(light.y) * factor), F32(D(light.z) * factor)};
}

double Cosine(double argument, float multiplier)
{
    const std::optional<double> product = CosineProductPC53(argument, multiplier);
    EXPECT_TRUE(product.has_value());
    return product.value_or(0.0);
}

double Sine(double argument, float multiplier)
{
    const std::optional<double> product = SineProductPC53(argument, multiplier);
    EXPECT_TRUE(product.has_value());
    return product.value_or(0.0);
}

struct Request
{
    std::int32_t type;
    std::int32_t subtype;
    float width;
    std::int32_t key;
    std::int32_t skill;
    glm::vec3 first{100.0f, 200.0f, 300.0f};
    glm::vec3 second{110.0f, 190.0f, 305.0f};
    glm::vec3 angles{};
};

Client::Core::Expected<std::optional<std::size_t>, std::string>
Create(std::span<JointState> joints, Request request, void* owner, CrtRandom& random,
       JointOwnerKind ownerKind = JointOwnerKind::ByType)
{
    const JointRequest made{request.type,
                            request.first,
                            request.second,
                            request.angles,
                            request.subtype,
                            owner,
                            request.width,
                            request.key,
                            request.skill,
                            0,
                            -1};
    return Client::Joints::CreateFaunaJoint(joints, made, random, ownerKind);
}

// The common tail (0x53c393) with angles at 0: count rises up to maximumCount − 1.
std::int32_t Appended(std::int32_t count, std::int32_t maximum)
{
    return std::min(count + 1, maximum - 1);
}

// What a 60f/0 or 60f/0x12 does in one 0x53b239 step with the key at −1, without the fall.
void TurnSparkle(JointState& expected)
{
    const double tenth = D(Float(0x3dcccccdu));
    double argument = 0;
    if (expected.subtype == 0x12)
    {
        argument = (static_cast<double>(expected.life * 2) + D(expected.velocity.y)) * tenth;
        if (expected.skill == 1)
            argument = argument * -1.0;
    }
    else
        argument = (static_cast<double>(expected.life) + D(expected.velocity.y)) * tenth;
    const double cosine = Cosine(argument, expected.speed);
    const double sine = Sine(argument, expected.speed);
    expected.position.x = F32(cosine + D(expected.targetPosition.x));
    expected.position.y = F32(D(expected.targetPosition.y) - sine);
    if (expected.subtype == 0x12)
    {
        expected.position.z = F32(D(expected.velocity.z) * D(Float(0x3f8ccccdu)) + D(expected.position.z));
        expected.speed = F32(D(expected.speed) + tenth);
        expected.width = F32(D(expected.width) + D(Float(0x3e4ccccdu)));
    }
    else
        expected.position.z = F32(D(expected.velocity.z) + D(expected.position.z));
    if (expected.life < 10)
        expected.light = Faded(expected.light, 0x3f44ec4fu);
}
} // namespace

// 0x533677 and 0x5341fc..0x5342d2: the 60f/0 of width 10 (0x51037c): life 100, 20 points, speed 40, vz (rand() % 150)
// · 0.01f, vy rand() % 500 − 250, the character's light and the target at the second point. Two rand().
TEST(FullSetJointsTest, TheSparkleIsBornWithTheLightOfItsOwnerAndTwoRandoms)
{
    ActorState actor;
    actor.light = {0.25f, 0.5f, 0.75f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    const Request request{0x60f, 0, 10.0f, -1, 0};

    const auto created = Create(joints, request, &actor, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), std::optional<std::size_t>(0));
    CrtRandom draws(1);
    const int rise = draws.Next();
    const int drift = draws.Next();
    EXPECT_EQ(random.State(), draws.State());
    const JointState& joint = joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.type, 0x60f);
    EXPECT_EQ(joint.subtype, 0);
    EXPECT_EQ(joint.texture, 0x60f);
    EXPECT_EQ(joint.blend, 1);
    EXPECT_EQ(joint.life, 100);
    EXPECT_EQ(joint.maximumCount, 20);
    EXPECT_EQ(joint.width, 10.0f);
    EXPECT_EQ(joint.speed, 40.0f);
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f, static_cast<float>(drift % 500 - 250),
                                        F32(static_cast<double>(rise % 150) * D(Float(0x3c23d70au)))));
    EXPECT_EQ(joint.light, actor.light);
    EXPECT_EQ(joint.position, request.first);
    EXPECT_EQ(joint.targetPosition, request.second);
    EXPECT_EQ(joint.ownerContext, &actor);
    EXPECT_EQ(joint.key, -1);
    EXPECT_TRUE(joint.tailEnabled);
}

// Without the character, the 60f/0 cannot read its light (0x53429b).
TEST(FullSetJointsTest, TheSparkleNeedsItsOwner)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);

    const auto created = Create(joints, {0x60f, 0x12, 20.0f, -1, 0}, nullptr, random);

    ASSERT_FALSE(created.HasValue());
    EXPECT_NE(created.Error().find("0x53429b"), std::string::npos) << created.Error();
}

// 0x53b239..0x53b3d1: the 60f/0 turns at 40 around its target with (life + vy) · 0.1f, rises by vz and, with life below
// 10, the light times 0.7692308f; the common tail appends it and it dies at step 101. No rand().
TEST(FullSetJointsTest, TheSparkleTurnsAroundItsTargetUntilItsLifeRunsOut)
{
    ActorState actor;
    actor.light = {0.25f, 0.5f, 0.75f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    ASSERT_TRUE(Create(joints, {0x60f, 0, 10.0f, -1, 0}, &actor, random).HasValue());
    JointState expected = joints[0];
    const std::uint32_t state = random.State();

    for (int step = 1; step <= 101; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
        TurnSparkle(expected);
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.subtype, 0);
        EXPECT_EQ(joint.alive, step < 101);
    }
    EXPECT_EQ(random.State(), state);
}

// 0x53b378..0x53b3a9: with the width below 40 and the owner's action in [0xd, 0x21] or [0x38, 0x3c], after turning, it
// becomes the 60f/1 without tail and with life at most 10. The 60f/1 does not move (0x5358bc), only fades with life
// below 10, and dies when its life runs out (Q-C383).
TEST(FullSetJointsTest, TheSparkleFallsWithTheActionsOfItsOwnerAndThenOnlyFades)
{
    struct Case
    {
        std::uint8_t action;
        bool falls;
    };
    constexpr std::array cases{Case{0xc, false},  Case{0xd, true},  Case{0x21, true}, Case{0x22, false},
                               Case{0x37, false}, Case{0x38, true}, Case{0x3c, true}, Case{0x3d, false}};
    for (const Case& probe : cases)
    {
        SCOPED_TRACE(static_cast<int>(probe.action));
        ActorState actor;
        actor.light = glm::vec3(1.0f);
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        ASSERT_TRUE(Create(joints, {0x60f, 0, 10.0f, -1, 0}, &actor, random).HasValue());
        JointState expected = joints[0];
        for (int step = 0; step < 3; ++step)
        {
            ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
            TurnSparkle(expected);
            expected.count = Appended(expected.count, expected.maximumCount);
            --expected.life;
        }

        actor.animation.action = probe.action;
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
        TurnSparkle(expected);
        JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        if (!probe.falls)
        {
            EXPECT_EQ(joint.subtype, 0);
            EXPECT_TRUE(joint.tailEnabled);
            EXPECT_EQ(joint.count, Appended(expected.count, expected.maximumCount));
            EXPECT_EQ(joint.life, expected.life - 1);
            continue;
        }
        EXPECT_EQ(joint.subtype, 1);
        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, 9);

        glm::vec3 light = joint.light;
        for (int step = 1; step <= 10; ++step)
        {
            SCOPED_TRACE(step);
            ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
            light = Faded(light, 0x3f44ec4fu);
            EXPECT_EQ(joint.position, expected.position);
            EXPECT_EQ(joint.light, light);
            EXPECT_EQ(joint.count, expected.count);
            EXPECT_EQ(joint.life, 9 - step);
            EXPECT_EQ(joint.alive, step < 10);
        }
    }
}

// 0x444740: the dark horse's 60f/0 has its HelperState as owner, and 0x53429b reads its own light from it (+0xf4, the
// one 0x443d70 sets to 3), not the character's. The rest is created as with the character.
TEST(FullSetJointsTest, TheSparkleOfTheHorseIsBornWithTheLightOfTheHorse)
{
    HelperState horse;
    horse.light = {3.0f, 2.0f, 1.0f};
    horse.horse.animation.action = 0xd;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    const Request request{0x60f, 0, 40.0f, 2, 0};

    const auto created = Create(joints, request, &horse, random, JointOwnerKind::Helper);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), std::optional<std::size_t>(0));
    CrtRandom draws(1);
    static_cast<void>(draws.Next());
    const int drift = draws.Next();
    const int rise = draws.Next();
    EXPECT_EQ(random.State(), draws.State());
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.ownerContext, &horse);
    EXPECT_EQ(joint.ownerKind, JointOwnerKind::Helper);
    EXPECT_EQ(joint.light, horse.light);
    EXPECT_EQ(joint.life, 50);
    EXPECT_EQ(joint.key, 2);
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f, static_cast<float>(drift % 500 - 250),
                                        F32(static_cast<double>(rise % 250 + 200) * D(Float(0x3c23d70au)))));
    EXPECT_EQ(joint.targetPosition, request.second);

    // 0x530860 writes the owner kind at each creation: the slot goes back to ByType with the character.
    joints[0].alive = false;
    ActorState actor;
    actor.light = glm::vec3(0.5f);
    ASSERT_TRUE(Create(joints, {0x60f, 0, 10.0f, -1, 0}, &actor, random).HasValue());
    EXPECT_EQ(joints[0].ownerKind, JointOwnerKind::ByType);
    EXPECT_EQ(joints[0].light, actor.light);
}

// 0x53b378..0x53b3a9 with the horse as owner: below width 40 the fall looks at the horse's action (+0x111,
// DarkHorse's), not the character's.
TEST(FullSetJointsTest, TheSparkleOfTheHorseFallsWithTheActionOfTheHorse)
{
    for (const std::uint8_t action : {std::uint8_t{0xc}, std::uint8_t{0xd}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        HelperState horse;
        horse.light = glm::vec3(3.0f);
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        ASSERT_TRUE(Create(joints, {0x60f, 0, 10.0f, -1, 0}, &horse, random, JointOwnerKind::Helper).HasValue());
        JointState expected = joints[0];
        horse.horse.animation.action = action;

        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

        TurnSparkle(expected);
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.subtype, action == 0xd ? 1 : 0);
        EXPECT_EQ(joint.tailEnabled, action != 0xd);
        EXPECT_EQ(joint.life, action == 0xd ? 9 : 99);
    }
}

// MuRet can only read a helper owner in the 60f/0 and 60f/0x12: with another joint it stops before writing the slot and
// without rand(); with the pool full it does not read the arguments.
TEST(FullSetJointsTest, OnlyTheSparklesTakeAHelperForOwner)
{
    HelperState horse;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);

    const auto created = Create(joints, {0x610, 7, 10.0f, -1, 0}, &horse, random, JointOwnerKind::Helper);

    ASSERT_FALSE(created.HasValue());
    EXPECT_NE(created.Error().find("0x530860"), std::string::npos) << created.Error();
    EXPECT_FALSE(joints[0].alive);
    EXPECT_EQ(random.State(), CrtRandom(1).State());

    joints[0].alive = true;
    const auto full = Create(joints, {0x610, 7, 10.0f, -1, 0}, &horse, random, JointOwnerKind::Helper);
    ASSERT_TRUE(full.HasValue()) << full.Error();
    EXPECT_FALSE(full.Value().has_value());
}

// 0x53c393: a 60f/1 with the tail uses the matrix that the last joint of the loop that computed it left in [ebp−0xe8]
// (the falling 60f/0 turns it off, so it is only reached with a hand-built state). With none before, the one on the
// stack: with drawing it stops (Q-C1623).
TEST(FullSetJointsTest, AFallenSparkleWithItsTailUsesThePreviousMatrix)
{
    std::array<JointState, 2> joints{};
    joints[1].alive = true;
    joints[1].type = 0x60f;
    joints[1].subtype = 1;
    joints[1].tailEnabled = true;
    joints[1].drawEnabled = 3;
    joints[1].width = 20.0f;
    joints[1].maximumCount = 5;
    joints[1].life = 30;
    joints[1].position = {10.0f, 20.0f, 30.0f};
    CrtRandom random(1);
    auto alone = joints;
    const auto stepped = Client::Joints::StepJoints(alone, random);
    ASSERT_FALSE(stepped.HasValue());
    EXPECT_NE(stepped.Error().find("0x53c3a5"), std::string::npos) << stepped.Error();

    // The earlier 0x613/0xa computes the matrix with its angles from before the case (0, 0, 90).
    joints[0].alive = true;
    joints[0].type = 0x613;
    joints[0].subtype = 0xa;
    joints[0].angles = {0.0f, 0.0f, 90.0f};
    joints[0].life = 10;
    JointState expected = joints[1];
    ASSERT_TRUE(Client::Joints::JointKernels::AppendJointTail(
                    expected, Client::Math::OriginalAngleMatrix(glm::vec3{0.0f, 0.0f, 90.0f}))
                    .HasValue());
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    EXPECT_EQ(joints[1].count, expected.count);
    EXPECT_EQ(joints[1].points[0], expected.points[0]);
    EXPECT_EQ(joints[1].position, (glm::vec3{10.0f, 20.0f, 30.0f}));
}

// 0x534cb6: the 60f/0x12 (0x5103be) subtract (blend 4) and, with width 20, live 50 with a third rand() for vz:
// (rand() % 250 + 200) · 0.01f. They turn with doubled life, skill 1 reversed, rise by vz · 1.1f and grow the speed
// 0.1f and the width 0.2f per step.
TEST(FullSetJointsTest, TheWideSparklesTurnBothWaysAndGrow)
{
    for (const std::int32_t skill : {0, 1})
    {
        SCOPED_TRACE(skill);
        ActorState actor;
        actor.light = glm::vec3(1.0f);
        std::array<JointState, 1> joints{};
        CrtRandom random(1);

        ASSERT_TRUE(Create(joints, {0x60f, 0x12, 20.0f, -1, skill}, &actor, random).HasValue());

        CrtRandom draws(1);
        const int rise = draws.Next();
        const int drift = draws.Next();
        const int wideRise = draws.Next();
        static_cast<void>(rise);
        EXPECT_EQ(random.State(), draws.State());
        JointState expected = joints[0];
        EXPECT_EQ(expected.blend, 4);
        EXPECT_EQ(expected.life, 50);
        EXPECT_EQ(expected.width, 20.0f);
        EXPECT_EQ(expected.skill, static_cast<std::uint8_t>(skill));
        EXPECT_EQ(expected.velocity, glm::vec3(0.0f, static_cast<float>(drift % 500 - 250),
                                               F32(static_cast<double>(wideRise % 250 + 200) * D(Float(0x3c23d70au)))));

        for (int step = 1; step <= 51; ++step)
        {
            SCOPED_TRACE(step);
            ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
            TurnSparkle(expected);
            expected.count = Appended(expected.count, expected.maximumCount);
            --expected.life;
            const JointState& joint = joints[0];
            EXPECT_EQ(joint.position, expected.position);
            EXPECT_EQ(joint.speed, expected.speed);
            EXPECT_EQ(joint.width, expected.width);
            EXPECT_EQ(joint.light, expected.light);
            EXPECT_EQ(joint.life, expected.life);
            EXPECT_EQ(joint.alive, step < 51);
        }
        EXPECT_EQ(random.State(), draws.State());
    }
}

// 0x53b378: with the width at 40 or more, the owner's action does not make it fall.
TEST(FullSetJointsTest, AWideSparkleOfFortyDoesNotFall)
{
    ActorState actor;
    actor.animation.action = 0xd;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    ASSERT_TRUE(Create(joints, {0x60f, 0x12, 40.0f, -1, 0}, &actor, random).HasValue());

    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

    EXPECT_EQ(joints[0].subtype, 0x12);
    EXPECT_TRUE(joints[0].tailEnabled);
    EXPECT_EQ(joints[0].life, 49);
}

// 0x531c43..0x531ec7: the 610/7 at +13 (0x5104a8): life to the key (0x28), light at 1, vy and vz at 0, blend 4, speed
// 10, texture 0x60f, the width, 16 points and vx 15. x and y shift 40 from the second point with the cosine and sine of
// rand() % 360 taken as radians (Q-C384); the start (+0x24) is that position.
TEST(FullSetJointsTest, TheSetTrailIsBornFortyAwayFromItsSecondPoint)
{
    ActorState actor;
    actor.light = {0.25f, 0.5f, 0.75f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    const Request request{0x610, 7, 20.0f, 0x28, 1};

    const auto created = Create(joints, request, &actor, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    CrtRandom draws(1);
    const int across = draws.Next() % 360;
    const int along = draws.Next() % 360;
    EXPECT_EQ(random.State(), draws.State());
    const JointState& joint = joints[0];
    const glm::vec3 at{F32(Cosine(static_cast<double>(across), 40.0f) + D(request.second.x)),
                       F32(D(request.second.y) - Sine(static_cast<double>(along), 40.0f)), request.first.z};
    EXPECT_EQ(joint.position, at);
    EXPECT_EQ(joint.pathIncrement, at);
    EXPECT_EQ(joint.targetPosition, request.second);
    EXPECT_EQ(joint.life, 0x28);
    EXPECT_EQ(joint.key, 0);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.velocity, glm::vec3(15.0f, 0.0f, 0.0f));
    EXPECT_EQ(joint.blend, 4);
    EXPECT_EQ(joint.speed, 10.0f);
    EXPECT_EQ(joint.texture, 0x60f);
    EXPECT_EQ(joint.width, 20.0f);
    EXPECT_EQ(joint.maximumCount, 16);
    EXPECT_EQ(joint.skill, 1);
}

// 0x531c7b: with color 3 and without an owner, the common 0x610 part reads the light of a null.
TEST(FullSetJointsTest, TheSetTrailOfColorThreeNeedsItsOwner)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);

    const auto created = Create(joints, {0x610, 7, 20.0f, 0x28, 3}, nullptr, random);

    ASSERT_FALSE(created.HasValue());
    EXPECT_NE(created.Error().find("0x531c7b"), std::string::npos) << created.Error();
    EXPECT_EQ(random.State(), CrtRandom(1).State());
}

// 0x537c82..0x537d17: the 610/7 sways from its start with sin(life · 0.4f) · vx, rises by the speed (which drops 0.1f,
// like vx) and, with life below 15, the light times 0.8333333f. It dies at step 41. No rand().
TEST(FullSetJointsTest, TheSetTrailSwaysUpwardsUntilItsLifeRunsOut)
{
    ActorState actor;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    ASSERT_TRUE(Create(joints, {0x610, 7, 20.0f, 0x28, 1}, &actor, random).HasValue());
    JointState expected = joints[0];
    const std::uint32_t state = random.State();
    const double tenth = D(Float(0x3dcccccdu));

    for (int step = 1; step <= 41; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
        const float argument = F32(static_cast<double>(expected.life) * D(Float(0x3ecccccdu)));
        expected.position.x = F32(Sine(D(argument), expected.velocity.x) + D(expected.pathIncrement.x));
        expected.position.y = expected.pathIncrement.y;
        expected.position.z = F32(D(expected.speed) + D(expected.position.z));
        expected.speed = F32(D(expected.speed) - tenth);
        expected.velocity.x = F32(D(expected.velocity.x) - tenth);
        if (expected.life < 15)
            expected.light = Faded(expected.light, 0x3f555555u);
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.speed, expected.speed);
        EXPECT_EQ(joint.velocity.x, expected.velocity.x);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.alive, step < 41);
    }
    EXPECT_EQ(random.State(), state);
}

// 0x5323d3 and 0x53254f..0x5325c2: the 5ac/3's 619/9: life 90, 20 points, count 0, +0x9ec at the ftol of the z turn,
// vx 50, light (0.09090909f; 0.04545455f; 0.09090909f) and the position and target on the character. No rand();
// without the character, it stops (0x5325a2).
TEST(FullSetJointsTest, TheSetEnergyIsBornOnItsOwnerWithTheTurnOfTheFlare)
{
    ActorState actor;
    actor.position = {130.0f, 170.0f, 290.0f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 9, 10.0f, -1, 0};
    request.angles = {0.0f, 0.0f, 213.3f};

    const auto created = Create(joints, request, &actor, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(random.State(), CrtRandom(1).State());
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.life, 90);
    EXPECT_EQ(joint.maximumCount, 20);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.field9ec, 213);
    EXPECT_EQ(joint.velocity, glm::vec3(50.0f, 0.0f, 0.0f));
    EXPECT_EQ(joint.light, glm::vec3(Float(0x3dba2e8cu), Float(0x3d3a2e8cu), Float(0x3dba2e8cu)));
    EXPECT_EQ(joint.position, actor.position);
    EXPECT_EQ(joint.targetPosition, actor.position);
    EXPECT_EQ(joint.width, 10.0f);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.ownerContext, &actor);

    std::array<JointState, 1> ownerless{};
    const auto cut = Create(ownerless, request, nullptr, random);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x5325a2"), std::string::npos) << cut.Error();
}

// 0x5387a9..0x5388e2: the 619/9 follows the character live: shifts the segments from count − 1 to 0 by how much it
// moved, turns at vx from the target with +0x9ec · 0.1f (+0x9ec rises 2), z is the target plus 90 − life plus 10; vx
// drops 1 and at step 51, below 0, stays at 0 with life at 0. The light times 1.25 above 80 and times 0.9090909f below
// 50.
TEST(FullSetJointsTest, TheSetEnergyCirclesItsMovingOwnerUntilItsRadiusRunsOut)
{
    ActorState actor;
    actor.position = {130.0f, 170.0f, 290.0f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 9, 10.0f, -1, 0};
    request.angles = {0.0f, 0.0f, 213.3f};
    ASSERT_TRUE(Create(joints, request, &actor, random).HasValue());
    JointState expected = joints[0];
    const double tenth = D(Float(0x3dcccccdu));

    for (int step = 1; step <= 51; ++step)
    {
        SCOPED_TRACE(step);
        if (step == 12 || step == 30)
            actor.position = {F32(D(actor.position.x) + 3.5), F32(D(actor.position.y) - 2.25),
                              F32(D(actor.position.z) + 1.0)};
        const JointState before = joints[0];
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

        glm::vec3 delta{};
        for (int axis = 0; axis < 3; ++axis)
            delta[axis] = F32(D(expected.targetPosition[axis]) - D(actor.position[axis]));
        expected.targetPosition = actor.position;
        const double argument = static_cast<double>(expected.field9ec) * tenth;
        expected.field9ec += 2;
        const double sine = expected.velocity.x > 0 ? Sine(argument, expected.velocity.x) : 0.0;
        const double cosine = expected.velocity.x > 0 ? Cosine(argument, expected.velocity.x) : 0.0;
        expected.position.x = F32(sine + D(expected.targetPosition.x));
        expected.position.y = F32(cosine + D(expected.targetPosition.y));
        expected.position.z = F32((static_cast<double>(90 - expected.life) + D(expected.targetPosition.z)) + 10.0);
        const double reduced = D(expected.velocity.x) - 1.0;
        expected.velocity.x = F32(reduced);
        if (!(reduced >= 0))
        {
            expected.velocity.x = 0.0f;
            expected.life = 0;
        }
        if (expected.life < 50)
            expected.light = Faded(expected.light, 0x3f68ba2eu);
        else if (expected.life > 80)
            expected.light = Faded(expected.light, 0x3fa00000u);
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;

        const JointState& joint = joints[0];
        EXPECT_EQ(joint.targetPosition, expected.targetPosition);
        EXPECT_EQ(joint.field9ec, expected.field9ec);
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.velocity.x, expected.velocity.x);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.alive, step < 51);
        // The segments from count − 1 to 0 shifted and, with the tail, one slot further back; the last is not shifted.
        for (std::int32_t segment = 0; segment < joint.count; ++segment)
        {
            SCOPED_TRACE(segment);
            glm::vec3 corner = before.points[static_cast<std::size_t>(segment)][2];
            if (segment < before.count)
                for (int axis = 0; axis < 3; ++axis)
                    corner[axis] = F32(D(corner[axis]) - D(delta[axis]));
            EXPECT_EQ(joint.points[static_cast<std::size_t>(segment) + 1][2], corner);
        }
    }
    EXPECT_EQ(random.State(), CrtRandom(1).State());
}

// 0x5325c7..0x532659: the 0x35's 619/0xa: like the /9, with life 80, vx 80 and +0x9d4 at 1 only with turn 225 or 405.
TEST(FullSetJointsTest, TheSkillEnergyTurnsForwardOnlyFromTwoAngles)
{
    for (const float turn : {45.0f, 405.0f, 225.0f, 450.0f})
    {
        SCOPED_TRACE(turn);
        ActorState actor;
        actor.position = {130.0f, 170.0f, 290.0f};
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        Request request{0x619, 0xa, 15.0f, -1, 0};
        request.angles = {-45.0f, 0.0f, turn};

        const auto created = Create(joints, request, &actor, random);

        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(random.State(), CrtRandom(1).State());
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.life, 80);
        EXPECT_EQ(joint.maximumCount, 20);
        EXPECT_EQ(joint.count, 0);
        EXPECT_EQ(joint.field9ec, static_cast<std::int32_t>(turn));
        EXPECT_EQ(joint.field9d4, turn == 225.0f || turn == 405.0f ? 1 : 0);
        EXPECT_EQ(joint.velocity, glm::vec3(80.0f, 0.0f, 0.0f));
        EXPECT_EQ(joint.light, glm::vec3(Float(0x3dba2e8cu), Float(0x3d3a2e8cu), Float(0x3dba2e8cu)));
        EXPECT_EQ(joint.position, actor.position);
        EXPECT_EQ(joint.targetPosition, actor.position);
        EXPECT_EQ(joint.width, 15.0f);
    }
}

// 0x5388e7..0x538a4d: the 619/0xa follows the owner like the /9, with z at the target plus life · 0.0125f · 300, +0x9ec
// which rises 2 with +0x9d4 and otherwise drops 2, and the light times 1.2f above 68 and times 0.9090909f below 40. At
// step 81, vx below 0, it stays at 0 with life at 0. The values, by their bits, come from a computation apart from the
// ASM (FSIN/FCOS at 64 bits, PC53 and fstp to float) at steps 1 and 12 (the light rises), 20 (the owner moves 3.5;
// −2.25 and 1 before the step), 42 (the light drops), 80 (vx at 0) and 81 (life at 0 and dead).
TEST(FullSetJointsTest, TheSkillEnergyCirclesItsOwnerAndClimbsWithItsLife)
{
    struct Checkpoint
    {
        float turn;
        int step;
        std::array<std::uint32_t, 3> position;
        std::uint32_t radius;
        std::array<std::uint32_t, 3> light;
        std::int32_t field9ec;
        std::int32_t life;
    };
    constexpr std::array<Checkpoint, 12> Checkpoints{{
        {45, 1, {0x424f30bbu, 0x431922e7u, 0x44138000u}, 0x429e0000u, {0x3ddf6b0fu, 0x3d5f6b0fu, 0x3ddf6b0fu}, 43, 79},
        {45, 12, {0x43357423u, 0x42f80dcdu, 0x44093000u}, 0x42880000u, {0x3f4f8087u, 0x3ecf8087u, 0x3f4f8087u}, 21, 68},
        {45, 20, {0x432ccc1bu, 0x435667c7u, 0x4401f000u}, 0x42700000u, {0x3f4f8087u, 0x3ecf8087u, 0x3f4f8087u}, 5, 60},
        {45,
         42,
         {0x431a29e2u, 0x4306ac92u, 0x43daa000u},
         0x42180000u,
         {0x3f3ca363u, 0x3ebca363u, 0x3f3ca363u},
         -39,
         38},
        {45, 80, {0x4306743bu, 0x43280cbcu, 0x43936000u}, 0, {0x3ca161fdu, 0x3c2161fdu, 0x3ca161fdu}, -115, 0},
        {45, 81, {0x43058000u, 0x4327c000u, 0x43918000u}, 0, {0x3c92b62bu, 0x3c12b62bu, 0x3c92b62bu}, -117, -1},
        {225,
         1,
         {0x42b60d52u, 0x42c84573u, 0x44138000u},
         0x429e0000u,
         {0x3ddf6b0fu, 0x3d5f6b0fu, 0x3ddf6b0fu},
         227,
         79},
        {225,
         12,
         {0x42ca20d4u, 0x4368a3bau, 0x44093000u},
         0x42880000u,
         {0x3f4f8087u, 0x3ecf8087u, 0x3f4f8087u},
         249,
         68},
        {225,
         20,
         {0x433d99b0u, 0x433fb3ffu, 0x4401f000u},
         0x42700000u,
         {0x3f4f8087u, 0x3ecf8087u, 0x3f4f8087u},
         265,
         60},
        {225,
         42,
         {0x42d7ceadu, 0x43452cc9u, 0x43daa000u},
         0x42180000u,
         {0x3f3ca363u, 0x3ebca363u, 0x3f3ca363u},
         309,
         38},
        {225, 80, {0x430610bcu, 0x43289328u, 0x43936000u}, 0, {0x3ca161fdu, 0x3c2161fdu, 0x3ca161fdu}, 385, 0},
        {225, 81, {0x43058000u, 0x4327c000u, 0x43918000u}, 0, {0x3c92b62bu, 0x3c12b62bu, 0x3c92b62bu}, 387, -1},
    }};
    const auto vector = [](const std::array<std::uint32_t, 3>& value)
    { return glm::vec3(Float(value[0]), Float(value[1]), Float(value[2])); };
    for (const float turn : {45.0f, 225.0f})
    {
        SCOPED_TRACE(turn);
        ActorState actor;
        actor.position = {130.0f, 170.0f, 290.0f};
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        Request request{0x619, 0xa, 15.0f, -1, 0};
        request.angles = {-45.0f, 0.0f, turn};
        ASSERT_TRUE(Create(joints, request, &actor, random).HasValue());

        for (int step = 1; step <= 81; ++step)
        {
            SCOPED_TRACE(step);
            if (step == 20)
                actor.position = {133.5f, 167.75f, 291.0f};
            ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

            const JointState& joint = joints[0];
            EXPECT_EQ(joint.targetPosition, actor.position);
            EXPECT_EQ(joint.alive, step < 81);
            for (const Checkpoint& checkpoint : Checkpoints)
            {
                if (checkpoint.turn != turn || checkpoint.step != step)
                    continue;
                EXPECT_EQ(joint.position, vector(checkpoint.position));
                EXPECT_EQ(joint.velocity.x, Float(checkpoint.radius));
                EXPECT_EQ(joint.light, vector(checkpoint.light));
                EXPECT_EQ(joint.field9ec, checkpoint.field9ec);
                EXPECT_EQ(joint.life, checkpoint.life);
            }
        }
        EXPECT_EQ(random.State(), CrtRandom(1).State());
    }
}

// The pet decal's 619/1 and the summons decal's 619/3 (0x518900), owned by the hero.
namespace
{
// 0x4e9810 with the target: yaw and pitch turn by `turn` (what SteerJoint does in the step).
void Steer(JointState& expected, const glm::vec3& target, float turn)
{
    using Client::Math::CreateAngle;
    using Client::Math::TurnAngle;
    const auto yaw = F32(CreateAngle(expected.position.x, expected.position.y, target.x, target.y));
    expected.angles.z = F32(TurnAngle(expected.angles.z, yaw, turn));
    const double x = D(expected.position.x) - target.x;
    const double y = D(expected.position.y) - target.y;
    const auto horizontal = F32(std::sqrt(D(F32(x * x)) + D(F32(y * y))));
    const auto pitch = F32(360.0 - CreateAngle(expected.position.z, horizontal, target.z, 0));
    expected.angles.x = F32(TurnAngle(expected.angles.x, pitch, turn));
}
} // namespace

// 0x5323d3..0x5323f8 and 0x5325ca: the 619/1 and 619/3 only have the common 0x619 part: speed 0, life 12, the width and
// 2 points. No rand(); the position is the first point and the target is not written, because it has an owner.
TEST(FullSetJointsTest, TheSummonEnergyIsBornWithTheCommonFieldsOfThe619)
{
    for (const std::int32_t subtype : {1, 3})
    {
        SCOPED_TRACE(subtype);
        ActorState hero;
        hero.position = {1300.0f, 1700.0f, 290.0f};
        std::array<JointState, 1> joints{};
        joints[0].targetPosition = {1.0f, 2.0f, 3.0f};
        CrtRandom random(1);
        Request request{0x619, subtype, 5.0f, -1, 0};
        request.angles = {40.0f, 0.0f, 213.0f};

        const auto created = Create(joints, request, &hero, random);

        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(random.State(), CrtRandom(1).State());
        const JointState& joint = joints[0];
        EXPECT_TRUE(joint.alive);
        EXPECT_EQ(joint.life, 12);
        EXPECT_EQ(joint.maximumCount, 2);
        EXPECT_EQ(joint.count, 0);
        EXPECT_EQ(joint.speed, 0.0f);
        EXPECT_EQ(joint.width, 5.0f);
        EXPECT_EQ(joint.texture, 0x619);
        EXPECT_EQ(joint.light, glm::vec3(1.0f));
        EXPECT_EQ(joint.position, request.first);
        EXPECT_EQ(joint.angles, request.angles);
        EXPECT_EQ(joint.targetPosition, glm::vec3(1.0f, 2.0f, 3.0f));
        EXPECT_EQ(joint.ownerContext, &hero);
    }
}

namespace
{
// 0x538a52..0x538d28 with the 619/1 or 619/3 (the test below checks it).
void ClimbTowardsTheOwner(std::int32_t subtype)
{
    ActorState hero;
    hero.position = {1300.0f, 1700.0f, 290.0f};
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, subtype, 5.0f, -1, 0};
    request.first = {1450.0f, 1600.0f, 380.0f};
    request.angles = {40.0f, 0.0f, 213.0f};
    ASSERT_TRUE(Create(joints, request, &hero, random).HasValue());
    JointState expected = joints[0];
    Client::Particles::FrameSprites sprites;
    CrtRandom draws(1);
    const double tenth = D(Float(0x3dcccccdu));

    for (int step = 1; step <= 13; ++step)
    {
        SCOPED_TRACE(step);
        if (step == 6)
            hero.position = {1310.0f, 1690.0f, 300.0f};
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0, &sprites).HasValue());

        const auto matrix = Client::Math::OriginalAngleMatrix(expected.angles);
        const glm::vec3 moved = Client::Math::OriginalVectorRotate({0.0f, -expected.speed, 0.0f}, matrix);
        if (expected.speed > 0)
            for (int axis = 0; axis < 3; ++axis)
                expected.position[axis] = F32(D(expected.position[axis]) + D(moved[axis]));
        expected.speed = F32(4.0 + D(expected.speed));
        const glm::vec3 target{hero.position.x, hero.position.y, F32(D(hero.position.z) + 120.0)};
        Steer(expected, target, 10.0f);
        const double brightness = static_cast<double>(12 - expected.life) * tenth;
        if (subtype == 3)
            expected.light = {F32(brightness), F32(brightness * D(Float(0x3f19999au))),
                              F32(brightness * D(Float(0x3ecccccdu)))};
        else
            expected.light = {F32(brightness * D(Float(0x3ecccccdu))), F32(brightness * D(Float(0x3f19999au))),
                              F32(brightness)};
        if (expected.life == 1)
        {
            const auto rotation = static_cast<float>(draws.Next() % 360);
            const auto scale = F32(static_cast<double>((draws.Next() & 7) + 8) * D(Float(0x3e4ccccdu)));
            const auto shown = std::ranges::find_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; });
            ASSERT_NE(shown, sprites.Sprites().end());
            EXPECT_EQ(shown->texture, 0x5fd);
            EXPECT_EQ(shown->position, target);
            EXPECT_EQ(shown->scale, scale);
            EXPECT_EQ(shown->light, expected.light);
            EXPECT_EQ(shown->rotation, rotation);
        }
        // The last life's sprite stays in the frame table: none before and only that one after.
        EXPECT_EQ(std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }),
                  step >= 12 ? 1 : 0);
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;

        const JointState& joint = joints[0];
        EXPECT_EQ(joint.speed, expected.speed);
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.angles, expected.angles);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.alive, step < 13);
        EXPECT_EQ(random.State(), draws.State());
    }
}

} // namespace

// 0x538a52..0x538d28: after the common advance with the earlier speed, the speed rises 4; it turns 10 at a time toward
// the hero 120 higher, read live; the light is (12 − life) · 0.1f times (0.4f; 0.6f; 1) for the /1 and times (1; 0.6f;
// 0.4f) for the /3 (0x538bc8) and, with life at 1, the 0x5fd sprite at that target with a rand() % 360 turn and scale
// ((rand() & 7) + 8) · 0.2f. The common tail with the matrix from the start of the step; it dies at step 13.
TEST(FullSetJointsTest, TheSummonEnergyClimbsTowardsItsOwnerAndShinesOnItsLastLife)
{
    for (const std::int32_t subtype : {1, 3})
    {
        SCOPED_TRACE(subtype);
        ClimbTowardsTheOwner(subtype);
    }
}

// 0x538b17: without the hero it has no one to follow; 0x538cce: in the last life, without the RNG or the sprites it
// stops before touching anything.
TEST(FullSetJointsTest, TheSummonEnergyNeedsItsOwnerAndItsLastSprite)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 1, 5.0f, -1, 0};
    ASSERT_TRUE(Create(joints, request, nullptr, random).HasValue());
    const auto ownerless = Client::Joints::StepJoints(joints, random);
    ASSERT_FALSE(ownerless.HasValue());
    EXPECT_NE(ownerless.Error().find("0x538b17"), std::string::npos) << ownerless.Error();

    ActorState hero;
    std::array<JointState, 1> last{};
    ASSERT_TRUE(Create(last, request, &hero, random).HasValue());
    last[0].life = 1;
    const JointState before = last[0];
    const auto blind = Client::Joints::StepJoints(last, random);
    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x538cce"), std::string::npos) << blind.Error();
    EXPECT_EQ(last[0].speed, before.speed);
    EXPECT_EQ(last[0].light, before.light);
    EXPECT_EQ(last[0].life, 1);
}

// The 0x813 bolts' 619/6 (0x52cb85), ownerless. 0x5323d3..0x532415: the common 0x619 part and light (1; 1; 0.5) with 4
// points; the target is the second point. No rand().
TEST(FullSetJointsTest, TheBoltEnergyIsBornWithAPaleLight)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 6, 5.0f, -1, 0};
    request.angles = {40.0f, 0.0f, 213.0f};

    const auto created = Create(joints, request, nullptr, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(random.State(), CrtRandom(1).State());
    const JointState& joint = joints[0];
    EXPECT_EQ(joint.life, 12);
    EXPECT_EQ(joint.maximumCount, 4);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.width, 5.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f, 1.0f, 0.5f));
    EXPECT_EQ(joint.position, request.first);
    EXPECT_EQ(joint.targetPosition, request.second);
    EXPECT_EQ(joint.ownerContext, nullptr);
}

// 0x538a52..0x538a70 and 0x538c25..0x538ca9: after the common advance with the earlier speed, the speed rises 2,
// without turning or changing the light. With 10 life or less, the 0x5fd sprite at the target with the gray light (6 −
// |life − 6|) · 0.15f, a rand() % 360 turn and scale ((rand() & 7) + 8) · 0.05f: eleven sprites, from step 3 to 13,
// when it dies.
TEST(FullSetJointsTest, TheBoltEnergyGlidesAndSparksInItsLastLives)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 6, 5.0f, -1, 0};
    request.angles = {40.0f, 0.0f, 213.0f};
    ASSERT_TRUE(Create(joints, request, nullptr, random).HasValue());
    JointState expected = joints[0];
    Client::Particles::FrameSprites sprites;
    CrtRandom draws(1);
    std::size_t made = 0;

    for (int step = 1; step <= 13; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0, &sprites).HasValue());

        const auto matrix = Client::Math::OriginalAngleMatrix(expected.angles);
        const glm::vec3 moved = Client::Math::OriginalVectorRotate({0.0f, -expected.speed, 0.0f}, matrix);
        if (expected.speed > 0)
            for (int axis = 0; axis < 3; ++axis)
                expected.position[axis] = F32(D(expected.position[axis]) + D(moved[axis]));
        expected.speed = F32(2.0 + D(expected.speed));
        if (expected.life <= 10)
        {
            const auto rotation = static_cast<float>(draws.Next() % 360);
            const auto scale = F32(static_cast<double>((draws.Next() & 7) + 8) * D(Float(0x3d4ccccdu)));
            const int distance = expected.life < 6 ? 6 - expected.life : expected.life - 6;
            const auto glow = F32(static_cast<double>(6 - distance) * D(Float(0x3e19999au)));
            ASSERT_LT(made, sprites.Sprites().size());
            const auto& shown = sprites.Sprites()[made++];
            EXPECT_TRUE(shown.alive);
            EXPECT_EQ(shown.texture, 0x5fd);
            EXPECT_EQ(shown.position, request.second);
            EXPECT_EQ(shown.scale, scale);
            EXPECT_EQ(shown.light, glm::vec3(glow));
            EXPECT_EQ(shown.rotation, rotation);
        }
        EXPECT_EQ(std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }),
                  static_cast<std::ptrdiff_t>(made));
        expected.count = Appended(expected.count, expected.maximumCount);
        --expected.life;

        const JointState& joint = joints[0];
        EXPECT_EQ(joint.speed, expected.speed);
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.angles, request.angles);
        EXPECT_EQ(joint.light, glm::vec3(1.0f, 1.0f, 0.5f));
        EXPECT_EQ(joint.count, expected.count);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.alive, step < 13);
        EXPECT_EQ(random.State(), draws.State());
    }
    EXPECT_EQ(made, 11u);
}

// 0x538c5d: with 10 life or less, without the RNG or the sprites it stops; before that, it does not ask for them.
TEST(FullSetJointsTest, TheBoltEnergyNeedsTheSpritesOnlyForItsSparks)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, 6, 5.0f, -1, 0};
    ASSERT_TRUE(Create(joints, request, nullptr, random).HasValue());
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
    EXPECT_EQ(joints[0].life, 10);
    const auto blind = Client::Joints::StepJoints(joints, random);
    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x538c5d"), std::string::npos) << blind.Error();
}

// The wizard's charged skill 619/8 (0x5189f0), owned by the character. The expected values are literals, computed
// outside the port (Fraction and the x87's roundings; seed 1's first rand() is 41).
namespace
{
constexpr std::int32_t ChargedEnergy = 8;
} // namespace

// 0x5323d3..0x5323f8 and 0x5324ea..0x53254a: the common 0x619 part (speed 0, the width) and case 8: life 0x11, 3
// points, light (0.5; 0.5; (41 & 0x7f) · 1/255f + 0.5) and the target at the second point with z + 100, even with an
// owner. One rand(); the position and angles are the request's.
TEST(FullSetJointsTest, TheChargedEnergyIsBornWithAHalfLightAndOneRandom)
{
    ActorState hero;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, ChargedEnergy, 10.0f, -1, 0};
    request.angles = {40.0f, 0.0f, 213.0f};

    const auto created = Create(joints, request, &hero, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(created.Value(), std::optional<std::size_t>(0));
    CrtRandom once(1);
    static_cast<void>(once.Next());
    EXPECT_EQ(random.State(), once.State());
    const JointState& joint = joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.type, 0x619);
    EXPECT_EQ(joint.subtype, ChargedEnergy);
    EXPECT_EQ(joint.texture, 0x619);
    EXPECT_EQ(joint.life, 17);
    EXPECT_EQ(joint.maximumCount, 3);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.speed, 0.0f);
    EXPECT_EQ(joint.width, 10.0f);
    EXPECT_EQ(joint.light, glm::vec3(0.5f, 0.5f, Float(0x3f292929u)));
    EXPECT_EQ(joint.position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(joint.angles, glm::vec3(40.0f, 0.0f, 213.0f));
    EXPECT_EQ(joint.targetPosition, glm::vec3(110.0f, 190.0f, 405.0f));
    EXPECT_EQ(joint.ownerContext, &hero);
}

// 0x530860..0x53088a: with the table full it leaves before the case, without the light's rand().
TEST(FullSetJointsTest, TheChargedEnergyIsNotBornInAFullPoolNorDrawsItsRandom)
{
    ActorState hero;
    std::array<JointState, 2> joints{};
    for (JointState& joint : joints)
        joint.alive = true;
    CrtRandom random(1);

    const auto created = Create(joints, Request{0x619, ChargedEnergy, 10.0f, -1, 0}, &hero, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_FALSE(created.Value().has_value());
    EXPECT_EQ(random.State(), CrtRandom(1).State());
}

// 0x538a52..0x538a70, 0x538a94..0x538b15 and 0x53c393, with 17 life and speed 0 (no common advance): the speed at 4; it
// turns 10 at a time toward the target (300, 200, 400) from (100, 200, 400): heading 90 leaves z at 10 and pitch 360
// (0x4e93f0 without x gives 0) leaves x at 360; the 0x5ac sprite at the position with scale 1, the light, turn 0 and
// blend 0. The light does not change (0x53c628 takes the /8 to 0x538c23). No rand(); the common tail: one point and 16.
TEST(FullSetJointsTest, TheChargedEnergySteersTowardsItsTargetAndLeavesAFlare)
{
    ActorState hero;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    Request request{0x619, ChargedEnergy, 10.0f, -1, 0};
    request.first = {100.0f, 200.0f, 400.0f};
    request.second = {300.0f, 200.0f, 300.0f};
    ASSERT_TRUE(Create(joints, request, &hero, random).HasValue());
    const auto afterBirth = random.State();
    Client::Particles::FrameSprites sprites;

    const auto stepped = Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0, &sprites);

    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), afterBirth);
    const JointState& joint = joints[0];
    const glm::vec3 light{0.5f, 0.5f, Float(0x3f292929u)};
    EXPECT_EQ(joint.speed, 4.0f);
    EXPECT_EQ(joint.position, glm::vec3(100.0f, 200.0f, 400.0f));
    EXPECT_EQ(joint.angles, glm::vec3(360.0f, 0.0f, 10.0f));
    EXPECT_EQ(joint.light, light);
    EXPECT_EQ(joint.targetPosition, glm::vec3(300.0f, 200.0f, 400.0f));
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joint.life, 16);
    EXPECT_TRUE(joint.alive);
    ASSERT_EQ(std::ranges::count_if(sprites.Sprites(), [](const auto& sprite) { return sprite.alive; }), 1);
    const auto& flare = sprites.Sprites()[0];
    EXPECT_EQ(flare.texture, 0x5ac);
    EXPECT_EQ(flare.position, glm::vec3(100.0f, 200.0f, 400.0f));
    EXPECT_EQ(flare.scale, 1.0f);
    EXPECT_EQ(flare.light, light);
    EXPECT_EQ(flare.rotation, 0.0f);
    EXPECT_EQ(flare.blend, 0);
}

// 0x538aad..0x538af5: with 10 life or less it does not turn; with less than 6, each light channel times 0.5555556f
// (0x6ba234): 0.5 gives 0x3e8e38e4 and 0x3f292929 gives 0x3ebbf4d9. The 0x5ac sprite comes out the same, with the light
// that is left.
TEST(FullSetJointsTest, TheChargedEnergyStopsTurningAndFadesBelowSixLives)
{
    struct Case
    {
        std::int32_t life;
        glm::vec3 light;
    };
    const glm::vec3 born{0.5f, 0.5f, Float(0x3f292929u)};
    for (const Case& probe :
         {Case{10, born}, Case{6, born}, Case{5, {Float(0x3e8e38e4u), Float(0x3e8e38e4u), Float(0x3ebbf4d9u)}}})
    {
        SCOPED_TRACE(probe.life);
        ActorState hero;
        std::array<JointState, 1> joints{};
        CrtRandom random(1);
        Request request{0x619, ChargedEnergy, 10.0f, -1, 0};
        request.first = {100.0f, 200.0f, 400.0f};
        request.second = {300.0f, 200.0f, 300.0f};
        request.angles = {20.0f, 0.0f, 30.0f};
        ASSERT_TRUE(Create(joints, request, &hero, random).HasValue());
        joints[0].life = probe.life;
        Client::Particles::FrameSprites sprites;

        ASSERT_TRUE(Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0, &sprites).HasValue());

        EXPECT_EQ(joints[0].angles, request.angles);
        EXPECT_EQ(joints[0].light, probe.light);
        EXPECT_EQ(joints[0].life, probe.life - 1);
        EXPECT_EQ(sprites.Sprites()[0].light, probe.light);
        EXPECT_TRUE(sprites.Sprites()[0].alive);
    }
}

// 0x538b0d: each step's sprite needs the frame's sprite table; without it, it stops before touching the joint.
TEST(FullSetJointsTest, TheChargedEnergyNeedsTheFrameSprites)
{
    ActorState hero;
    std::array<JointState, 1> joints{};
    CrtRandom random(1);
    ASSERT_TRUE(Create(joints, Request{0x619, ChargedEnergy, 10.0f, -1, 0}, &hero, random).HasValue());

    const auto blind = Client::Joints::StepJoints(joints, random);

    ASSERT_FALSE(blind.HasValue());
    EXPECT_NE(blind.Error().find("0x538b0d"), std::string::npos) << blind.Error();
    EXPECT_EQ(joints[0].speed, 0.0f);
    EXPECT_EQ(joints[0].life, 17);
}
