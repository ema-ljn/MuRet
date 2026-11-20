#include "Scenes/FaunaJointCreation.h"
#include "Core/Expected.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameItemGlowEffects.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/OriginalRotation.h"
#include "Render/Models/OriginalRetainedMath.h"
#include "World/Sprites.h"
#include "World/Particles.h"
#include "World/IParticleDrawingServices.h"
#include "World/ParticleDrawing.h"
#include "Render/ParticleBillboardRequest.h"
#include "Render/TextureEnvMode.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>

#include "World/IFishStepServices.h"
#include "World/ModelEffects.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/GameDrawTail.h"
#include "Render/Models/Cloth.h"
#include "Scenes/UiItemDrawing.h"

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

TEST(GameJointStepTest, Skill39BoltsConstructAndAdvanceWithoutRandomDraws)
{
    std::array<Client::Scenes::JointState, 1> joints{};
    glm::vec3 position{100, 200, 300};
    glm::vec3 target{400, 500, 600};
    glm::vec3 angles{10, 20, 30};
    Client::Render::Models::CrtRandom random(1);
    const Client::World::FishEffectRequest request{0x60f, position, target, angles, 0x17, nullptr, 40, 0, 0, 0, -1};
    const auto created = Client::Scenes::CreateFaunaJoint(joints, request, random);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), 0u);
    const auto& joint = joints[0];
    EXPECT_EQ(joint.position, (glm::vec3{100, 200, 500}));
    EXPECT_EQ(joint.targetPosition, target);
    EXPECT_EQ(joint.angles, angles);
    EXPECT_EQ(joint.speed, 0);
    EXPECT_EQ(joint.velocity, (glm::vec3{1, 5, 1}));
    EXPECT_EQ(joint.count, -1);
    EXPECT_EQ(joint.maximumCount, 15);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.width, 40);
    EXPECT_EQ(joint.life, 24);
    const auto stepped = Client::Scenes::StepJoints(joints, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(joint.life, 23);
    EXPECT_EQ(random.State(), 1u);
}

TEST(GameJointStepTest, Skill39ConstructionPreservesTheRetainedQuadAndSeparatesItsBendAngles)
{
    for (const int key : {-1, 0, 1, 2, 3, 4, 5, 6, 0xffff})
    {
        SCOPED_TRACE(key);
        std::array<Client::Scenes::JointState, 1> joints{};
        auto& joint = joints[0];
        joint.width = 8;
        joint.count = 49;
        joint.maximumCount = 50;
        joint.pathIncrement = {999, 888, 777};
        joint.tailEnabled = true;
        glm::vec3 point{100, 200, 300}, target{400, 500, 600}, angles{};
        int opaqueOwner = 7;
        Client::Render::Models::CrtRandom random(1);
        const Client::World::FishEffectRequest request{0x60f, point, target, angles, 0x17, &opaqueOwner,
                                                       40,    key,   0,      0,      -1};
        const auto created = Client::Scenes::CreateFaunaJoint(joints, request, random);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        EXPECT_EQ(joint.points[0][0], (glm::vec3{96, 200, 300}));
        EXPECT_EQ(joint.points[0][3], (glm::vec3{100, 200, 304}));
        EXPECT_EQ(joint.width, 40);
        EXPECT_EQ(joint.count, -1);
        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(joint.ownerContext, &opaqueOwner);
        EXPECT_EQ(joint.angles, angles);
        const auto signedKey = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(key));
        EXPECT_EQ(joint.field9ec, signedKey);
        EXPECT_EQ(joint.life, key == 5 ? 10 : 24 - signedKey);
        EXPECT_EQ(joint.maximumCount, key == 5 ? 20 : 15);
        EXPECT_EQ(joint.drawEnabled, key == 5 ? 2 : 3);
        EXPECT_EQ(joint.targetPosition, target);
        const float lift = key == 0 || key == 2   ? 200.0f
                           : key == 1 || key == 3 ? 10.0f
                           : key == 4 || key == 5 ? 100.0f
                                                  : 0.0f;
        EXPECT_EQ(joint.position, point + glm::vec3(0, 0, lift));
        EXPECT_EQ(joint.pathIncrement, joint.position);
        const float yaw = key == 0 || key == 1 ? 90.0f : key == 2 || key == 3 ? -90.0f : key == 4 ? 180.0f : 0.0f;
        EXPECT_EQ(joint.bendAngles, (glm::vec3{key == 5 ? 90.0f : 0.0f, 0, yaw}));
        EXPECT_EQ(random.State(), 1u);
    }
}

