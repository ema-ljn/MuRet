#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalRotation.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The swirls 0x6e8/0 and /1 that the 0x114/1 drops in pairs (0x51c25f, without an owner): the creation in
// 0x5345bf..0x5346d3 and the step in 0x53b7f1..0x53b93a, without the common movement or the common tail.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x6e8u;

using Client::Math::D;
using Client::Math::F32;

struct SwirlWorld
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype)
    {
        glm::vec3 from = start;
        glm::vec3 angles{90.0f, 0.0f, 30.0f};
        const Client::Kernel::JointRequest request{.type = 0x6e8,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = nullptr,
                                                   .scale = 85.0f,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random);
    }

    glm::vec3 start{1000.0f, 2000.0f, 300.0f};
    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
};
} // namespace

// 0x5345bf..0x5346d3: without the common tail, the width, speed 20, the angles (−90, 0, z), life 10 and 30 points; the
// /1 with the repeated texture (+0x9f0). The light at 1 with an odd rand() and (1; 0.8f; 0.6f) with an even one; +0x24
// at the position. With the table full, nothing, not even rand().
TEST(SwirlJointsTest, EachSwirlIsBornUprightWithItsLight)
{
    for (const std::int32_t subtype : {0, 1})
    {
        SCOPED_TRACE(subtype);
        SwirlWorld tested;
        const auto created = tested.Create(subtype);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        CrtRandom expected(Seed);
        const bool white = (expected.Next() & 1) != 0;
        EXPECT_EQ(tested.random.State(), expected.State());
        const JointState& joint = tested.joints[0];
        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(joint.repeatedTexture, subtype == 1);
        EXPECT_EQ(joint.width, 85.0f);
        EXPECT_EQ(joint.speed, 20.0f);
        EXPECT_EQ(joint.angles, glm::vec3(-90.0f, 0.0f, 30.0f));
        EXPECT_EQ(joint.life, 10);
        EXPECT_EQ(joint.maximumCount, 30);
        EXPECT_EQ(joint.velocity, glm::vec3(0.0f));
        EXPECT_EQ(joint.light,
                  white ? glm::vec3(1.0f)
                        : glm::vec3(1.0f, std::bit_cast<float>(0x3f4ccccdu), std::bit_cast<float>(0x3f19999au)));
        EXPECT_EQ(joint.pathIncrement, tested.start);
    }

    SwirlWorld full;
    for (JointState& joint : full.joints)
        joint.alive = true;
    const auto none = full.Create(0);
    ASSERT_TRUE(none.HasValue()) << none.Error();
    EXPECT_FALSE(none.Value().has_value());
    EXPECT_EQ(full.random.State(), CrtRandom(Seed).State());
}

// 0x53b7f1..0x53b8af: with life over 9, thirty times the tail with the angles' matrix and the position plus (0, 0,
// −speed) turned; always the speed 2 less and the light times 0.7142857f. Without the common movement or rand().
TEST(SwirlJointsTest, TheSwirlUnwindsOnlyOnItsFirstStep)
{
    SwirlWorld tested;
    ASSERT_TRUE(tested.Create(0).HasValue());
    JointState& joint = tested.joints[0];
    JointState expected = joint;
    const auto state = tested.random.State();
    const double fade = D(std::bit_cast<float>(0x3f36db6eu));

    ASSERT_TRUE(tested.Step().HasValue());

    const auto matrix = Client::Math::OriginalAngleMatrix(expected.angles);
    for (int turn = 0; turn < 30; ++turn)
    {
        const glm::vec3 moved = Client::Math::OriginalVectorRotate({0.0f, 0.0f, -20.0f}, matrix);
        for (int axis = 0; axis < 3; ++axis)
            expected.position[axis] = F32(D(moved[axis]) + D(expected.position[axis]));
    }
    EXPECT_EQ(joint.position, expected.position);
    // 0x535520 leaves the count at 30 − 1 with the tail full.
    EXPECT_EQ(joint.count, 29);
    EXPECT_EQ(joint.speed, 18.0f);
    for (int channel = 0; channel < 3; ++channel)
        expected.light[channel] = F32(D(expected.light[channel]) * fade);
    EXPECT_EQ(joint.light, expected.light);
    EXPECT_EQ(joint.life, 9);
    EXPECT_EQ(tested.random.State(), state);

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_EQ(joint.position, expected.position);
    EXPECT_EQ(joint.count, 29);
    EXPECT_EQ(joint.speed, 16.0f);
    for (int channel = 0; channel < 3; ++channel)
        expected.light[channel] = F32(D(expected.light[channel]) * fade);
    EXPECT_EQ(joint.light, expected.light);
    EXPECT_EQ(joint.life, 8);
}
