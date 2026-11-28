#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Terrain/TerrainLighting.h"
#include "WorldObjects/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The 0x610/6 that Kalima's 0x95 throws with the 0x11 toward its target (0x43b0ed, 0x43b11c and 0x43b14e): the
// creation in 0x531c43..0x531cc7 and 0x531e04..0x531e27 and the step in 0x537b36..0x537c7d, with the target as owner.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x610u;

using Client::Math::D;
using Client::Math::F32;

struct BoltWorld
{
    BoltWorld()
    {
        target.alive = true;
        target.position = {1600.0f, 1000.0f, 150.0f};
    }

    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::uint8_t skill = 0)
    {
        glm::vec3 from = start;
        glm::vec3 to = target.position;
        glm::vec3 angles{0.0f, 0.0f, 40.0f};
        const Client::Kernel::JointRequest request{.type = 0x610,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = 6,
                                                   .ownerContext = owned ? &target : nullptr,
                                                   .scale = 30.0f,
                                                   .key = 0x32,
                                                   .skill = skill,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random,
                                                owned ? JointOwnerKind::Actor : JointOwnerKind::ByType);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0.0f, nullptr, lighting.get());
    }

    ActorState target;
    bool owned = true;
    glm::vec3 start{1000.0f, 1000.0f, 300.0f};
    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
};

// 0x4e9810 with the target: yaw and pitch turn by `turn`; returns the unrounded distance.
double Steer(JointState& expected, const glm::vec3& target, float turn)
{
    using Client::Math::CreateAngle;
    using Client::Math::TurnAngle;
    const auto yaw = F32(CreateAngle(expected.position.x, expected.position.y, target.x, target.y));
    expected.angles.z = F32(TurnAngle(expected.angles.z, yaw, turn));
    const double x = D(expected.position.x) - target.x;
    const double y = D(expected.position.y) - target.y;
    const auto z = F32(D(expected.position.z) - target.z);
    const auto horizontal = F32(std::sqrt(D(F32(x * x)) + D(F32(y * y))));
    const auto pitch = F32(360.0 - CreateAngle(expected.position.z, horizontal, target.z, 0));
    expected.angles.x = F32(TurnAngle(expected.angles.x, pitch, turn));
    return std::sqrt((D(z) * z + D(F32(x * x))) + D(F32(y * y)));
}

// The common advance (0x53590e) with the previous speed and angles.
void MoveAhead(JointState& expected)
{
    const glm::vec3 moved = Client::Math::OriginalVectorRotate({0.0f, -expected.speed, 0.0f},
                                                               Client::Math::OriginalAngleMatrix(expected.angles));
    for (int axis = 0; axis < 3; ++axis)
        expected.position[axis] = F32(D(expected.position[axis]) + D(moved[axis]));
}
} // namespace

// 0x531c43..0x531cc7, 0x531e04..0x531e27 and 0x532003: the life in the key (50) and the key at 0; the light at 1, blend
// 4, the width, 16 points, speed 5 and the target at the second point. Without rand(). With color 3 without an owner,
// the original dereferences the null pointer (+0xf4, 0x531c7b) and crashes: MuRet stops.
TEST(TargetBoltJointsTest, TheBoltIsBornWithTheKeyAsItsLife)
{
    BoltWorld tested;
    const auto created = tested.Create();
    ASSERT_TRUE(created.HasValue()) << created.Error();
    EXPECT_EQ(tested.random.State(), CrtRandom(Seed).State());
    const JointState& joint = tested.joints[0];
    EXPECT_EQ(joint.life, 50);
    EXPECT_EQ(joint.key, 0);
    EXPECT_EQ(joint.maximumCount, 16);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.blend, 4);
    EXPECT_EQ(joint.width, 30.0f);
    EXPECT_EQ(joint.speed, 5.0f);
    EXPECT_EQ(joint.position, tested.start);
    EXPECT_EQ(joint.targetPosition, tested.target.position);
    EXPECT_EQ(joint.ownerContext, &tested.target);
    EXPECT_EQ(joint.texture, 0x610);

    BoltWorld lonely;
    lonely.owned = false;
    const auto cut = lonely.Create(3);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_NE(cut.Error().find("0x531c7b"), std::string::npos) << cut.Error();
}

// 0x537b4a..0x537c7d: with life 30 or more, the angles' z drops 10 and the position rises 2.5 after the common
// advance; the speed rises 5 and it turns with it toward the target 150 higher; far away (more than 70) it does not
// brake. With life 15 or more, the light does not change. One rand() for the dynamic light.
TEST(TargetBoltJointsTest, TheBoltRisesAndTurnsTowardsItsTarget)
{
    BoltWorld tested;
    ASSERT_TRUE(tested.Create().HasValue());
    JointState expected = tested.joints[0];

    ASSERT_TRUE(tested.Step().HasValue());

    MoveAhead(expected);
    expected.angles.z = F32(D(expected.angles.z) - 10.0);
    expected.position.z = F32(D(expected.position.z) + 2.5);
    expected.speed = 10.0f;
    const glm::vec3 aim{tested.target.position.x, tested.target.position.y, F32(D(tested.target.position.z) + 150.0)};
    EXPECT_GT(Steer(expected, aim, 10.0f), 70.0);
    const JointState& joint = tested.joints[0];
    EXPECT_EQ(joint.position, expected.position);
    EXPECT_EQ(joint.angles, expected.angles);
    EXPECT_EQ(joint.speed, 10.0f);
    EXPECT_EQ(joint.light, glm::vec3(1.0f));
    EXPECT_EQ(joint.life, 49);
    CrtRandom draws(Seed);
    static_cast<void>(draws.Next());
    EXPECT_EQ(tested.random.State(), draws.State());
}