TEST(GameJointStepTest, Skill39BoltsWaitGrowToTheirCapFadeAndExpireAtMinusOne)
{
    for (const int key : {0, 1, 2, 3, 4})
    {
        SCOPED_TRACE(key);
        std::array<Client::Scenes::JointState, 1> joints{};
        glm::vec3 point{100, 200, 300}, angles{10, 20, 30};
        Client::Render::Models::CrtRandom random(1);
        const Client::World::FishEffectRequest request{0x60f, point, point, angles, 0x17, nullptr, 40, key, 0, 0, -1};
        ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue());
        auto& joint = joints[0];
        const auto anchor = joint.pathIncrement;
        while (joint.life >= 20)
        {
            ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
            EXPECT_EQ(joint.count, -1);
            EXPECT_EQ(joint.position, anchor);
            EXPECT_EQ(joint.speed, 0);
        }
        for (const int count : {0, 5, 14})
        {
            ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
            EXPECT_EQ(joint.count, count);
        }
        EXPECT_EQ(joint.velocity.z, 13);
        EXPECT_EQ(joint.velocity.y, 15);
        EXPECT_EQ(joint.speed, 0);
        EXPECT_EQ(joint.bendAngles.x, key == 0 || key == 2 ? 55.0f : key == 1 || key == 3 ? -35.0f : 10.0f);
        // Siete incrementos6,5..15,5, luego ocho de16,5:209, sin usar el15 almacenado como delta.
        EXPECT_EQ(joint.bendAngles.z, key == 0 || key == 1 ? -89.0f : key == 2 || key == 3 ? 149.0f : 1.0f);
        EXPECT_EQ(joint.angles, angles);
        EXPECT_EQ(joint.pathIncrement, anchor);
        while (joint.life >= 0)
        {
            const auto light = joint.light;
            const int life = joint.life;
            ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
            EXPECT_EQ(joint.count, 14);
            const auto factor = std::bit_cast<float>(0x3f44ec4fu);
            for (int axis = 0; axis < 3; ++axis)
                EXPECT_EQ(joint.light[axis],
                          life < 10 ? static_cast<float>(static_cast<double>(light[axis]) * factor) : light[axis]);
            EXPECT_EQ(joint.alive, joint.life >= 0);
        }
        EXPECT_EQ(joint.life, -1);
        EXPECT_EQ(random.State(), 1u);
    }
}

TEST(GameJointStepTest, Skill39KeyFiveKeepsThePriorSpeedForTheLowLifeCommonMotion)
{
    std::array<Client::Scenes::JointState, 1> joints{};
    glm::vec3 point{100, 200, 300}, angles{10, 20, 30};
    Client::Render::Models::CrtRandom random(1);
    const Client::World::FishEffectRequest request{0x60f, point, point, angles, 0x17, nullptr, 40, 5, 0, 0, -1};
    ASSERT_TRUE(Client::Scenes::CreateFaunaJoint(joints, request, random).HasValue());
    auto& joint = joints[0];
    for (const int count : {0, 5, 14})
    {
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
        EXPECT_EQ(joint.count, count);
        EXPECT_EQ(joint.speed, 200);
    }
    ASSERT_EQ(joint.life, 7);
    const auto before = joint.position;
    const auto offset =
        Client::Render::Models::OriginalVectorRotate({0, -200, 0}, Client::Render::Models::OriginalAngleMatrix(angles));
    const glm::vec3 moved{static_cast<float>(static_cast<double>(before.x) + offset.x),
                          static_cast<float>(static_cast<double>(before.y) + offset.y),
                          static_cast<float>(static_cast<double>(before.z) + offset.z)};
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
    EXPECT_EQ(joint.position, moved);
    const auto cross = Client::Render::Models::OriginalVectorRotate(
        {0, 0, -20}, Client::Render::Models::OriginalAngleMatrix(joint.bendAngles));
    const glm::vec3 face{static_cast<float>(static_cast<double>(moved.x) + cross.x),
                         static_cast<float>(static_cast<double>(moved.y) + cross.y),
                         static_cast<float>(static_cast<double>(moved.z) + cross.z)};
    EXPECT_EQ(joint.points[0][2], face);
    EXPECT_EQ(joint.speed, 0);
    EXPECT_EQ(joint.count, 15);
    EXPECT_EQ(joint.bendAngles.z, angles.z);
    EXPECT_EQ(joint.bendAngles.x, -119.0f);
    EXPECT_EQ(joint.angles, angles);
    EXPECT_EQ(joint.velocity.z, 13);
    EXPECT_EQ(random.State(), 1u);
}

