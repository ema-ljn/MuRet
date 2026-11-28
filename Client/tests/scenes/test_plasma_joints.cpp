#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Math/OriginalAngles.h"
#include "Math/OriginalRotation.h"
#include "Kernel/ActorState.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Particles/Particles.h"
#include "Terrain/TerrainLighting.h"
#include "WorldObjects/WorldObjectStepServices.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The 0x14f (PlasmaJoints.cpp): creation 0x530ddd..0x530ed8 and step 0x535b37..0x535d4a, with 0x5026a4's request
// (0x530860(0x14f, point, target + 0x10, turn, 0x4f2230(o), target, 100, −1, 0, 0, −1)). The step's expected values
// repeat the ASM formulas with another CrtRandom of the same seed.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x14fu;

using Client::Math::D;
using Client::Math::F32;

struct PlasmaWorld
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype, float width,
                                                                           void* owner)
    {
        glm::vec3 from = start;
        glm::vec3 to = second;
        glm::vec3 angles{};
        const Client::Kernel::JointRequest request{.type = 0x14f,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = owner,
                                                   .scale = width,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, nullptr, particles.get(), 0.0f, nullptr, lighting.get());
    }

    glm::vec3 start{1000.0f, 2000.0f, 300.0f};
    glm::vec3 second{1000.0f, 1500.0f, 220.0f};
    std::vector<JointState> joints = std::vector<JointState>(2);
    std::unique_ptr<Client::Particles::Particles> particles = std::make_unique<Client::Particles::Particles>();
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
    CrtRandom random{Seed};
};

// 0x535c26..0x535c77: the radius 2 dynamic light with ((rand() & 3) + 4) · 0.04f · 0.4f, by the bits of each draw
// (from a separate computation: each product at 53 bits and the fst to float).
constexpr std::array<std::uint32_t, 4> GlowBits{0x3d83126fu, 0x3da3d70au, 0x3dc49ba6u, 0x3de56041u};

// One round of the step with `replay`: the turn toward the target, the shake and, if it does not arrive, the light (in
// `lighting`, if given) and the advance. Returns whether it arrived.
bool Turn(JointState& joint, CrtRandom& replay, Client::Terrain::TerrainLighting* lighting = nullptr)
{
    const float distance = F32(Client::Math::MoveToward(joint.position, joint.angles, joint.targetPosition, 50.0f));
    joint.velocity.x = F32(static_cast<double>((replay.Next() & 0x3ff) - 0x200) / D(joint.width));
    const double turn = static_cast<double>((replay.Next() & 0x3ff) - 0x200) / D(joint.width);
    joint.velocity.z = F32(turn);
    const glm::vec3 shaken{F32(D(joint.angles.x) + D(joint.velocity.x)), F32(D(joint.angles.y) + D(joint.velocity.y)),
                           F32(turn + D(joint.angles.z))};
    if (D(joint.speed) * 1.5 > D(distance))
        return true;
    const auto shade = static_cast<std::size_t>(replay.Next() & 3);
    if (lighting != nullptr)
    {
        const glm::vec3 glow(std::bit_cast<float>(GlowBits[shade]));
        EXPECT_TRUE(Client::WorldObjects::AddDynamicLight(lighting, joint.position, glow, 2, "prueba").HasValue());
    }
    const auto moved =
        Client::Math::OriginalVectorRotate({0.0f, -joint.speed, 0.0f}, Client::Math::OriginalAngleMatrix(shaken));
    for (int axis = 0; axis < 3; ++axis)
        joint.position[axis] = F32(D(moved[axis]) + D(joint.position[axis]));
    return false;
}
} // namespace

// 0x530ddd..0x530ed8: without rand(); speed 50, the repeated texture, the width, 50 points and life 20; texture and
// light by subtype (0..2 the 0x614, 3..5 the 0x632); another subtype keeps the prefix's (the type and the white light).
TEST(PlasmaJointsTest, EachSubtypeBornsWithItsColor)
{
    constexpr auto Bits = [](std::uint32_t bits) { return std::bit_cast<float>(bits); };
    struct Probe
    {
        std::int32_t subtype;
        std::uint16_t texture;
        glm::vec3 light;
    };
    for (const Probe& probe :
         {Probe{0, 0x614, {Bits(0x3f333333u), 1.0f, Bits(0x3f333333u)}},
          Probe{1, 0x614, {1.0f, Bits(0x3f19999au), Bits(0x3f19999au)}},
          Probe{2, 0x614, {Bits(0x3f333333u), Bits(0x3f333333u), 1.0f}},
          Probe{3, 0x632, {Bits(0x3dcccccdu), Bits(0x3f4ccccdu), Bits(0x3dcccccdu)}},
          Probe{4, 0x632, {1.0f, Bits(0x3e99999au), Bits(0x3e4ccccdu)}},
          Probe{5, 0x632, {Bits(0x3e4ccccdu), Bits(0x3e99999au), 1.0f}}, Probe{6, 0x14f, glm::vec3(1.0f)}})
    {
        SCOPED_TRACE(probe.subtype);
        PlasmaWorld tested;
        ActorState target{};
        ASSERT_TRUE(tested.Create(probe.subtype, 100.0f, &target).HasValue());
        EXPECT_EQ(tested.random.State(), CrtRandom{Seed}.State());
        const JointState& joint = tested.joints[0];
        EXPECT_EQ(joint.texture, probe.texture);
        EXPECT_EQ(joint.light, probe.light);
        EXPECT_EQ(joint.speed, 50.0f);
        EXPECT_TRUE(joint.repeatedTexture);
        EXPECT_TRUE(joint.tailEnabled);
        EXPECT_EQ(joint.width, 100.0f);
        EXPECT_EQ(joint.maximumCount, 50);
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.ownerContext, &target);
    }
}

