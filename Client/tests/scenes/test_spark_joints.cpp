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

#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// The 0x610/5 and the 0x60f/0x29 and /0x2a (SparkJoints.cpp): the sparks of Kalima's 0x196 (0x43e904) and the one
// circling NPC 257 (0x5165d1) with those it drops (0x53a0eb). 06, "Los joints de las familias".
namespace
{
using Client::Crt::CrtRandom;
using Client::Kernel::ActorState;
using Client::Kernel::JointOwnerKind;
using Client::Kernel::JointState;
using Client::Math::OriginalAngleMatrix;
using Client::Math::OriginalVectorRotate;

constexpr std::uint32_t Seed = 0x5ba4u;

using Client::Math::D;
using Client::Math::F32;

glm::vec3 Sum(const glm::vec3& first, const glm::vec3& second)
{
    return {F32(D(first.x) + D(second.x)), F32(D(first.y) + D(second.y)), F32(D(first.z) + D(second.z))};
}

struct Pool
{
    Client::Core::Expected<std::optional<std::size_t>, std::string>
    Create(std::int32_t type, std::int32_t subtype, glm::vec3 from, glm::vec3 to, float width, void* owner = nullptr,
           JointOwnerKind kind = JointOwnerKind::ByType, std::int32_t skill = 0)
    {
        glm::vec3 angles = startAngles;
        const Client::Kernel::JointRequest request{.type = type,
                                                   .firstPosition = from,
                                                   .secondPosition = to,
                                                   .angles = angles,
                                                   .subtype = subtype,
                                                   .ownerContext = owner,
                                                   .scale = width,
                                                   .key = -1,
                                                   .skill = skill,
                                                   .skillSerial = 0,
                                                   .target = -1};
        return Client::Joints::CreateFaunaJoint(joints, request, random, kind);
    }

    Client::Core::Expected<void, std::string> Step()
    {
        return Client::Joints::StepJoints(joints, random, nullptr, nullptr, 0.0f, nullptr, lighting.get());
    }