TEST(GameJointStepTest, PiercingTrailJointsRetainTheirModelOwnerAndAdvanceWithoutRandomness)
{
    Client::World::ModelEffect owner;
    owner.alive = true;
    owner.start = {100, 200, 300};
    owner.transform.position = {130, 260, 390};
    owner.copiedAngles = {10, 20, 30};
    glm::vec3 first{1, 2, 3}, target{4, 5, 6}, angles{};
    std::array<Client::Scenes::JointState, 4> joints{};
    Client::Render::Models::CrtRandom random(1);
    for (int subtype = 0; subtype < 4; ++subtype)
    {
        const Client::World::FishEffectRequest request{0x610, first, target, angles, subtype, &owner, 12, 30, 0, 0, -1};
        const auto created =
            Client::Scenes::CreateFaunaJoint(joints, request, random, Client::Scenes::JointOwnerKind::ModelEffect);
        ASSERT_TRUE(created.HasValue()) << created.Error();
        ASSERT_EQ(created.Value(), static_cast<std::size_t>(subtype));
        const auto& joint = joints[static_cast<std::size_t>(subtype)];
        EXPECT_EQ(joint.ownerContext, &owner);
        EXPECT_EQ(joint.life, 30);
        EXPECT_EQ(joint.key, 0);
        EXPECT_EQ(joint.maximumCount, 50);
        EXPECT_EQ(joint.width, 20);
        EXPECT_EQ(joint.light, glm::vec3(0.5f));
        EXPECT_EQ(joint.velocity, (glm::vec3{18, 0, 0}));
        EXPECT_EQ(joint.targetPosition, target);
    }
    const auto stepped = Client::Scenes::StepJoints(joints, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    for (const auto& joint : joints)
    {
        EXPECT_EQ(joint.count, 4);
        EXPECT_EQ(joint.life, 29);
    }
    EXPECT_EQ(random.State(), 1u);
}

TEST(GameJointStepTest, PiercingTrailsFollowLiveModelFieldsThenAppendAndDieWithTheirOwner)
{
    for (const int subtype : {0, 1, 2, 3})
    {
        SCOPED_TRACE(subtype);
        std::array<Client::Scenes::JointState, 1> joints{};
        Client::World::ModelEffect owner;
        owner.alive = true;
        owner.start = {100, 200, 300};
        owner.transform.position = {130, 260, 390};
        owner.copiedAngles = {10, 20, 30};
        glm::vec3 point{}, angles{15, 25, 35};
        Client::Render::Models::CrtRandom random(1);
        const Client::World::FishEffectRequest request{0x610, point, point, angles, subtype, &owner, 12, 30, 0, 0, -1};
        ASSERT_TRUE(
            Client::Scenes::CreateFaunaJoint(joints, request, random, Client::Scenes::JointOwnerKind::ModelEffect)
                .HasValue());
        auto& joint = joints[0];
        const float phase = subtype == 0 ? 0.0f : subtype == 1 ? 90.0f : subtype == 2 ? 180.0f : 240.0f;
        EXPECT_EQ(joint.speed, phase);
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
        glm::vec3 finalAngles = owner.copiedAngles;
        finalAngles.y = phase + 20.0f;
        const auto offset = Client::Render::Models::OriginalVectorRotate(
            {18, 0, 0}, Client::Render::Models::OriginalAngleMatrix(finalAngles));
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_EQ(joint.position[axis],
                      static_cast<float>(static_cast<double>(owner.transform.position[axis]) + offset[axis]));
        EXPECT_EQ(joint.targetPosition, owner.transform.position);
        EXPECT_EQ(joint.points[0], joint.points[1]);
        EXPECT_EQ(joint.count, 4);
        EXPECT_EQ(joint.angles, angles);
        joint.life = 14;
        owner.transform.position = {160, 290, 420};
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
        EXPECT_EQ(joint.targetPosition, owner.transform.position);
        EXPECT_EQ(joint.light, glm::vec3(std::bit_cast<float>(0x3eaaaaabu)));
        owner.alive = false;
        const auto target = joint.targetPosition;
        ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
        EXPECT_EQ(joint.position, target + glm::vec3(0, 0, 50));
        EXPECT_EQ(joint.life, -2);
        EXPECT_FALSE(joint.alive);
        EXPECT_EQ(joint.count, 9);
        EXPECT_EQ(random.State(), 1u);
    }
}

TEST(GameJointStepTest, CosineDoubleProductsPreserveTheMultiplierAndSignedZeroAtTheOrigin)
{
    namespace Math = Client::Render::Models::Detail;
    const double retained = -0.12345678901234567;
    EXPECT_EQ(Math::CosineDoubleProductPC53(0, retained), retained);
    ASSERT_TRUE(Math::CosineDoubleProductPC53(0, -0.0).has_value());
    EXPECT_TRUE(std::signbit(*Math::CosineDoubleProductPC53(0, -0.0)));
    EXPECT_EQ(Math::SineDoubleProductPC53(0, retained), -0.0);
}

// Referencia independiente mpmath100dps: trascendente R64, productos R53 y storesF32 del ASM.
TEST(GameJointStepTest, PiercingImpactsFollowTheirRetainedCurveAndExpireWithoutReadingTheirOwner)
{
    struct Fixture
    {
        int life;
        std::array<std::uint32_t, 3> position;
    };
    const std::array fixtures{
        Fixture{20, {0x42988a79u, 0x43360cfbu, 0x43a36793u}}, Fixture{10, {0x42ee94f8u, 0x433a5743u, 0x439f0494u}},
        Fixture{9, {0x42e0b86du, 0x433367c4u, 0x439efcc1u}}, Fixture{1, {0x42f681f3u, 0x43276a4du, 0x4395632fu}},
        Fixture{0, {0x42f95c08u, 0x4325f566u, 0x4395d765u}}};
    std::array<Client::Scenes::JointState, 1> joints{};
    auto& joint = joints[0];
    joint.width = 8;
    Client::World::ModelEffect owner;
    owner.alive = false;
    glm::vec3 point{100, 200, 300}, angles{0, 0, 33.25f};
    Client::Render::Models::CrtRandom random(1);
    const Client::World::FishEffectRequest request{0x60f, point, point, angles, 6, &owner, 30, -1, 0, 0, -1};
    const auto created =
        Client::Scenes::CreateFaunaJoint(joints, request, random, Client::Scenes::JointOwnerKind::ModelEffect);
    ASSERT_TRUE(created.HasValue()) << created.Error();
    ASSERT_EQ(created.Value(), 0u);
    EXPECT_EQ(joint.life, 20);
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.maximumCount, 30);
    EXPECT_EQ(joint.width, 30);
    const auto retainedOffset =
        Client::Render::Models::OriginalVectorRotate({-4, 0, 0}, Client::Render::Models::OriginalAngleMatrix(angles));
    for (int axis = 0; axis < 3; ++axis)
        EXPECT_EQ(joint.points[0][0][axis],
                  static_cast<float>(static_cast<double>(point[axis]) + retainedOffset[axis]));
    EXPECT_EQ(joint.velocity.x, 41);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.velocity.y), 0x3f8c5cf2u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.velocity.z), 0xbfd616dau);
    const auto afterBirth = random.State();
    for (int life = 20; life >= 0; --life)
    {
        ASSERT_TRUE(joint.alive);
        ASSERT_EQ(joint.life, life);
        const auto stepped = Client::Scenes::StepJoints(joints, random);
        ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
        for (const auto& fixture : fixtures)
        {
            if (fixture.life == life)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.position[axis]),
                              fixture.position[static_cast<std::size_t>(axis)]);
                }
            }
        }
        EXPECT_EQ(joint.life, life - 1);
        EXPECT_EQ(joint.count, 21 - life);
        EXPECT_EQ(joint.alive, life > 0);
    }
    EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.light.x), 0x3d948ee3u);
    EXPECT_EQ(joint.light, glm::vec3(joint.light.x));
    EXPECT_EQ(random.State(), afterBirth);
}

