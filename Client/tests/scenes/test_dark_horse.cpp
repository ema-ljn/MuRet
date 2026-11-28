#include "Pets/DarkHorse.h"
#include "Kernel/HelperTable.h"

#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Models/Skeleton.h"

#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <ios>
#include <map>
#include <vector>

// Client::Kernel::DarkHorse (0xbf), created by 0x443d70 and moved by 0x444010 each step
// (Client/docs/research/06-render-escenas-y-ui.md, "El caballo oscuro").
namespace
{
using Client::Kernel::DarkHorse;
using Client::Models::ActionInfo;
using Client::Models::BoneKey;
using Client::Models::ObjectTransform;
using Client::Models::Skeleton;
using Client::Models::SkeletonBone;
using Client::Pets::DarkHorseGait;
using Client::Pets::DarkHorseGaitOf;
using Client::Pets::DarkHorseLoginScale;
using Client::Pets::DarkHorseModel;
using Client::Pets::DrawnDarkHorse;
using Client::Pets::StepDarkHorse;

// DarkHorse.bmd's seven actions, 10 keys each, on one bone.
Skeleton HorseSkeleton()
{
    Skeleton skeleton;
    SkeletonBone bone;
    bone.actions.assign(7, std::vector<BoneKey>(10, BoneKey{}));
    for (std::vector<BoneKey>& keys : bone.actions)
    {
        for (BoneKey& key : keys)
        {
            key.rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
        }
    }
    skeleton.bones = {bone};
    skeleton.actions.assign(7, ActionInfo{.repeats = true, .speed = 0.0f, .keyCount = 10, .lockPositions = false});
    return skeleton;
}

void ExpectGait(std::uint8_t rider, std::uint8_t action, float speed)
{
    const DarkHorseGait gait = DarkHorseGaitOf(rider);
    EXPECT_EQ(gait.action, action) << std::hex << static_cast<int>(rider);
    EXPECT_FLOAT_EQ(gait.speed, speed) << std::hex << static_cast<int>(rider);
}

// 0x444010 with 0xbf: the horse's action from the rider's (+0x111), with the speed it writes to +0xd8.
TEST(DarkHorseTest, TheHorseFollowsItsRider)
{
    ExpectGait(0x53, 3, 0.34f);
    ExpectGait(0x4b, 1, 0.34f);
    ExpectGait(0x4e, 2, 0.34f);
    ExpectGait(0x52, 2, 0.34f);
    ExpectGait(0x54, 5, 1.0f);
    ExpectGait(0x55, 6, 1.0f);
    // The login's mounted standing action (0x4a) and the ranges' neighbors: 0 at 0.3.
    ExpectGait(0x4a, 0, 0.3f);
    ExpectGait(0x4c, 0, 0.3f);
    ExpectGait(0x4d, 0, 0.3f);
    ExpectGait(0x56, 0, 0.3f);
    ExpectGait(0x00, 0, 0.3f);
}

// 0x443d70 creates it invisible, with alpha at 0 and speed at 0.5; 0x4e9980 raises it each step by a tenth of what is
// left to 1: 0.1, 0.19, … and passes 0.99 (opaque from there) at step 44.
TEST(DarkHorseTest, TheHorseFadesInByATenthOfWhatIsLeft)
{
    const Skeleton skeleton = HorseSkeleton();
    DarkHorse horse;
    EXPECT_FLOAT_EQ(horse.alpha, 0.0f);
    EXPECT_FLOAT_EQ(horse.speed, 0.5f);

    StepDarkHorse(horse, skeleton, 0x4a);
    EXPECT_FLOAT_EQ(horse.alpha, 0.1f);
    StepDarkHorse(horse, skeleton, 0x4a);
    EXPECT_FLOAT_EQ(horse.alpha, 0.19f);
    for (int step = 3; step <= 43; ++step)
    {
        StepDarkHorse(horse, skeleton, 0x4a);
    }
    EXPECT_LT(horse.alpha, 0.99f);
    StepDarkHorse(horse, skeleton, 0x4a);
    EXPECT_GE(horse.alpha, 0.99f);
}

// 0x4e9a0c..0x4e9a1e: the subtraction, the product by 0.1f and the sum run on the x87, with a single rounding to float
// at the end (fstp). Rounding each operation gives another bit from step 5 (0x3ed1ab4c, not 0x3ed1ab4b).
TEST(DarkHorseTest, TheFadeRoundsOnceLikeTheBinary)
{
    const Skeleton skeleton = HorseSkeleton();
    DarkHorse horse;
    const std::map<int, std::uint32_t> expected{{1, 0x3dcccccdU},  {2, 0x3e428f5cU},  {5, 0x3ed1ab4bU},
                                                {10, 0x3f26bd03U}, {44, 0x3f7d8474U}, {60, 0x3f7f8a3dU}};
    for (int step = 1; step <= 60; ++step)
    {
        StepDarkHorse(horse, skeleton, 0x4a);
        if (const auto bits = expected.find(step); bits != expected.end())
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(horse.alpha), bits->second) << "paso " << step;
        }
    }
}

