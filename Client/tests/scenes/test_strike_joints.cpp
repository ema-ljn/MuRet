#include "Joints/FaunaJointCreation.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Math/X87.h"
#include "Kernel/DrawTailState.h"
#include "Kernel/JointRequest.h"
#include "Particles/KalimaParticleKernels.h"
#include "Effects/ModelEffects.h"
#include "Particles/Particles.h"
#include "Particles/Sprites.h"

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The 0x615/5 of Kalima's 0x615 (0x52b560) and the /6 of the siege's 0x61c/1 pulse (0x52c31e and 0x52c342): the
// creation in 0x531b0c..0x531bfc and the step in 0x53768f..0x5379ef, shared with Chaos Castle's /2 and Crywolf's /7.
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;

constexpr std::uint32_t Seed = 0x615u;

using Client::Math::D;
using Client::Math::F32;

struct Strike
{
    Client::Core::Expected<std::optional<std::size_t>, std::string> Create(std::int32_t subtype)
    {
        glm::vec3 from = start;
        glm::vec3 turn{10.0f, 20.0f, 30.0f};
        const bool owned = subtype == 6;
        const Client::Kernel::JointRequest request{.type = 0x615,
                                                   .firstPosition = from,
                                                   .secondPosition = from,
                                                   .angles = turn,
                                                   .subtype = subtype,
                                                   .ownerContext = owned ? &owner : nullptr,
                                                   .scale = 80.0f,
                                                   .key = -1,
                                                   .skill = 0,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random,
                                                owned ? JointOwnerKind::ModelEffect : JointOwnerKind::ByType);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, nullptr, particles.get(), 0.0f, sprites.get(), nullptr, {},
                                          [this](float, float) { return ground; });
    }

    std::vector<JointState> joints = std::vector<JointState>(2);
    CrtRandom random{Seed};
    std::unique_ptr<Client::Particles::Particles> particles = std::make_unique<Client::Particles::Particles>();
    std::unique_ptr<Client::Particles::FrameSprites> sprites = std::make_unique<Client::Particles::FrameSprites>();
    Client::Effects::ModelEffect owner{};
    glm::vec3 start{1000.0f, 2000.0f, 300.0f};
    double ground = 250.0;
};
} // namespace

// 0x531b0c..0x531bfc: texture 0x614, 50 points, life 20, x and y shifted rand() % 10 − 5, 800 higher, the angles at 0,
// the target at the second point and +0x24 at the shifted position. The /5 with the light (1; 0.5; 0.2f); the /6 with
// speed 0.3f, the light at 1 and another 700 higher without touching +0x24. Two rand().
TEST(StrikeJointsTest, EachBoltIsBornAboveItsPoint)
{
    for (const std::int32_t subtype : {5, 6})
    {
        SCOPED_TRACE(subtype);
        Strike strike;
        ASSERT_TRUE(strike.Create(subtype).HasValue());
        CrtRandom expected{Seed};
        const int x = expected.Next() % 10 - 5;
        const int y = expected.Next() % 10 - 5;
        EXPECT_EQ(strike.random.State(), expected.State());
        const JointState& joint = strike.joints[0];
        const glm::vec3 shifted{F32(static_cast<double>(x) + D(strike.start.x)),
                                F32(static_cast<double>(y) + D(strike.start.y)), F32(D(strike.start.z) + 800.0)};
        EXPECT_EQ(joint.texture, 0x614);
        EXPECT_EQ(joint.width, 80.0f);
        EXPECT_EQ(joint.maximumCount, 50);
        EXPECT_EQ(joint.life, 20);
        EXPECT_EQ(joint.angles, glm::vec3(0.0f));
        EXPECT_EQ(joint.targetPosition, strike.start);
        EXPECT_EQ(joint.pathIncrement, shifted);
        EXPECT_TRUE(joint.tailEnabled);
        if (subtype == 5)
        {
            EXPECT_EQ(joint.position, shifted);
            EXPECT_EQ(joint.speed, 0.0f);
            EXPECT_EQ(joint.light, glm::vec3(1.0f, 0.5f, std::bit_cast<float>(0x3e4ccccdu)));
            EXPECT_EQ(joint.ownerContext, nullptr);
        }
        else
        {
            EXPECT_EQ(joint.position, glm::vec3(shifted.x, shifted.y, F32(D(shifted.z) + 700.0)));
            EXPECT_EQ(joint.speed, std::bit_cast<float>(0x3e99999au));
            EXPECT_EQ(joint.light, glm::vec3(1.0f));
            EXPECT_EQ(joint.ownerContext, &strike.owner);
        }
    }
}