TEST(GameJointStepTest, MissingCounterFailsAfterTheOwnerTargetWasCopied)
{
    Client::Scenes::FaunaObject owner;
    owner.active = 1;
    owner.position = {100, 200, 300};
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x10f;
    joint.subtype = 1;
    joint.ownerContext = &owner;
    joint.life = 50;
    const auto result = Client::Scenes::StepJoints(std::span(&joint, 1));
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x536187"), std::string::npos);
    EXPECT_EQ(joint.targetPosition, (glm::vec3{100, 200, 310}));
    EXPECT_EQ(joint.life, 50);
    EXPECT_EQ(joint.count, 0);
}

// Referencia independiente mpmath180dps: trig→R64, productosPC53 y spillsF32, no ejecuta main.exe.
TEST(GameJointStepTest, CounterPhaseWrapAndHalfFrequenciesMatchIndependentFixtures)
{
    struct Vector
    {
        std::size_t index;
        std::int32_t steps;
        std::array<std::uint32_t, 3> position;
        std::uint32_t blue;
    };
    const std::array vectors{Vector{0, 0, {0x42b2cb5cu, 0x4337557fu, 0x43de27bdu}, 0x3f01e1ffu},
                             Vector{0, 123, {0x42f7419eu, 0x4325dfd9u, 0x43d35dc3u}, 0x3f14ab99u},
                             Vector{1, 123, {0x420d027au, 0x432f752bu, 0x43a54486u}, 0x3f063b49u},
                             Vector{499, INT32_MIN, {0x429234d9u, 0x43673c2au, 0x43d393abu}, 0x3e8620e4u},
                             Vector{1, INT32_MAX, {0x431ef97cu, 0x432ba13fu, 0x43822862u}, 0x3e6aa8afu}};
    auto joints = std::make_unique<std::array<Client::Scenes::JointState, 500>>();
    for (const auto& vector : vectors)
    {
        joints->fill({});
        Client::Scenes::FaunaObject owner;
        owner.active = 1;
        owner.position = {100, 200, 300};
        auto& joint = (*joints)[vector.index];
        joint.alive = true;
        joint.type = 0x10f;
        joint.subtype = 1;
        joint.ownerContext = &owner;
        joint.life = 50;
        Client::Render::Models::ClothWorld cloth;
        cloth.logicSteps = vector.steps;
        ASSERT_TRUE(Client::Scenes::StepJoints(*joints, cloth.random, &cloth).HasValue());
        for (int axis = 0; axis < 3; ++axis)
            EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.position[axis]),
                      vector.position[static_cast<std::size_t>(axis)]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.light.z), vector.blue);
        EXPECT_FLOAT_EQ(joint.light.x, 0.2f);
        EXPECT_FLOAT_EQ(joint.light.y, 0.2f);
        EXPECT_EQ(joint.life, 49);
    }
}

TEST(GameJointStepTest, OpenCounterAccessAvoidsFourDrawsAndDeadOwnerNeverOpensIt)
{
    Client::Scenes::FaunaObject owner;
    owner.active = 1;
    std::array<Client::Scenes::JointState, 1> joints{};
    auto& joint = joints[0];
    joint.alive = true;
    joint.type = 0x10f;
    joint.subtype = 1;
    joint.ownerContext = &owner;
    joint.life = 100;
    Client::Render::Models::ClothWorld cloth;
    Client::Render::Models::CrtRandom expected;
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, cloth.random, &cloth).HasValue());
    for (int i = 0; i < 4; ++i)
        static_cast<void>(expected.Next());
    EXPECT_EQ(cloth.random.State(), expected.State());
    cloth.openCounterAccesses = 1;
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, cloth.random, &cloth).HasValue());
    EXPECT_EQ(cloth.random.State(), expected.State());
    owner.active = 0;
    joint.tailEnabled = true;
    joint.maximumCount = 5;
    ASSERT_TRUE(Client::Scenes::StepJoints(joints, cloth.random, &cloth).HasValue());
    EXPECT_EQ(cloth.random.State(), expected.State());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.life, -1);
    EXPECT_EQ(joint.count, 1);
}

