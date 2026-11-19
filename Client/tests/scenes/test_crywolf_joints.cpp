#include "Scenes/FaunaJointCreation.h"

#include "Render/ModelBatch.h"
#include "Render/Models/Cloth.h"
#include "Render/Pass3D.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameDrawTail.h"
#include "World/IFishStepServices.h"
#include "World/Particles.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>
#include <cstddef>

#include <array>
#include <cstdint>
#include <memory>

namespace
{
using Client::Render::Models::CrtRandom;
using Client::Scenes::JointState;

// 0x531b0c, /7: dos rand() y z +1050, sin la cola común; el paso dibuja 50 tramos sin emitir las partículas de /2. Los
// 45 de la bajada bajan 20 cada uno (0x53774b, 0x6b86d0).
TEST(CrywolfJointsTest, ArrivalBoltCreatesStepsAndDrawsWithoutTheChaosCastleEmission)
{
    std::array<JointState, 1> joints{};
    CrtRandom random(7);
    glm::vec3 position{100, 200, 300};
    glm::vec3 angles{};
    const auto created = Client::Scenes::CreateFaunaJoint(
        joints, Client::World::FishEffectRequest{0x615, position, position, angles, 7, nullptr, 60, -1, 0, 0, -1},
        random);
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
    const auto stepped = Client::Scenes::StepJoints(joints, random);
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

// 0x530afa → 0x5309d6 → 0x530a00: /17 pasa por FirstQuad con el ancho retenido (el 0x614 solo llega a 0x5309d6 por
// la tabla 0x534ccc, después del primer tramo) y usa un rand() para la velocidad. Sigue el paso común del 614
// (giro 25, dos rand por tramo, cola/vida), sin las emisiones del ancho 50.
TEST(CrywolfJointsTest, RisingBeamBuildsItsFirstQuadWithTheRetainedWidthAndCompletesItsStep)
{
    std::array<JointState, 1> joints{};
    joints[0].points[0][0] = {91, 92, 93};
    joints[0].width = 6;
    CrtRandom random(7);
    glm::vec3 origin{};
    glm::vec3 angles{};
    const auto created = Client::Scenes::CreateFaunaJoint(
        joints, Client::World::FishEffectRequest{0x614, origin, origin, angles, 17, nullptr, 10, -1, 0, 0, -1}, random);
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
    const auto stepped = Client::Scenes::StepJoints(joints, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joint.life, 9);
    EXPECT_EQ(joint.count, 2);
    EXPECT_EQ(random.State(), 0xbb6f8e98u);
}

TEST(CrywolfJointsTest, TheBarrierBeamsFollowBothRetainedBonePointsAndStopWithTheirActor)
{
    Client::Scenes::ActorState actor;
    actor.alive = true;
    actor.effectOrigin = {100, 200, 300};
    actor.bonePoint4c = {400, 500, 600};
    std::array<JointState, 2> joints{};
    CrtRandom random(7);
    glm::vec3 origin{};
    glm::vec3 angles{};
    for (const std::int32_t subtype : {2, 3})
    {
        const auto created = Client::Scenes::CreateFaunaJoint(
            joints, Client::World::FishEffectRequest{0x618, origin, origin, angles, subtype, &actor, 30, -1, 0, 0, -1},
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
    auto particles = std::make_unique<Client::World::Particles>();
    const auto stepped = Client::Scenes::StepJoints(joints, random, nullptr, particles.get());
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joints[0].position, actor.effectOrigin);
    EXPECT_EQ(joints[1].position, actor.bonePoint4c);
    EXPECT_EQ(joints[0].life, 999999998);
    EXPECT_EQ(joints[1].count, 1);
    ASSERT_TRUE(particles->All()[0].alive);
    EXPECT_EQ(particles->All()[0].type, 0x5d5);
    EXPECT_EQ(particles->All()[0].position, actor.effectOrigin);
    // 0x540bf2 añade0,5 a cada componente del color que 0x538285 calculó para la partícula.
    EXPECT_FLOAT_EQ(particles->All()[0].light.x, 0.9f);
    EXPECT_FLOAT_EQ(particles->All()[0].light.y, 0.58f);
    EXPECT_FLOAT_EQ(particles->All()[0].light.z, 1.3f);
    EXPECT_EQ(particles->All()[1].position, actor.bonePoint4c);
    EXPECT_EQ(random.State(), 7u);
    actor.alive = false;
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, random, nullptr, particles.get()).HasValue());
    EXPECT_FALSE(joints[0].alive);
    EXPECT_FALSE(joints[1].alive);
    EXPECT_EQ(joints[0].life, 999999998);
    EXPECT_EQ(joints[0].count, 1);
}
} // namespace