// 0x53768f..0x5379ef: the bolt is rebuilt from +0x24: 45 segments that shift x and y by rand() % 20 − 10 and lower z
// (16 the /5, 13 the /6), four that approach the target, and the target. The /5 drops nothing; the /6 puts the target's
// z 30 above the ground (0x5f93c0) and leaves there the particle 0x6ed/0 of scale 2 with its angles. With life over 10,
// the common tail adds segment 51 (0x53c393).
TEST(StrikeJointsTest, TheBoltRedrawsItsPathEachStep)
{
    for (const std::int32_t subtype : {5, 6})
    {
        SCOPED_TRACE(subtype);
        Strike strike;
        ASSERT_TRUE(strike.Create(subtype).HasValue());
        const JointState& joint = strike.joints[0];
        const glm::vec3 from = joint.pathIncrement;
        const CrtRandom before = strike.random;
        CrtRandom expected = strike.random;

        ASSERT_TRUE(strike.Step().HasValue());

        // Two per segment of the descent and of the arrival and, with the /6, the two of the 0x6ed's speed (0x543b82).
        for (int draw = 0; draw < 45 * 2 + 4 * 2 + (subtype == 6 ? 2 : 0); ++draw)
            static_cast<void>(expected.Next());
        EXPECT_EQ(strike.random.State(), expected.State());
        // Each descent segment shifts x and then y by rand() % 20 − 10 (summed in the x87) and lowers z.
        const double drop = subtype == 6 ? 13.0 : 16.0;
        CrtRandom jitter = before;
        glm::vec3 lowest = from;
        for (int tramo = 0; tramo < 45; ++tramo)
        {
            lowest.x = F32(static_cast<double>(jitter.Next() % 20 - 10) + D(lowest.x));
            lowest.y = F32(static_cast<double>(jitter.Next() % 20 - 10) + D(lowest.y));
            lowest.z = F32(D(lowest.z) - drop);
        }
        // Segment 45 (the last of the descent) ended up six slots back: the four of the arrival, the target and the
        // tail. With the angles at 0, its point 2 is the center 40 lower and point 0, 40 to the left.
        EXPECT_EQ(joint.points[6][2].x, lowest.x);
        EXPECT_EQ(joint.points[6][2].y, lowest.y);
        EXPECT_EQ(joint.points[6][0].z, lowest.z);
        EXPECT_NE(lowest.x, from.x);
        EXPECT_NE(lowest.y, from.y);
        EXPECT_EQ(joint.count, 49);
        EXPECT_EQ(joint.life, 19);
        EXPECT_TRUE(joint.tailEnabled);
        const auto made =
            std::ranges::count_if(strike.particles->All(), [](const auto& particle)
                                  { return particle.alive && particle.type == Client::Particles::KalimaFanta; });
        if (subtype == 5)
        {
            EXPECT_EQ(made, 0);
            EXPECT_EQ(joint.position, strike.start);
            continue;
        }
        // The target drops to 30 above the ground after being copied into the position (0x53782a), which does not
        // follow it.
        const glm::vec3 landed{strike.start.x, strike.start.y, 280.0f};
        EXPECT_EQ(joint.targetPosition, landed);
        EXPECT_EQ(joint.position, strike.start);
        ASSERT_EQ(made, 1);
        const auto& spark = strike.particles->All().front();
        EXPECT_EQ(spark.subtype, 0);
        EXPECT_EQ(spark.scale, 2.0f);
        EXPECT_EQ(spark.position, landed);
    }
}

// 0x5379d3..0x5379ef: with life 10 or less, the /5 and the /6 turn off the tail (no 0x53c393 segment); under 4, the
// light times 0.8333333f.
TEST(StrikeJointsTest, TheBoltFadesAndStopsItsTail)
{
    for (const std::int32_t subtype : {5, 6})
    {
        SCOPED_TRACE(subtype);
        Strike strike;
        ASSERT_TRUE(strike.Create(subtype).HasValue());
        JointState& joint = strike.joints[0];
        joint.life = 3;
        const glm::vec3 light = joint.light;

        ASSERT_TRUE(strike.Step().HasValue());

        EXPECT_FALSE(joint.tailEnabled);
        EXPECT_EQ(joint.life, 2);
        const double fade = D(std::bit_cast<float>(0x3f555555u));
        EXPECT_EQ(joint.light, glm::vec3(F32(D(light.x) * fade), F32(D(light.y) * fade), F32(D(light.z) * fade)));
    }
}
