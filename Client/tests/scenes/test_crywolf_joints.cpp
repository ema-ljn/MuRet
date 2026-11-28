#include "Joints/FaunaJointCreation.h"

#include "Crt/CrtRandom.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Scenes/GameDrawTail.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>
#include <cstddef>

#include <array>
#include <cstdint>
#include <memory>

namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::JointState;

// 0x531b0c, /7: two rand() and z +1050, without the common tail; the step draws 50 segments without emitting the /2's
// particles. The 45 of the descent drop 20 each (0x53774b, 0x6b86d0).
TEST(CrywolfJointsTest, ArrivalBoltCreatesStepsAndDrawsWithoutTheChaosCastleEmission)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(7);
    glm::vec3 position{100, 200, 300};
    glm::vec3 angles{};
    const auto created = Client::Joints::CreateFaunaJoint(
        joints, Client::Kernel::JointRequest{0x615, position, position, angles, 7, nullptr, 60, -1, 0, 0, -1}, random);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), 0u);
    const auto& joint = joints[0];
    EXPECT_EQ(joint.position, (glm::vec3{96, 197, 1350}));
    EXPECT_EQ(joint.pathIncrement, joint.position);
    EXPECT_EQ(joint.targetPosition, position);
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.maximumCount, 50);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.texture, 0x614);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
    const auto stepped = Client::Joints::StepJoints(joints, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joint.life, 19);
    EXPECT_EQ(joint.count, 49);
    EXPECT_EQ(joint.position, position);
    EXPECT_EQ(joint.points[5][0].z, 1350.0f - 45 * 20.0f);
    EXPECT_FALSE(joint.tailEnabled);
    Client::Render::ModelBatch batch;
    batch.Begin(Client::Render::Pass3D{});
    ASSERT_TRUE(Client::Scenes::DrawJoints(batch, joints, 0).HasValue());
    EXPECT_EQ(batch.Vertices().size(), 588u);
}

// 0x531b0c, /1 (the arriving summon's bolt, 0x51747c): like the /7, but z rises 800 (0x6b8ac8) and the tail turns off
// after the target (0x531b9e); the step drops 16 per segment (0x537753) and, like the /7, does not turn the tail back
// on (0x5379ca), with the same rand() as the /2 and /7. The /2 turns it on with life over 10.
TEST(CrywolfJointsTest, TheSummonBoltFallsLikeTheArrivalWithItsTailOff)
{
    std::array<JointState, 3> joints{};
    CrtRandom random(7);
    CrtRandom twin(7);
    glm::vec3 position{100, 200, 300};
    glm::vec3 angles{};
    for (const std::int32_t subtype : {1, 2})
    {
        const auto created = Client::Joints::CreateFaunaJoint(
            joints, Client::Kernel::JointRequest{0x615, position, position, angles, subtype, nullptr, 55, -1, 0, 0, -1},
            subtype == 1 ? random : twin);
        ASSERT_TRUE(created.HasValue()) << created.Error();
    }
    const auto& joint = joints[0];
    EXPECT_EQ(joint.position, (glm::vec3{96, 197, 1100}));
    EXPECT_EQ(joint.pathIncrement, joint.position);
    EXPECT_EQ(joint.targetPosition, position);
    EXPECT_EQ(joint.width, 55.0f);
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.maximumCount, 50);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_TRUE(joints[1].tailEnabled);
    EXPECT_EQ(random.State(), 0xc40e66b9u);
    EXPECT_EQ(twin.State(), random.State());
    std::array<JointState, 1> alone{joint};
    std::array<JointState, 1> chaos{joints[1]};
    chaos[0].position = alone[0].position;
    chaos[0].pathIncrement = alone[0].pathIncrement;
    auto sprites = std::make_unique<Client::Particles::FrameSprites>();
    auto particles = std::make_unique<Client::Particles::Particles>();
    ASSERT_TRUE(Client::Joints::StepJoints(alone, random).HasValue());
    ASSERT_TRUE(Client::Joints::StepJoints(chaos, twin, nullptr, particles.get(), 0, sprites.get()).HasValue());
    EXPECT_EQ(alone[0].life, 19);
    EXPECT_EQ(alone[0].position, position);
    EXPECT_EQ(alone[0].points[5][0].z, 1100.0f - 45 * 16.0f);
    EXPECT_EQ(alone[0].count, 49);
    // The /2 with the tail on also appends the common tail's segment.
    EXPECT_EQ(chaos[0].points[6][0].z, alone[0].points[5][0].z);
    EXPECT_FALSE(alone[0].tailEnabled);
    EXPECT_TRUE(chaos[0].tailEnabled);
}