TEST(GameJointStepTest, HelperBeamsPrebuildNineSegmentsWithoutReadingTheirOpaqueOwner)
{
    for (const int type : {0x613, 0x614})
    {
        std::array<Client::Scenes::JointState, 1> joints{};
        joints[0].targetPosition = {900, 800, 700};
        joints[0].width = 8;
        glm::vec3 first{};
        glm::vec3 second{100, 200, 300};
        glm::vec3 angles{};
        std::uint32_t opaqueOwner = 0;
        Client::World::FishEffectRequest request{type, first, second, angles, type == 0x613 ? 4 : 14, &opaqueOwner, 20,
                                                 -1,   0,     0,      -1};
        Client::Render::Models::CrtRandom random(19);
        Client::Render::Models::CrtRandom expected(19);
        const auto created = Client::Scenes::CreateFaunaJoint(joints, request, random);
        ASSERT_TRUE(created.HasValue());
        ASSERT_TRUE(created.Value().has_value());
        const auto& joint = joints[0];
        EXPECT_EQ(joint.count, 9);
        EXPECT_EQ(joint.maximumCount, 10);
        EXPECT_EQ(joint.life, 0);
        EXPECT_EQ(joint.position, (glm::vec3{900, 800, 700}));
        EXPECT_EQ(joint.points[0][0], (glm::vec3{80, 180, 270}));
        EXPECT_EQ(joint.points[9][0], (glm::vec3{-4, 0, 0}));
        EXPECT_EQ(random.State(), expected.State());
        if (type == 0x613)
        {
            ASSERT_TRUE(Client::Scenes::StepJoints(joints, random).HasValue());
            EXPECT_FALSE(joints[0].alive);
            EXPECT_EQ(joints[0].life, -1);
        }
        else
        {
            const auto stepped = Client::Scenes::StepJoints(joints, random);
            ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
            EXPECT_EQ(joints[0].position, (glm::vec3{900, 750, 700}));
            EXPECT_EQ(joints[0].life, -1);
            static_cast<void>(expected.Next());
            static_cast<void>(expected.Next());
            EXPECT_EQ(random.State(), expected.State());
        }
    }
}