    std::vector<JointState> joints = std::vector<JointState>(4);
    CrtRandom random{Seed};
    glm::vec3 startAngles{};
    std::unique_ptr<Client::Terrain::TerrainLighting> lighting = std::make_unique<Client::Terrain::TerrainLighting>();
};

// A seed whose rand() from `index` satisfy `wanted`.
template <typename Wanted> std::uint32_t SeedWhere(Wanted wanted)
{
    for (std::uint32_t seed = 1; seed < 100000; ++seed)
    {
        CrtRandom probe(seed);
        if (wanted(probe))
            return seed;
    }
    return 0;
}

// 0x531c43..0x531cc7 and 0x531dd0..0x531dff: the life in the key and then 50, 8 points, speed 3, blend 4, the second
// face and pass 2, the light at 1 and the skill's width (20 with 0 and 3, 40 with 1, the retained one with another);
// color 3 without an owner stops. Without rand().
TEST(SparkJointsTest, TheHelperSparkTakesItsWidthFromTheSkillColor)
{
    for (const auto& [skill, width] : {std::pair{0, 20.0f}, std::pair{1, 40.0f}, std::pair{2, 7.5f}})
    {
        SCOPED_TRACE(skill);
        Pool pool;
        pool.joints[0].width = 7.5f;
        ASSERT_TRUE(
            pool.Create(0x610, 5, {1, 2, 3}, {4, 5, 6}, 99.0f, nullptr, JointOwnerKind::ByType, skill).HasValue());
        const JointState& joint = pool.joints[0];
        EXPECT_EQ(joint.width, width);
        EXPECT_EQ(joint.life, 50);
        EXPECT_EQ(joint.key, 0);
        EXPECT_EQ(joint.maximumCount, 8);
        EXPECT_EQ(joint.speed, 3.0f);
        EXPECT_EQ(joint.blend, 4);
        EXPECT_EQ(joint.drawEnabled, 2);
        EXPECT_EQ(joint.category, 2);
        EXPECT_EQ(joint.light, glm::vec3(1.0f));
        EXPECT_EQ(joint.targetPosition, (glm::vec3{4, 5, 6}));
        EXPECT_EQ(pool.random.State(), Seed);
    }
    Pool colored;
    const auto cut = colored.Create(0x610, 5, {}, {}, 20.0f, nullptr, JointOwnerKind::ByType, 3);
    ASSERT_FALSE(cut.HasValue());
    EXPECT_TRUE(cut.Error().starts_with("0x531c7b")) << cut.Error();
}

// 0x537a00..0x537b31: with life 25 or more (after the common advance of 3), z turns −10 and the position rises 2.5;
// with less, it speeds up by 5 to 30, turns toward the target and, near it, with the heading changed by more than 20,
// brakes 10; with life under 10, the three lights are the red one times 0.8333333f. One rand() for the dynamic light.
TEST(SparkJointsTest, TheHelperSparkRisesThenChasesItsTarget)
{
    Pool pool;
    pool.startAngles = {10.0f, 0.0f, 50.0f};
    ASSERT_TRUE(pool.Create(0x610, 5, {100, 200, 300}, {130, 240, 300}, 20.0f).HasValue());
    JointState& joint = pool.joints[0];
    const glm::vec3 start = joint.position;
    CrtRandom twin = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    const glm::vec3 moved = Sum(start, OriginalVectorRotate({0.0f, -3.0f, 0.0f}, OriginalAngleMatrix({10, 0, 50})));
    EXPECT_EQ(joint.position, (glm::vec3{moved.x, moved.y, F32(D(moved.z) + 2.5)}));
    EXPECT_EQ(joint.angles, (glm::vec3{10.0f, 0.0f, 40.0f}));
    EXPECT_EQ(joint.speed, 3.0f);
    EXPECT_EQ(joint.life, 49);
    static_cast<void>(twin.Next());
    EXPECT_EQ(pool.random.State(), twin.State());

    // Near the target with speed at 27: 32 stays at 30, the turn changes more than 20 and it brakes to 20.
    joint.life = 8;
    joint.speed = 27.0f;
    joint.light = {0.6f, 0.2f, 0.3f};
    joint.angles = {0.0f, 0.0f, 300.0f};
    joint.position = {100.0f, 200.0f, 300.0f};
    joint.targetPosition = {110.0f, 220.0f, 300.0f};
    const glm::vec3 before =
        Sum(joint.position, OriginalVectorRotate({0.0f, -27.0f, 0.0f}, OriginalAngleMatrix({0.0f, 0.0f, 300.0f})));
    glm::vec3 turned = joint.angles;
    const double distance = Client::Math::MoveToward(before, turned, joint.targetPosition, 30.0f);
    ASSERT_LE(distance, 70.0);
    ASSERT_GT(std::abs(300.0 - D(turned.z)), 20.0);
    twin = pool.random;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(joint.position, before);
    EXPECT_EQ(joint.angles, turned);
    EXPECT_EQ(joint.speed, 20.0f);
    const float faded = F32(D(0.6f) * D(std::bit_cast<float>(0x3f555555u)));
    EXPECT_EQ(joint.light, glm::vec3(faded));
    static_cast<void>(twin.Next());
    EXPECT_EQ(pool.random.State(), twin.State());
}

// 0x5339a5..0x533a1e and 0x53a039..0x53a122: the 0x60f/0x2a is born (0, −15, 0) turned further and stays on that path
// with velocity (0, −50, 0), life 100, 20 points, the requested width and without the common tail. Each step turns z by
// 15, goes back to life 100 and sets itself on the path plus the turned velocity; it draws rand() % 100 and, with the
// ftol of z a multiple of 45 and the rand() at 50 or less, drops a 0x60f/0x29 (three rand()) at its position. With its
// owner dead it turns off its /0x2a.
TEST(SparkJointsTest, TheNpcSparkleCirclesAndDropsSparkles)
{
    ActorState owner;
    owner.alive = true;
    const std::uint32_t seed = SeedWhere([](CrtRandom& probe) { return probe.Next() % 100 <= 50; });
    ASSERT_NE(seed, 0u);
    Pool pool;
    pool.random = CrtRandom(seed);
    pool.startAngles = {0.0f, 0.0f, 30.0f};
    ASSERT_TRUE(
        pool.Create(0x60f, 0x2a, {500, 600, 700}, {500, 600, 700}, 15.0f, &owner, JointOwnerKind::Actor).HasValue());
    ActorState stranger;
    stranger.alive = true;
    ASSERT_TRUE(pool.Create(0x60f, 0x2a, {0, 0, 0}, {0, 0, 0}, 15.0f, &stranger, JointOwnerKind::Actor).HasValue());
    pool.joints[1].angles.z = 0.0f;
    JointState& joint = pool.joints[0];
    const glm::vec3 path =
        Sum(OriginalVectorRotate({0.0f, -15.0f, 0.0f}, OriginalAngleMatrix({0, 0, 30})), glm::vec3{500, 600, 700});
    EXPECT_EQ(joint.position, path);
    EXPECT_EQ(joint.pathIncrement, path);
    EXPECT_EQ(joint.velocity, (glm::vec3{0.0f, -50.0f, 0.0f}));
    EXPECT_EQ(joint.life, 100);
    EXPECT_EQ(joint.maximumCount, 20);
    EXPECT_EQ(joint.width, 15.0f);
    EXPECT_FALSE(joint.tailEnabled);
    EXPECT_EQ(joint.targetPosition, (glm::vec3{500, 600, 700}));
    EXPECT_EQ(pool.random.State(), seed);

    // The outsider turns from 0 to 15 (not a multiple of 45, drops nothing) and draws its rand() % 100 after the first.
    CrtRandom twin(seed);
    static_cast<void>(twin.Next());
    const auto first = twin.Next() % 300;
    const auto second = twin.Next() % 300;
    const auto lift = twin.Next() % 5 + 5;
    static_cast<void>(twin.Next());
    ASSERT_TRUE(pool.Step().HasValue());
    const glm::vec3 spun = Sum(OriginalVectorRotate({0.0f, -50.0f, 0.0f}, OriginalAngleMatrix({0, 0, 45})), path);
    EXPECT_EQ(joint.angles.z, 45.0f);
    EXPECT_EQ(joint.position, spun);
    EXPECT_EQ(joint.life, 99);
    EXPECT_EQ(pool.random.State(), twin.State());
    const JointState& sparkle = pool.joints[2];
    ASSERT_TRUE(sparkle.alive);
    EXPECT_EQ(sparkle.type, 0x60f);
    EXPECT_EQ(sparkle.subtype, 0x29);
    EXPECT_EQ(sparkle.ownerContext, nullptr);
    EXPECT_EQ(sparkle.angles, glm::vec3(0.0f));
    EXPECT_EQ(sparkle.position, spun);
    EXPECT_EQ(sparkle.targetPosition, spun);
    EXPECT_EQ(sparkle.width, 15.0f);
    EXPECT_EQ(sparkle.maximumCount, 50);
    const double thousandth = D(std::bit_cast<float>(0x3a83126fu));
    const float red = F32(static_cast<double>(first) * thousandth);
    EXPECT_EQ(sparkle.light, (glm::vec3{F32(1.0 - D(red)), F32(1.0 - static_cast<double>(second) * thousandth), 1}));
    EXPECT_EQ(sparkle.velocity.z, static_cast<float>(lift));
    // Born after the step's slot: in this same round it takes its step (life 39, without rand()).
    EXPECT_EQ(sparkle.life, 39);

    owner.alive = false;
    const auto state = pool.random.State();
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_FALSE(joint.alive);
    EXPECT_EQ(joint.angles.z, 60.0f);
    EXPECT_EQ(joint.position, spun);
    EXPECT_TRUE(pool.joints[1].alive);
    // Only the outsider's rand() % 100 (z at 30, it does not drop).
    CrtRandom after(state);
    static_cast<void>(after.Next());
    EXPECT_EQ(pool.random.State(), after.State());
}

// 0x539fc2..0x53a028 and 0x53b318: the 0x60f/0x29 with life 20 or less stays above the target and rises vz + 1 (vz
// grows by 1), with the light times 0.9090909f; under 10, also times 0.7692308f. Without rand().
TEST(SparkJointsTest, TheFallingSparkleRisesOverItsTarget)
{
    Pool pool;
    ASSERT_TRUE(pool.Create(0x60f, 0x29, {10, 20, 30}, {40, 50, 60}, 15.0f).HasValue());
    JointState& joint = pool.joints[0];
    EXPECT_EQ(joint.life, 40);
    const glm::vec3 light = joint.light;
    const auto state = pool.random.State();
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(joint.position, (glm::vec3{10, 20, 30}));
    EXPECT_EQ(joint.light, light);

    joint.life = 9;
    joint.velocity.z = 6.0f;
    ASSERT_TRUE(pool.Step().HasValue());
    EXPECT_EQ(joint.position, (glm::vec3{40.0f, 50.0f, 37.0f}));
    EXPECT_EQ(joint.velocity.z, 7.0f);
    const double gone = D(std::bit_cast<float>(0x3f68ba2eu));
    const double fade = D(std::bit_cast<float>(0x3f44ec4fu));
    EXPECT_EQ(joint.light.x, F32(D(F32(D(light.x) * gone)) * fade));
    EXPECT_EQ(joint.light.z, F32(D(F32(1.0 * gone)) * fade));
    EXPECT_EQ(joint.life, 8);
    EXPECT_EQ(pool.random.State(), state);
}
} // namespace