// 0x537b6b..0x537c29: the speed is capped at 30; at 70 or less from the target, with the z turn over 20 and the speed
// 20 or more, it drops 10. With life under 15, the three channels are x times 0.8333333f.
TEST(TargetBoltJointsTest, TheBoltBrakesNearItsTargetAndFades)
{
    BoltWorld tested;
    tested.target.position = {1060.0f, 975.0f, 150.0f};
    ASSERT_TRUE(tested.Create().HasValue());
    JointState& joint = tested.joints[0];
    joint.angles = {};
    joint.speed = 25.0f;
    joint.life = 10;
    joint.light = {0.9f, 0.5f, 0.25f};
    JointState expected = joint;

    ASSERT_TRUE(tested.Step().HasValue());

    MoveAhead(expected);
    const glm::vec3 aim{tested.target.position.x, tested.target.position.y, F32(D(tested.target.position.z) + 150.0)};
    EXPECT_LE(Steer(expected, aim, 30.0f), 70.0);
    EXPECT_GT(std::abs(D(expected.angles.z)), 20.0);
    EXPECT_EQ(joint.position, expected.position);
    EXPECT_EQ(joint.angles, expected.angles);
    EXPECT_EQ(joint.speed, 20.0f);
    EXPECT_EQ(joint.light, glm::vec3(F32(D(0.9f) * D(std::bit_cast<float>(0x3f555555u)))));
    EXPECT_EQ(joint.life, 9);
}

// 0x537b3f..0x537b44 and 0x53be97: without an owner it goes out without rand(), with the common tail and life.
TEST(TargetBoltJointsTest, TheBoltWithoutItsTargetGoesOut)
{
    BoltWorld tested;
    ASSERT_TRUE(tested.Create().HasValue());
    JointState& joint = tested.joints[0];
    joint.ownerContext = nullptr;

    ASSERT_TRUE(tested.Step().HasValue());

    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, 49);
    EXPECT_EQ(tested.random.State(), CrtRandom(Seed).State());
}

// 0x537bf3..0x537c29 at the edge of 70: from (1000, 1025, 300) with the angles at 0, speed 25 and life 20, the common
// advance leaves the joint at (1000, 1000, 300) and the target 150 higher is at the same height, 70 farther in x. With
// the target's y at 1000 the distance is exactly 70 and it brakes (30 − 10); with the next float y, just over 70, it
// does not. In both, the radius 2 dynamic light at the position with ((rand() & 3) + 8) · 0.03f times (0.4f; 1; 0.8f).
TEST(TargetBoltJointsTest, TheBoltBrakesOnlyWithinSeventy)
{
    for (const float y : {std::nextafter(1000.0f, 2000.0f), 1000.0f})
    {
        SCOPED_TRACE(y);
        BoltWorld tested;
        tested.start = {1000.0f, 1025.0f, 300.0f};
        tested.target.position = {1070.0f, y, 150.0f};
        ASSERT_TRUE(tested.Create().HasValue());
        JointState& joint = tested.joints[0];
        joint.angles = {};
        joint.speed = 25.0f;
        joint.life = 20;
        CrtRandom draws = tested.random;

        const auto stepped = tested.Step();

        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        EXPECT_EQ(joint.position, glm::vec3(1000.0f, 1000.0f, 300.0f));
        EXPECT_EQ(joint.speed, y == 1000.0f ? 20.0f : 30.0f);
        EXPECT_EQ(joint.light, glm::vec3(1.0f));
        const double luminosity =
            static_cast<double>((draws.Next() & 3) + 8) * D(std::bit_cast<float>(0x3cf5c28fu)); // 0x6b8bd4
        EXPECT_EQ(tested.random.State(), draws.State());
        const glm::vec3 color{F32(luminosity * D(std::bit_cast<float>(0x3ecccccdu))), F32(luminosity),
                              F32(luminosity * D(std::bit_cast<float>(0x3f4ccccdu)))};
        auto lit = std::make_unique<Client::Terrain::TerrainLighting>();
        ASSERT_TRUE(Client::WorldObjects::AddDynamicLight(lit.get(), joint.position, color, 2, "prueba").HasValue());
        EXPECT_TRUE(lit->cells == tested.lighting->cells);
        const auto dark = std::make_unique<Client::Terrain::TerrainLighting>();
        EXPECT_FALSE(lit->cells == dark->cells);
    }
}