TEST(GameJointStepTest, RealHelperPublisherCompletesTheFollowingTickWithItsFloorOwnerOpaque)
{
    Client::Scenes::FloorItemState owner;
    owner.type = 0x1a18;
    Client::Scenes::DrawnObject object;
    object.model = 0x1a18;
    std::array<Client::Render::Models::BoneTransform, 31> bones;
    bones.fill(Client::Render::Models::BoneTransform(1));
    std::array<Client::Scenes::JointState, 20> joints{};
    Client::Render::Models::CrtRandom random(19);
    Client::World::FrameSprites sprites;
    Client::Scenes::ItemSpecialDrawContext special;
    special.sprites = &sprites;
    Client::Scenes::ItemDrawContext context;
    context.special = &special;
    context.ownerContext = &owner;
    context.poseBones = [&]() { return std::span<const Client::Render::Models::BoneTransform>(bones); };
    context.createJoint =
        [&](const Client::World::FishEffectRequest& request) -> Client::Core::Expected<void, std::string>
    {
        auto created = Client::Scenes::CreateFaunaJoint(joints, request, random);
        if (!created.HasValue())
            return Client::Core::Unexpected(created.Error());
        return {};
    };
    std::optional<float> alpha;
    ASSERT_TRUE(Client::Scenes::PrepareItemGlowEffects(object, {}, 0, context, alpha).HasValue());
    for (const auto& joint : joints)
    {
        ASSERT_TRUE(joint.alive);
        EXPECT_EQ(joint.ownerContext, &owner);
        EXPECT_EQ(joint.count, 9);
    }
    const auto stepped = Client::Scenes::StepJoints(joints, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    for (const auto& joint : joints)
    {
        EXPECT_FALSE(joint.alive);
        EXPECT_EQ(joint.life, -1);
        EXPECT_EQ(joint.count, 9);
        EXPECT_EQ(joint.ownerContext, &owner);
    }
    EXPECT_EQ(owner.type, 0x1a18);
}

// 0x60c3b6..0x60c598 desde el dibujo de la UI (0x5e0720): las 20 uniones del 0x1a18 (12,6) con el objeto local de la
// pila de 0x5e0720 de dueño, que nadie desreferencia (07 Q-C1821). Con dueño, 0x530860 no escribe +0x58 (0x53099b):
// queda el del ocupante anterior del lugar, y la posición sale de ahí (0x5333a7). Nacen después del dibujo de las
// uniones del frame (0x637516, 0x637592) y mueren en el primer paso del siguiente (0x53c3ad..0x53c3bd), con dos rand()
// por cada 0x614 (0x536cb3, 0x536cda): el original no las dibuja nunca, y el dibujo intermedio de MuRet las apaga en su
// copia.
TEST(GameJointStepTest, TheUiWingJointsDieUnseenInTheirFirstStep)
{
    Client::Scenes::DrawnObject object;
    object.model = 0x1a18;
    std::array<Client::Render::Models::BoneTransform, 31> bones;
    bones.fill(Client::Render::Models::BoneTransform(1));
    auto tail = std::make_unique<Client::Scenes::GameDrawTailState>();
    for (auto& joint : tail->joints)
        joint.targetPosition = {900, 800, 700};
    Client::Render::Models::CrtRandom random(19);
    Client::World::FrameSprites sprites;
    Client::Scenes::ItemSpecialDrawContext special;
    special.sprites = &sprites;
    Client::Scenes::ItemDrawContext context;
    context.special = &special;
    context.ownerContext = Client::Scenes::UiItemLocalObject();
    context.poseBones = [&]() { return std::span<const Client::Render::Models::BoneTransform>(bones); };
    context.createJoint =
        [&](const Client::World::FishEffectRequest& request) -> Client::Core::Expected<void, std::string>
    {
        auto created = Client::Scenes::CreateFaunaJoint(tail->joints, request, random);
        if (!created.HasValue())
            return Client::Core::Unexpected(created.Error());
        return {};
    };
    const Client::Render::Models::CrtRandom created = random;
    std::optional<float> alpha;
    ASSERT_TRUE(Client::Scenes::PrepareItemGlowEffects(object, {}, 0, context, alpha).HasValue());
    EXPECT_EQ(random.State(), created.State());
    EXPECT_EQ(std::ranges::count_if(tail->joints, [](const auto& joint) { return joint.alive; }), 20);
    for (std::size_t index = 0; index < 20; ++index)
    {
        const Client::Scenes::JointState& joint = tail->joints[index];
        EXPECT_EQ(joint.ownerContext, Client::Scenes::UiItemLocalObject());
        EXPECT_EQ(joint.targetPosition, glm::vec3(900, 800, 700));
        EXPECT_EQ(joint.position, glm::vec3(900, 800, 700));
    }

    // El dibujo intermedio no las ve; la tabla de verdad las guarda para el paso.
    auto copy = std::make_unique<Client::Scenes::GameDrawTailState>(*tail);
    Client::Scenes::HideUiItemJoints(*copy);
    EXPECT_EQ(std::ranges::count_if(copy->joints, [](const auto& joint) { return joint.alive; }), 0);
    EXPECT_EQ(std::ranges::count_if(tail->joints, [](const auto& joint) { return joint.alive; }), 20);

    Client::Render::Models::CrtRandom expected = random;
    expected.Discard(20);
    const auto stepped = Client::Scenes::StepJoints(tail->joints, random);
    ASSERT_TRUE(stepped.HasValue()) << stepped.Error();
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(std::ranges::count_if(tail->joints, [](const auto& joint) { return joint.alive; }), 0);
}

TEST(GameJointStepTest, ReachedBeamEmitsThunderWithTwoCreationDrawsThenStepsItsRotation)
{
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.maximumCount = 10;
    joint.width = 50;
    joint.tailEnabled = true;
    joint.speed = 50;
    joint.light = {0.3f, 0.3f, 1};
    Client::Render::Models::CrtRandom random(1), expected(1);
    Client::World::Particles particles;
    const auto result = Client::Scenes::StepJoints(std::span(&joint, 1), random, nullptr, &particles);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    const auto size = static_cast<float>(static_cast<double>(expected.Next() % 8 + 6) * 0.1f);
    const auto rotation = static_cast<float>(expected.Next() % 360);
    const auto smokeRoll = expected.Next() % 8;
    ASSERT_NE(smokeRoll, 0);
    EXPECT_EQ(random.State(), expected.State());
    const auto& particle = particles.All()[0];
    ASSERT_TRUE(particle.alive);
    EXPECT_EQ(particle.type, 0x5ca);
    EXPECT_EQ(particle.life, 2);
    EXPECT_FLOAT_EQ(particle.scale, size);
    EXPECT_FLOAT_EQ(particle.rotation, rotation);
    EXPECT_EQ(particle.position, (glm::vec3{0, -50, 0}));
    EXPECT_EQ(particle.light, joint.light);
    EXPECT_EQ(joint.life, -1);
    EXPECT_EQ(joint.count, 2);
    ASSERT_TRUE(Client::World::StepParticlesChecked(particles, random).HasValue());
    EXPECT_EQ(particle.life, 1);
    EXPECT_FLOAT_EQ(particle.rotation, rotation + 20);
    EXPECT_EQ(particle.position, (glm::vec3{0, -50, 0}));
    ASSERT_TRUE(Client::World::StepParticlesChecked(particles, random).HasValue());
    EXPECT_FALSE(particle.alive);
    EXPECT_FLOAT_EQ(particle.rotation, rotation + 40);
}

TEST(GameJointStepTest, MissingParticlePoolKeepsSteeringJitterAndInnerTailBeforeLife)
{
    std::array<Client::Scenes::JointState, 2> joints{};
    auto& joint = joints[0];
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.maximumCount = 10;
    joint.width = 50;
    joint.speed = 50;
    joint.life = 9;
    joints[1] = joint;
    Client::Render::Models::CrtRandom random(19), expected(19);
    auto result = Client::Scenes::StepJoints(joints, random);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x536eb9"), std::string::npos);
    EXPECT_EQ(joint.position, (glm::vec3{0, -50, 0}));
    EXPECT_EQ(joint.angles, (glm::vec3{360, 0, 335}));
    EXPECT_EQ(joint.life, 9);
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(joints[1].position, glm::vec3(0));
    EXPECT_EQ(joints[1].count, 0);
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
}

namespace
{
class ThunderDrawing final : public Client::World::IParticleDrawingServices
{
public:
    Client::Core::Expected<glm::vec2, Client::World::ParticleError> Dimensions(std::int32_t image) override
    {
        events.push_back("dimensions");
        EXPECT_EQ(image, 0x5ca);
        return glm::vec2(64, 32);
    }
    Client::Core::Expected<std::uint8_t, Client::World::ParticleError> MaterialFlag(std::int32_t type) override
    {
        events.push_back("flag");
        EXPECT_EQ(type, 0x5ca);
        return 3;
    }
    Result Material(std::uint8_t flag) override
    {
        events.push_back("material");
        EXPECT_EQ(flag, 3);
        return {};
    }
    Result Subtractive() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Blended() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Type620850() override
    {
        ADD_FAILURE();
        return {};
    }
    Result Type620870() override
    {
        ADD_FAILURE();
        return {};
    }
    Result TextureEnv(Client::Render::TextureEnvMode) override
    {
        ADD_FAILURE();
        return {};
    }
    Result Billboard(Client::Render::ParticleBillboardRequest request) override
    {
        events.push_back("billboard");
        ++calls;
        EXPECT_FLOAT_EQ(request.width, 32);
        EXPECT_FLOAT_EQ(request.height, 16);
        EXPECT_FLOAT_EQ(request.rotation, 123);
        return {};
    }
    int calls = 0;
    std::vector<std::string> events;
};
} // namespace
TEST(GameJointStepTest, ThunderDrawingUsesTheCommonBillboardAfterMaterial)
{
    Client::World::Particles particles;
    auto& particle = particles.All()[0];
    particle.alive = true;
    particle.type = particle.texture = 0x5ca;
    particle.scale = 0.5f;
    particle.rotation = 123;
    particle.subtype = INT32_MIN;
    float clock = 0;
    ThunderDrawing services;
    const auto result = Client::World::DrawParticlesChecked(particles, 0, std::cref(clock), services);
    ASSERT_TRUE(result.HasValue());
    EXPECT_EQ(services.calls, 1);
    EXPECT_EQ(services.events, (std::vector<std::string>{"dimensions", "flag", "material", "billboard"}));
}

// Client/docs/research/scripts/make_helper_joint_vectors.py: referencia independiente, dos iteraciones y cola común con
// matriz previa.
TEST(GameJointStepTest, BeamMovementJitterAndBothTailMatricesMatchIndependentVectors)
{
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.position = {11, 22, 33};
    joint.angles = {10, 20, 30};
    joint.targetPosition = {1000, 2000, 3000};
    joint.count = -1;
    joint.width = 23;
    joint.speed = 50;
    joint.life = 5;
    joint.velocity.y = 7;
    // maximumCount limita también count; la cola final usa la matriz anterior al steering.
    joint.maximumCount = 2;
    joint.tailEnabled = true;
    Client::Render::Models::CrtRandom random(1);
    auto result = Client::Scenes::StepJoints(std::span(&joint, 1), random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    const std::array<std::uint32_t, 3> position{0x42bbae84u, 0xc1d4ec56u, 0x42ad7ad8u};
    const std::array<std::uint32_t, 3> head{0x42a8f6e0u, 0xc2001314u, 0x42b558a9u};
    const std::array<std::uint32_t, 3> previous{0x42882b5eu, 0xc26a24fbu, 0x425face6u};
    for (int axis = 0; axis < 3; ++axis)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.position[axis]), position[static_cast<std::size_t>(axis)]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.points[0][0][axis]), head[static_cast<std::size_t>(axis)]);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.points[1][0][axis]), previous[static_cast<std::size_t>(axis)]);
    }
    EXPECT_EQ(joint.angles, (glm::vec3{320, 20, 80}));
    EXPECT_EQ(joint.velocity.y, 7);
    EXPECT_EQ(joint.life, 4);
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(random.State(), 0xe7847115u);
}