// Each step, the action the rider gives (0x4ea030: the old one becomes the previous and the frame goes to 0) and the
// advance at its speed (0x4ec200).
TEST(DarkHorseTest, AStepTakesTheActionOfTheRiderAndAdvancesAtItsSpeed)
{
    const Skeleton skeleton = HorseSkeleton();
    DarkHorse horse;

    StepDarkHorse(horse, skeleton, 0x4a);
    EXPECT_EQ(horse.animation.action, 0u);
    EXPECT_FLOAT_EQ(horse.animation.frame, 0.3f);
    EXPECT_FLOAT_EQ(horse.speed, 0.3f);

    StepDarkHorse(horse, skeleton, 0x54);
    EXPECT_EQ(horse.animation.action, 5u);
    EXPECT_FLOAT_EQ(horse.animation.frame, 1.0f);
    EXPECT_FLOAT_EQ(horse.speed, 1.0f);

    StepDarkHorse(horse, skeleton, 0x54);
    EXPECT_FLOAT_EQ(horse.animation.frame, 2.0f);
}

// 0x4440b8..0x4440f8: on the rider's safe cell the alpha rises and goes back to 0, and the horse advances with the
// action and speed it had, ignoring the rider's. On leaving, it rises again from 0.1.
TEST(DarkHorseTest, InTheRidersSafeCellTheAlphaGoesToZeroAndKeepsTheSpeed)
{
    const Skeleton skeleton = HorseSkeleton();
    DarkHorse horse;
    StepDarkHorse(horse, skeleton, 0x54, false);
    ASSERT_EQ(horse.animation.action, 5u);
    ASSERT_FLOAT_EQ(horse.speed, 1.0f);
    ASSERT_FLOAT_EQ(horse.alpha, 0.1f);

    StepDarkHorse(horse, skeleton, 0x53, true);

    EXPECT_FLOAT_EQ(horse.alpha, 0.0f);
    EXPECT_EQ(horse.animation.action, 5u);
    EXPECT_FLOAT_EQ(horse.speed, 1.0f);
    EXPECT_FLOAT_EQ(horse.animation.frame, 2.0f);

    StepDarkHorse(horse, skeleton, 0x53, false);

    EXPECT_FLOAT_EQ(horse.alpha, 0.1f);
    EXPECT_EQ(horse.animation.action, 3u);
    EXPECT_FLOAT_EQ(horse.speed, 0.34f);
}

// What is drawn: 0xbf, at the rider's place and angles, with 0x445260's scale, its alpha, the per-vertex light and the
// animation a bit past the step.
TEST(DarkHorseTest, TheHorseIsDrawnWhereItsRiderIs)
{
    const Skeleton skeleton = HorseSkeleton();
    DarkHorse horse;
    StepDarkHorse(horse, skeleton, 0x4a);
    ObjectTransform rider;
    rider.position = {10603.0f, 14112.0f, 580.0f};
    rider.angles = {0.0f, 0.0f, 188.0f};
    rider.scale = 1.6f;

    const auto drawn = DrawnDarkHorse(horse, &skeleton, rider, DarkHorseLoginScale, 0.5f);

    EXPECT_EQ(drawn.model, DarkHorseModel);
    EXPECT_EQ(drawn.transform.position, rider.position);
    EXPECT_EQ(drawn.transform.angles, rider.angles);
    EXPECT_FLOAT_EQ(drawn.transform.scale, 1.6f);
    EXPECT_FLOAT_EQ(drawn.alpha, 0.1f);
    EXPECT_TRUE(drawn.vertexLight);
    EXPECT_FLOAT_EQ(drawn.animation.frame, 0.3f + 0.5f * 0.3f);
    // Without the skeleton, the step's animation.
    EXPECT_FLOAT_EQ(DrawnDarkHorse(horse, nullptr, rider, 1.0f, 0.5f).animation.frame, 0.3f);
    EXPECT_FLOAT_EQ(DrawnDarkHorse(horse, nullptr, rider, 1.0f, 0.5f).transform.scale, 1.0f);
}
} // namespace