// 0x535b37..0x535cd4: after the common movement (50 by its angles), one round per point: the target is the owner 80
// higher, it turns toward it by 50 (0x4e9810), draws vx and vz ((rand() & 0x3ff) − 512) / width, adds the segment and,
// if it does not arrive (speed · 1.5 against the distance), with width 50 or more leaves the dynamic light and
// advances. With width 100, on arrival it drops nothing. The common tail follows.
TEST(PlasmaJointsTest, ThePlasmaTurnsTowardItsOwnerUntilItArrives)
{
    PlasmaWorld tested;
    ActorState target{};
    target.position = {1000.0f, 1500.0f, 220.0f};
    ASSERT_TRUE(tested.Create(0, 100.0f, &target).HasValue());
    JointState expected = tested.joints[0];

    ASSERT_TRUE(tested.Step().HasValue());

    CrtRandom replay{Seed};
    const auto first =
        Client::Math::OriginalVectorRotate({0.0f, -50.0f, 0.0f}, Client::Math::OriginalAngleMatrix(expected.angles));
    for (int axis = 0; axis < 3; ++axis)
        expected.position[axis] = F32(D(expected.position[axis]) + D(first[axis]));
    expected.targetPosition = {1000.0f, 1500.0f, 300.0f};
    auto lit = std::make_unique<Client::Terrain::TerrainLighting>();
    int turns = 0;
    for (; turns < 50; ++turns)
        if (Turn(expected, replay, lit.get()))
            break;
    ASSERT_LT(turns, 50);
    EXPECT_GT(turns, 3);
    const JointState& joint = tested.joints[0];
    EXPECT_EQ(tested.random.State(), replay.State());
    EXPECT_EQ(joint.targetPosition, expected.targetPosition);
    EXPECT_EQ(joint.position, expected.position);
    EXPECT_EQ(joint.angles, expected.angles);
    EXPECT_EQ(joint.velocity, expected.velocity);
    // The dynamic light of each round that does not arrive, at the position before advancing.
    EXPECT_TRUE(lit->cells == tested.lighting->cells);
    const auto dark = std::make_unique<Client::Terrain::TerrainLighting>();
    EXPECT_FALSE(lit->cells == dark->cells);
    // One segment per round (the arriving one too) and the common tail's.
    EXPECT_EQ(joint.count, turns + 2);
    EXPECT_EQ(joint.life, 19);
    EXPECT_EQ(std::ranges::count_if(tested.particles->All(),
                                    [](const Client::Particles::Particle& particle) { return particle.alive; }),
              0);
}

// 0x535cdf..0x535d4a: with the width at exactly 50, on arrival the particle 0x5ca and, with rand() % 8 at 0, three
// unused rand() and the 0x5f2/0. Without an owner, the target is the creation's second point.
TEST(PlasmaJointsTest, TheFiftyWidePlasmaSparksWhenItArrives)
{
    int smokes = 0;
    for (std::uint32_t seed = Seed; seed < Seed + 16; ++seed)
    {
        SCOPED_TRACE(seed);
        PlasmaWorld tested;
        tested.random = CrtRandom{seed};
        tested.second = {1000.0f, 1960.0f, 300.0f};
        ASSERT_TRUE(tested.Create(0, 50.0f, nullptr).HasValue());
        JointState expected = tested.joints[0];
        ASSERT_TRUE(tested.Step().HasValue());

        CrtRandom replay{seed};
        const auto first = Client::Math::OriginalVectorRotate({0.0f, -50.0f, 0.0f},
                                                              Client::Math::OriginalAngleMatrix(expected.angles));
        for (int axis = 0; axis < 3; ++axis)
            expected.position[axis] = F32(D(expected.position[axis]) + D(first[axis]));
        ASSERT_TRUE(Turn(expected, replay));
        auto twin = std::make_unique<Client::Particles::Particles>();
        ASSERT_TRUE(
            twin->CreateChecked(0x5ca, expected.position, expected.angles, expected.light, 0, 1.0f, 0.0f, replay)
                .HasValue());
        const bool smoke = replay.Next() % 8 == 0;
        if (smoke)
        {
            ++smokes;
            for (int unused = 0; unused < 3; ++unused)
                static_cast<void>(replay.Next());
            ASSERT_TRUE(
                twin->CreateChecked(0x5f2, expected.position, expected.angles, expected.light, 0, 1.0f, 0.0f, replay)
                    .HasValue());
        }
        EXPECT_EQ(tested.random.State(), replay.State());
        EXPECT_EQ(tested.joints[0].targetPosition, tested.second);
        EXPECT_EQ(tested.joints[0].position, expected.position);
        const auto count = [&tested](std::int32_t type)
        {
            return std::ranges::count_if(tested.particles->All(), [type](const Client::Particles::Particle& particle)
                                         { return particle.alive && particle.type == type; });
        };
        EXPECT_EQ(count(0x5ca), 1);
        EXPECT_EQ(count(0x5f2), smoke ? 1 : 0);
    }
    EXPECT_GT(smokes, 0);
}