TEST(GameJointStepTest, FullParticlePoolConsumesOnlyJointJitterAndTheSmokeDecision)
{
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.maximumCount = 10;
    joint.width = 50;
    joint.speed = 50;
    Client::World::Particles particles;
    for (auto& particle : particles.All())
        particle.alive = true;
    Client::Render::Models::CrtRandom random(1), expected(1);
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    ASSERT_NE(expected.Next() % 8, 0);
    const auto result = Client::Scenes::StepJoints(std::span(&joint, 1), random, nullptr, &particles);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(joint.life, -1);
    EXPECT_EQ(joint.count, 1);
    EXPECT_EQ(particles.All()[0].type, 0);
}

TEST(GameJointStepTest, SmokeUsesTheBeamPositionAfterThreeDiscardedScratchDraws)
{
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.maximumCount = 10;
    joint.width = 50;
    joint.speed = 50;
    Client::World::Particles particles;
    Client::Render::Models::CrtRandom random(19), expected(19);
    const auto result = Client::Scenes::StepJoints(std::span(&joint, 1), random, nullptr, &particles, 123.5f);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    for (int draw = 0; draw < 10; ++draw)
        static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    const auto& smoke = particles.All()[1];
    ASSERT_TRUE(smoke.alive);
    EXPECT_EQ(smoke.type, 0x5f2);
    EXPECT_EQ(smoke.position, (glm::vec3{0, -50, 0}));
    EXPECT_EQ(smoke.start, smoke.position);
    EXPECT_EQ(smoke.rotation, 123);
}