// 0x530afa → 0x5309d6 → 0x530a00: /17 goes through FirstQuad with the retained width (the 0x614 only reaches 0x5309d6
// via table 0x534ccc, after the first segment) and uses one rand() for the speed. It follows the common 614 step (turn
// 25, two rand per segment, tail/life), without the width-50 emissions.
TEST(CrywolfJointsTest, RisingBeamBuildsItsFirstQuadWithTheRetainedWidthAndCompletesItsStep)
{
    std::array<JointState, 1> joints{};
    joints[0].points[0][0] = {91, 92, 93};
    joints[0].width = 6;
    CrtRandom random(7);
    glm::vec3 origin{};
    glm::vec3 angles{};
    const auto created = Client::Joints::CreateFaunaJoint(
        joints, Client::Kernel::JointRequest{0x614, origin, origin, angles, 17, nullptr, 10, -1, 0, 0, -1}, random);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), 0u);
    const auto& joint = joints[0];
    EXPECT_EQ(joint.points[0][0], (glm::vec3{-3, 0, 0}));
    EXPECT_EQ(joint.points[0][3], (glm::vec3{0, 0, 3}));
    EXPECT_EQ(joint.speed, 21);
    EXPECT_EQ(joint.width, 10);
    EXPECT_EQ(joint.life, 10);
    EXPECT_EQ(joint.maximumCount, 50);
    EXPECT_EQ(random.State(), 0x003d7aaeu);
    const auto stepped = Client::Joints::StepJoints(joints, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joint.life, 9);
    EXPECT_EQ(joint.count, 2);
    EXPECT_EQ(random.State(), 0xbb6f8e98u);
}

TEST(CrywolfJointsTest, TheBarrierBeamsFollowBothRetainedBonePointsAndStopWithTheirActor)
{
    Client::Kernel::ActorState actor;
    actor.alive = true;
    actor.effectOrigin = {100, 200, 300};
    actor.bonePoint4c = {400, 500, 600};
    std::array<JointState, 2> joints{};
    CrtRandom random(7);
    glm::vec3 origin{};
    glm::vec3 angles{};
    for (const std::int32_t subtype : {2, 3})
    {
        const auto created = Client::Joints::CreateFaunaJoint(
            joints, Client::Kernel::JointRequest{0x618, origin, origin, angles, subtype, &actor, 30, -1, 0, 0, -1},
            random);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_EQ(created.Value(), static_cast<std::size_t>(subtype - 2));
    }
    EXPECT_EQ(joints[0].position, actor.effectOrigin);
    EXPECT_EQ(joints[1].position, actor.bonePoint4c);
    EXPECT_EQ(joints[0].life, 999999999);
    EXPECT_EQ(joints[0].maximumCount, 20);
    EXPECT_EQ(joints[0].speed, 0);
    EXPECT_EQ(joints[0].light, (glm::vec3{0.5f, 0.1f, 1}));
    EXPECT_EQ(random.State(), 7u);
    actor.effectOrigin = {10, 20, 30};
    actor.bonePoint4c = {40, 50, 60};
    auto particles = std::make_unique<Client::Particles::Particles>();
    const auto stepped = Client::Joints::StepJoints(joints, random, nullptr, particles.get());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joints[0].position, actor.effectOrigin);
    EXPECT_EQ(joints[1].position, actor.bonePoint4c);
    EXPECT_EQ(joints[0].life, 999999998);
    EXPECT_EQ(joints[1].count, 1);
    ASSERT_TRUE(particles->All()[0].alive);
    EXPECT_EQ(particles->All()[0].type, 0x5d5);
    EXPECT_EQ(particles->All()[0].position, actor.effectOrigin);
    // 0x540bf2 adds 0.5 to each component of the color that 0x538285 computed for the particle.
    EXPECT_FLOAT_EQ(particles->All()[0].light.x, 0.9f);
    EXPECT_FLOAT_EQ(particles->All()[0].light.y, 0.58f);
    EXPECT_FLOAT_EQ(particles->All()[0].light.z, 1.3f);
    EXPECT_EQ(particles->All()[1].position, actor.bonePoint4c);
    EXPECT_EQ(random.State(), 7u);
    actor.alive = false;
    ASSERT_TRUE(Client::Joints::StepJoints(joints, random, nullptr, particles.get()).HasValue());
    EXPECT_FALSE(joints[0].alive);
    EXPECT_FALSE(joints[1].alive);
    EXPECT_EQ(joints[0].life, 999999998);
    EXPECT_EQ(joints[0].count, 1);
}
} // namespace
