#include "Scenes/FaunaJointCreation.h"
#include "Core/Expected.h"
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include "Scenes/CharacterDrawing.h"
#include "Scenes/GameItemGlowEffects.h"
#include "Scenes/GameItemSpecialDrawing.h"
#include "Render/Models/HalfLifeMath.h"
#include "World/Sprites.h"
#include "World/Particles.h"
#include "World/IParticleDrawingServices.h"
#include "World/ParticleDrawing.h"
#include "Render/ParticleBillboardRequest.h"
#include <functional>
#include <optional>

#include "World/IFishStepServices.h"
#include "Scenes/FaunaObject.h"
#include "Scenes/GameDrawTail.h"
#include "Render/Models/Cloth.h"
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

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