TEST(GameJointStepTest, MissingRandomStopsAfterBothSteeringStoresWithoutTailOrLife)
{
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.maximumCount = 10;
    joint.width = 23;
    joint.targetPosition = {100, 0, 100};
    joint.velocity = {1, 7, 3};
    joint.life = 8;
    const auto result = Client::Scenes::StepJoints(std::span(&joint, 1));
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x536cb3"), std::string::npos);
    EXPECT_EQ(joint.angles, (glm::vec3{335, 0, 25}));
    EXPECT_EQ(joint.velocity, (glm::vec3{1, 7, 3}));
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.life, 8);
}

TEST(GameJointStepTest, FullJointPoolLeavesThePublisherArgumentsAndRandomUntouched)
{
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 123;
    glm::vec3 first{1, 2, 3}, second{4, 5, 6}, angles{7, 8, 9};
    std::uint32_t opaqueOwner = 0;
    Client::World::FishEffectRequest request{0x614, first, second, angles, 14, &opaqueOwner, 23, -1, 0, 0, -1};
    Client::Render::Models::CrtRandom random(1);
    const auto result = Client::Scenes::CreateFaunaJoint(std::span(&joint, 1), request, random);
    ASSERT_TRUE(result.HasValue());
    EXPECT_FALSE(result.Value().has_value());
    EXPECT_EQ(joint.type, 123);
    EXPECT_EQ(random.State(), 1u);
    EXPECT_EQ(first, (glm::vec3{1, 2, 3}));
    EXPECT_EQ(second, (glm::vec3{4, 5, 6}));
}

TEST(GameJointStepTest, TailCapacityFailureKeepsTheReachedRandomPrefixAndLaterSlotsUntouched)
{
    std::array<Client::Scenes::JointState, 2> joints{};
    auto& joint = joints[0];
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.width = 23;
    joint.maximumCount = 51;
    joint.count = 49;
    joint.life = 7;
    joint.targetPosition = {100, 0, 100};
    joint.points[0][0] = {123, 456, 789};
    joints[1] = joint;
    Client::Render::Models::CrtRandom random(1), expected(1);
    const auto result = Client::Scenes::StepJoints(joints, random);
    ASSERT_FALSE(result.HasValue());
    EXPECT_NE(result.Error().find("0x53553f"), std::string::npos);
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    EXPECT_EQ(joint.count, 50);
    EXPECT_EQ(joint.life, 7);
    EXPECT_EQ(joint.points[0][0], (glm::vec3{123, 456, 789}));
    EXPECT_EQ(joints[1].count, 49);
    EXPECT_EQ(joints[1].angles, glm::vec3(0));
}

TEST(GameJointStepTest, EqualReachDistanceStillMovesAndZeroIterationsNeedNoRandom)
{
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.width = 23;
    joint.speed = 50;
    joint.targetPosition = {0, 25, 0}; // después del movimiento común quedan75, exactamente1.5*50.
    joint.maximumCount = 1;
    Client::Render::Models::CrtRandom random(1), expected(1);
    auto result = Client::Scenes::StepJoints(std::span(&joint, 1), random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_NE(joint.position, (glm::vec3{0, -50, 0}));
    static_cast<void>(expected.Next());
    static_cast<void>(expected.Next());
    EXPECT_EQ(random.State(), expected.State());
    joint = {};
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.life = 1;
    result = Client::Scenes::StepJoints(std::span(&joint, 1));
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_EQ(joint.life, 0);
    EXPECT_TRUE(joint.alive);
}

// El literal6ba108 y R64(atan)→productoPC53 distinguen estos casos de180/pi del host.
TEST(GameJointStepTest, UnsaturatedSteeringKeepsTheNativeAngleProductForBothAxes)
{
    struct Vector
    {
        glm::vec3 target;
        glm::vec3 angles;
        int axis;
    };
    const std::array vectors{Vector{{1, -1, 0}, {0, 0, 45}, 2}, Vector{{1, 0, -1}, {45, 0, 90}, 0}};
    for (const auto& vector : vectors)
    {
        Client::Scenes::JointState joint;
        joint.alive = true;
        joint.type = 0x614;
        joint.subtype = 14;
        joint.targetPosition = vector.target;
        joint.angles = vector.angles;
        joint.width = 23;
        joint.maximumCount = 1;
        Client::Render::Models::CrtRandom random(1);
        const auto result = Client::Scenes::StepJoints(std::span(&joint, 1), random);
        ASSERT_TRUE(result.HasValue()) << result.Error();
        EXPECT_EQ(std::bit_cast<std::uint32_t>(joint.angles[vector.axis]), 0x42340001u);
        EXPECT_EQ(joint.count, 0);
    }
}

TEST(GameJointStepTest, DistanceSpillRoundsUpToTheReachThresholdBeforeComparing)
{
    Client::Scenes::JointState joint;
    joint.alive = true;
    joint.type = 0x614;
    joint.subtype = 14;
    joint.width = 23;
    joint.maximumCount = 1;
    joint.speed = 2;
    // Tras el movimiento común, distancia retenida<3, pero su spillF32 es3 exactamente.
    joint.targetPosition = {1, -2, std::bit_cast<float>(0x403504f3u)};
    Client::Render::Models::CrtRandom random(1);
    const auto result = Client::Scenes::StepJoints(std::span(&joint, 1), random);
    ASSERT_TRUE(result.HasValue()) << result.Error();
    EXPECT_NE(joint.position, (glm::vec3{0, -2, 0}));
    EXPECT_EQ(joint.count, 0);
    EXPECT_EQ(joint.life, -1);
}
