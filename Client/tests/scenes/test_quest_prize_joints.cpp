#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRetainedMath.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"

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

// The 0x60f/0xa of a quest prize (A3 0xc9..0xcb, 0x4de0fe), with the character as owner: it is born like the 0x60f/0
// (0x53373a..0x5337da and 0x53428f..0x5342d2) and in the step (0x539f1b → 0x53b239) turns the same but goes down by vz
// (0x53b2d5). The expected values repeat the ASM formulas with another CrtRandom of the same seed.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointRequest;
using Client::Kernel::JointState;
using Client::Math::Detail::CosineProductPC53;
using Client::Math::Detail::SineProductPC53;

constexpr std::int32_t Sparkle = 0x60f, FallingSubtype = 0xa;
constexpr float PrizeWidth = 40.0f;
constexpr std::int32_t PrizeKey = 2;

using Client::Math::D;
using Client::Math::F32;

float Float(std::uint32_t bits)
{
    return std::bit_cast<float>(bits);
}

// As 0x4de0fe requests it: the copied point with z + 200 in both points, the character's angles and owner, width 40
// and key 2.
Client::Core::Expected<std::optional<std::size_t>, std::string>
CreateFalling(std::span<JointState> joints, ActorState& owner, CrtRandom& random, float width = PrizeWidth)
{
    glm::vec3 raised{100.0f, 200.0f, 500.0f};
    glm::vec3 angles{};
    const JointRequest request{.type = Sparkle,
                               .firstPosition = raised,
                               .secondPosition = raised,
                               .angles = angles,
                               .subtype = FallingSubtype,
                               .ownerContext = &owner,
                               .scale = width,
                               .key = PrizeKey,
                               .skill = 0,
                               .skillSerial = 0,
                               .target = -1};
    return Client::Joints::CreateFaunaJoint(joints, request, random);
}

// What the 0x60f/0xa does in one 0x53b239 step with key 2, without the fall. With the angles at 0, the common movement
// (0x53590e) only shifts y, which the turn overwrites.
void TurnFalling(JointState& expected)
{
    const double argument = (static_cast<double>(expected.life) + D(expected.velocity.y)) / PrizeKey;
    const std::optional<double> cosine = CosineProductPC53(argument, expected.speed);
    const std::optional<double> sine = SineProductPC53(argument, expected.speed);
    ASSERT_TRUE(cosine && sine);
    expected.position.x = F32(*cosine + D(expected.targetPosition.x));
    expected.position.y = F32(D(expected.targetPosition.y) - *sine);
    expected.position.z = F32(D(expected.position.z) - D(expected.velocity.z));
    if (expected.life < 10)
    {
        const double factor = D(Float(0x3f44ec4fu));
        expected.light = {F32(D(expected.light.x) * factor), F32(D(expected.light.y) * factor),
                          F32(D(expected.light.z) * factor)};
    }
}
} // namespace

// 0x533677, 0x53373a..0x5337da and 0x53428f..0x5342d2: life 100 and 20 points; vz (rand() % 150) · 0.01f, vy rand() %
// 500 − 250 and speed 40 (140 is the /0x19's); with width 40, above 10, life 50 and vz (rand() % 250 + 200) · 0.01f.
// The character's light and the target at the second point. Three rand(), without the /0x12's blend.
TEST(QuestPrizeJointsTest, TheFallingSparkleIsBornLikeTheSparkle)
{
    ActorState owner;
    owner.light = {0.25f, 0.5f, 0.75f};
    std::array<JointState, 2> joints{};
    CrtRandom random(7);

    const auto created = CreateFalling(joints, owner, random);

    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), std::optional<std::size_t>(0));
    CrtRandom draws(7);
    static_cast<void>(draws.Next());
    const int drift = draws.Next();
    const int rise = draws.Next();
    EXPECT_EQ(random.State(), draws.State());
    const JointState& joint = joints[0];
    EXPECT_TRUE(joint.alive);
    EXPECT_EQ(joint.type, Sparkle);
    EXPECT_EQ(joint.texture, Sparkle);
    EXPECT_EQ(joint.subtype, FallingSubtype);
    EXPECT_EQ(joint.blend, 1);
    EXPECT_EQ(joint.ownerContext, &owner);
    EXPECT_EQ(joint.light, owner.light);
    EXPECT_EQ(joint.life, 50);
    EXPECT_EQ(joint.maximumCount, 20);
    EXPECT_EQ(joint.width, PrizeWidth);
    EXPECT_EQ(joint.key, PrizeKey);
    EXPECT_EQ(joint.speed, 40.0f);
    EXPECT_EQ(joint.velocity, glm::vec3(0.0f, static_cast<float>(drift % 500 - 250),
                                        F32(static_cast<double>(rise % 250 + 200) * D(Float(0x3c23d70au)))));
    EXPECT_EQ(joint.position, glm::vec3(100.0f, 200.0f, 500.0f));
    EXPECT_EQ(joint.targetPosition, glm::vec3(100.0f, 200.0f, 500.0f));
    EXPECT_FALSE(joints[1].alive);
}

// 0x53b239..0x53b2e1 with key 2: (life + vy) / 2 as the argument, x and y around the target, and z going down by vz
// each step (fsub, 0x53b2d5). With life under 10, the light times 0.7692308f. With width 40 it does not read the owner
// even if its action is a falling one: it lives its 50 steps.
TEST(QuestPrizeJointsTest, TheFallingSparkleTurnsAndGoesDown)
{
    ActorState owner;
    owner.light = glm::vec3(1.0f);
    owner.animation.action = 0xd;
    std::array<JointState, 1> joints{};
    CrtRandom random(7);
    ASSERT_TRUE(CreateFalling(joints, owner, random).HasValue());
    JointState expected = joints[0];
    const CrtRandom born = random;

    for (int step = 1; step <= 51; ++step)
    {
        SCOPED_TRACE(step);
        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());
        TurnFalling(expected);
        expected.count = std::min(expected.count + 1, expected.maximumCount - 1);
        --expected.life;
        const JointState& joint = joints[0];
        EXPECT_EQ(joint.position, expected.position);
        EXPECT_EQ(joint.light, expected.light);
        EXPECT_EQ(joint.subtype, FallingSubtype);
        EXPECT_EQ(joint.life, expected.life);
        EXPECT_EQ(joint.alive, step < 51);
    }
    EXPECT_LT(joints[0].position.z, 500.0f);
    EXPECT_EQ(random.State(), born.State());
}

// 0x53b366..0x53b3d1: the /0xa also falls to the 0x60f/1 with width under 40 and the owner's action in [0xd, 0x21] or
// [0x38, 0x3c]: no tail and life at 10 at most.
TEST(QuestPrizeJointsTest, ANarrowFallingSparkleFallsWithItsOwner)
{
    for (const std::uint8_t action : {std::uint8_t{0xc}, std::uint8_t{0x38}})
    {
        SCOPED_TRACE(static_cast<int>(action));
        ActorState owner;
        owner.light = glm::vec3(1.0f);
        owner.animation.action = action;
        std::array<JointState, 1> joints{};
        CrtRandom random(7);
        ASSERT_TRUE(CreateFalling(joints, owner, random, 10.0f).HasValue());
        EXPECT_EQ(joints[0].life, 100);

        ASSERT_TRUE(Client::Joints::StepJoints(joints, random).HasValue());

        const bool falls = action == 0x38;
        EXPECT_EQ(joints[0].subtype, falls ? 1 : FallingSubtype);
        EXPECT_EQ(joints[0].tailEnabled, !falls);
        EXPECT_EQ(joints[0].life, falls ? 9 : 99);
    }
}
