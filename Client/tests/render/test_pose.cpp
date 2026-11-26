#include "Models/Pose.h"

#include "Math/HalfLifeMath.h"
#include "Math/RoundedMath.h"
#include "Models/Skeleton.h"

#include <gtest/gtest.h>

#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <numbers>
#include <vector>

// 0x4eb860 on a hand-built skeleton.
namespace
{
using Client::Math::AngleMatrix;
using Client::Math::AngleQuaternion;
using Client::Math::BoneTransform;
using Client::Math::QuaternionMatrix;
using Client::Math::QuaternionSlerp;
using Client::Math::RoundedCos;
using Client::Math::RoundedSin;
using Client::Math::VectorRotate;
using Client::Math::VectorTransform;
using Client::Models::ActionInfo;
using Client::Models::AnimationState;
using Client::Models::BoneKey;
using Client::Models::ObjectTransform;
using Client::Models::PoseSkeleton;
using Client::Models::PoseWorldSkeleton;
using Client::Models::Skeleton;
using Client::Models::SkeletonBone;

constexpr float Tolerance = 1e-5f;
constexpr glm::vec2 NoHead{0.0f, 0.0f};

float Radians(float degrees)
{
    return degrees * std::numbers::pi_v<float> / 180.0f;
}

BoneKey Key(glm::vec3 position, glm::vec3 degrees)
{
    const glm::vec3 radians{Radians(degrees.x), Radians(degrees.y), Radians(degrees.z)};
    return BoneKey{position, radians, AngleQuaternion(radians)};
}

// Three bones: a root, its child and a dummy. Action 0 has two keys; action 1, three and lock_positions. The root
// turns 90° about z from one key of action 0 to the other; the child is 10 in x from the root.
Skeleton SampleSkeleton()
{
    Skeleton skeleton;
    skeleton.actions = {ActionInfo{true, 0.0f, 2, false}, ActionInfo{true, 0.0f, 3, true}};
    SkeletonBone root;
    root.actions = {
        {Key({0, 0, 0}, {0, 0, 0}), Key({10, 0, 0}, {0, 0, 90})},
        {Key({1, 2, 3}, {0, 0, 0}), Key({4, 5, 6}, {0, 0, 0}), Key({7, 8, 9}, {0, 0, 0})},
    };
    SkeletonBone child;
    child.parent = 0;
    child.actions = {
        {Key({10, 0, 0}, {0, 0, 0}), Key({10, 0, 0}, {0, 0, 0})},
        {Key({10, 0, 0}, {0, 0, 0}), Key({20, 0, 0}, {0, 0, 0}), Key({30, 0, 0}, {0, 0, 0})},
    };
    SkeletonBone dummy;
    dummy.dummy = true;
    skeleton.bones = {root, child, dummy};
    return skeleton;
}

void ExpectNear(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, Tolerance);
    EXPECT_NEAR(actual.y, expected.y, Tolerance);
    EXPECT_NEAR(actual.z, expected.z, Tolerance);
}

std::vector<BoneTransform> Pose(const Skeleton& skeleton, const AnimationState& animation,
                                const ObjectTransform& object = {}, glm::vec2 headAngles = NoHead)
{
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(0.0f));
    EXPECT_TRUE(PoseSkeleton(skeleton, animation, object, headAngles, bones));
    return bones;
}

// Its relative position turns with the root.
TEST(PoseTest, AChildFollowsItsParent)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.bones[0].actions[0][0] = Key({0, 0, 0}, {0, 0, 90});

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{});

    ExpectNear(bones[1][3], {0, 10, 0});
    ExpectNear(VectorTransform({1, 0, 0}, bones[1]), {0, 11, 0});
}

// The root turns with the object's angles, in degrees. The object's scale and position do not enter the bones: SkinMesh
// applies them. The key's position goes in the translation, m[3].
TEST(PoseTest, TheRootTurnsWithTheObjectButDoesNotMoveWithIt)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.bones[0].actions[0][0] = Key({5, 0, 0}, {0, 0, 0});
    const ObjectTransform object{{100, 200, 300}, {0, 0, 90}, 2.0f, 0.0f};

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{}, object);

    ExpectNear(bones[0][3], {0, 5, 0});
    ExpectNear(VectorRotate({1, 0, 0}, bones[0]), {0, 1, 0});
}

// The world root scales its nine orientation components and keeps the object's position unscaled; then it
// concatenates the local pose before posing the child.
TEST(PoseTest, WorldRootScalesAndTranslatesBeforePosingChildren)
{
    Skeleton skeleton;
    skeleton.actions = {ActionInfo{true, 0.0f, 1, false}};
    SkeletonBone root;
    root.actions = {{Key({1, 0, 0}, {0, 0, 0})}};
    SkeletonBone child;
    child.parent = 0;
    child.actions = {{Key({1, 2, 3}, {0, 0, 90})}};
    SkeletonBone dummy;
    dummy.dummy = true;
    skeleton.bones = {root, child, dummy};
    const ObjectTransform object{{10, 20, 30}, {0, 0, 90}, 2.0f, 0.0f};
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(7.0f));

    ASSERT_TRUE(PoseWorldSkeleton(skeleton, AnimationState{}, object, NoHead, bones));

    // Root: object.position + 2 * Rz(90°) * (1, 0, 0).
    ExpectNear(bones[0][3], {10, 22, 30});
    ExpectNear(bones[0][0], {0, 2, 0});
    ExpectNear(bones[0][1], {-2, 0, 0});
    ExpectNear(bones[0][2], {0, 0, 2});
    // Child translation: root translation + 2 * Rz(90°) * (1, 2, 3).
    ExpectNear(bones[1][3], {6, 24, 36});
    // Its local Rz(90°) is concatenated with the world Rz(90°), including the world scale.
    ExpectNear(bones[1][0], {-2, 0, 0});
    ExpectNear(bones[1][1], {0, -2, 0});
    ExpectNear(bones[1][2], {0, 0, 2});
    EXPECT_EQ(bones[2], BoneTransform(7.0f));
}

// lock_positions computes bone 0's local position before the world transform is applied.
TEST(PoseTest, WorldRootAppliesScaleAndPositionToLockedRoot)
{
    Skeleton skeleton = SampleSkeleton();
    const AnimationState animation{1, 1.5f, 1, 0};
    ObjectTransform object{{100, 200, 300}, {0, 0, 90}, 2.0f, 2.0f};
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(7.0f));

    ASSERT_TRUE(PoseWorldSkeleton(skeleton, animation, object, NoHead, bones));

    // lock_positions keeps x/y of key zero and adds 2 to the interpolated z (six and a half).
    // The world root rotates and scales that vector, then adds the unscaled position.
    ExpectNear(bones[0][3], {96.0f, 202.0f, 313.0f});
    EXPECT_EQ(bones[2], BoneTransform(7.0f));
}

// Between the previous key and the current one, with the fractional part: a quarter of the way from 0° to 90° is
// 22.5°, and the position goes linearly.
TEST(PoseTest, InterpolatesBetweenThePreviousKeyAndTheCurrent)
{
    const AnimationState animation{0, 1.25f, 0, 0};

    const std::vector<BoneTransform> bones = Pose(SampleSkeleton(), animation);

    ExpectNear(bones[0][3], {2.5f, 0, 0});
    ExpectNear(VectorRotate({1, 0, 0}, bones[0]), {RoundedCos(Radians(22.5f)), RoundedSin(Radians(22.5f)), 0});
}

// The previous key can belong to another action: the one the advance stored when changing.
TEST(PoseTest, ThePreviousKeyCanBeOfAnotherAction)
{
    const AnimationState animation{0, 0.5f, 1, 2};
    Skeleton skeleton = SampleSkeleton();
    skeleton.actions[1].lockPositions = false;

    const std::vector<BoneTransform> bones = Pose(skeleton, animation);

    // From key 2 of action 1 (7, 8, 9) to key 0 of action 0 (0, 0, 0), halfway.
    ExpectNear(bones[0][3], {3.5f, 4.0f, 4.5f});
}

// The head bone subtracts headAngles.x from its x and headAngles.y from its z (not from its y), in degrees.
TEST(PoseTest, TheHeadSubtractsXAndZ)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.headBone = 1;

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{}, {}, {30.0f, 40.0f});

    const BoneTransform expected = AngleMatrix({-30.0f, 0.0f, -40.0f});
    for (int column = 0; column < 3; ++column)
    {
        ExpectNear(bones[1][column], expected[column]);
    }
    ExpectNear(VectorRotate({1, 0, 0}, bones[0]), {1, 0, 0});
}

// The head converts its degrees to radians with the float at 0x6ba140 (0x3c8efa36), one ulp above the one nearest to
// π/180, which is the root's (0x6ba360). With 3600° the difference shows.
TEST(PoseTest, TheHeadUsesTheDegreeFloatOfTheOriginal)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.headBone = 1;
    constexpr float HeadDegreesToRadians = std::bit_cast<float>(0x3c8efa36u);

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{}, {}, {3600.0f, 0.0f});

    const BoneTransform expected =
        QuaternionMatrix(AngleQuaternion({0.0f - 3600.0f * HeadDegreesToRadians, 0.0f, 0.0f}));
    for (int column = 0; column < 3; ++column)
    {
        EXPECT_EQ(bones[1][column], expected[column]) << "columna " << column;
    }
}

// With lock_positions, bone 0 takes x and y from key 0 of the current action, and its z is the interpolated one plus
// the model's height (+0x84).
TEST(PoseTest, LockPositionsTakesXAndYFromKeyZero)
{
    const AnimationState animation{1, 2.5f, 1, 1};
    ObjectTransform object;
    object.lockedRootHeight = 10.0f;

    const std::vector<BoneTransform> bones = Pose(SampleSkeleton(), animation, object);

    // x and y from (1, 2, 3); z from 6 to 9 halfway, plus 10.
    ExpectNear(bones[0][3], {1.0f, 2.0f, 17.5f});
    // Not the child: between (20, 0, 0) and (30, 0, 0).
    ExpectNear(bones[1][3], bones[0][3] + glm::vec3{25.0f, 0.0f, 0.0f});
}

// It is enough for the previous action to have it, and it is bone 0 by its index, not by being a root.
TEST(PoseTest, LockPositionsIsBoneZeroWithEitherAction)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.bones[1].parent = -1;
    const AnimationState animation{0, 1.5f, 1, 2};

    const std::vector<BoneTransform> bones = Pose(skeleton, animation);

    // x and y from key 0 of action 0 (0, 0); z from 9 to 0 halfway.
    ExpectNear(bones[0][3], {0.0f, 0.0f, 4.5f});
    // Bone 1, also a root, goes linearly: from (30, 0, 0) to (10, 0, 0).
    ExpectNear(bones[1][3], {20.0f, 0.0f, 0.0f});
}

TEST(PoseTest, DummyBonesAreNotPosed)
{
    const Skeleton skeleton = SampleSkeleton();
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(7.0f));

    ASSERT_TRUE(PoseSkeleton(skeleton, AnimationState{0, 1.5f, 0, 0}, {}, NoHead, bones));

    EXPECT_EQ(bones[2], BoneTransform(7.0f));
    EXPECT_NE(bones[0], BoneTransform(7.0f));
}

// 0x5fc700: with both keys within 0.001, the pose copies the previous one and does not slerp.
TEST(PoseTest, AlmostEqualKeysCopyTheFirst)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.bones[0].actions[0][0].rotation = glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f);
    skeleton.bones[0].actions[0][1].rotation = glm::quat::wxyz(0.9995f, 0.0f, 0.0f, 0.0009f);

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{0, 1.5f, 0, 0});

    EXPECT_EQ(bones[0][0], QuaternionMatrix(glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f))[0]);
    EXPECT_EQ(bones[0][1], QuaternionMatrix(glm::quat::wxyz(1.0f, 0.0f, 0.0f, 0.0f))[1]);
}

// An action the skeleton lacks is 0, and a key beyond range, 0. The fractional part comes from the frame before it is
// cut: with 5 in a two-key action it is 0.
TEST(PoseTest, AnActionOrAKeyOutOfRangeIsTheFirst)
{
    const std::vector<BoneTransform> outOfRange = Pose(SampleSkeleton(), AnimationState{7, 5.0f, 9, 9});
    const std::vector<BoneTransform> first = Pose(SampleSkeleton(), AnimationState{0, 0.0f, 0, 0});

    EXPECT_EQ(outOfRange, first);
}

// Exactly at the edge too: the action equal to the action count, and the key equal to the key count, are 0.
TEST(PoseTest, AnActionOrAKeyJustOutOfRangeIsTheFirst)
{
    const std::vector<BoneTransform> atTheEdge = Pose(SampleSkeleton(), AnimationState{2, 2.0f, 2, 2});
    const std::vector<BoneTransform> first = Pose(SampleSkeleton(), AnimationState{0, 0.0f, 0, 0});

    EXPECT_EQ(atTheEdge, first);
}

// The head subtracts its angles in both keys it interpolates, and the slerp goes between the two.
TEST(PoseTest, TheHeadInterpolatesBothKeys)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.headBone = 1;
    skeleton.bones[0].actions[0][1] = Key({10, 0, 0}, {0, 0, 0});
    skeleton.bones[1].actions[0][1] = Key({10, 0, 0}, {0, 0, 90});
    const glm::vec2 head{30.0f, 40.0f};

    const std::vector<BoneTransform> bones = Pose(skeleton, AnimationState{0, 1.5f, 0, 0}, {}, head);

    const glm::quat from = AngleQuaternion({Radians(-30.0f), 0.0f, Radians(-40.0f)});
    const glm::quat to = AngleQuaternion({Radians(-30.0f), 0.0f, Radians(90.0f - 40.0f)});
    const BoneTransform expected = QuaternionMatrix(QuaternionSlerp(from, to, 0.5f));
    for (int column = 0; column < 3; ++column)
    {
        ExpectNear(bones[1][column], expected[column]);
    }
}

// If a posed bone lacks the key it needs, nothing is written.
TEST(PoseTest, AMissingKeyIsRefused)
{
    Skeleton skeleton = SampleSkeleton();
    skeleton.bones[1].actions[0].pop_back();
    std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(7.0f));

    EXPECT_FALSE(PoseSkeleton(skeleton, AnimationState{0, 1.5f, 0, 0}, {}, NoHead, bones));
    EXPECT_EQ(bones[0], BoneTransform(7.0f));
}

// A hand-built Skeleton with a parent that is neither −1 nor an earlier bone: not posed, like the one MakeSkeleton
// rejects.
TEST(PoseTest, ABadParentIsRefused)
{
    for (const int parent : {1, 2, 5, -2})
    {
        Skeleton skeleton = SampleSkeleton();
        skeleton.bones[1].parent = static_cast<std::int16_t>(parent);
        std::vector<BoneTransform> bones(skeleton.bones.size(), BoneTransform(7.0f));

        EXPECT_FALSE(PoseSkeleton(skeleton, AnimationState{}, {}, NoHead, bones)) << parent;
        EXPECT_EQ(bones[0], BoneTransform(7.0f)) << parent;
    }
}

// With a frame beyond the keys, the key is 0 but the fractional part is the frame's: from the earlier key 1 to 0, a
// quarter of the way.
TEST(PoseTest, AFrameOutOfRangeKeepsItsFraction)
{
    const std::vector<BoneTransform> bones = Pose(SampleSkeleton(), AnimationState{0, 5.25f, 0, 1});

    ExpectNear(bones[0][3], {7.5f, 0, 0});
}

// Without the skeleton's size it writes nothing.
TEST(PoseTest, TheBonesMustBeThoseOfTheSkeleton)
{
    const Skeleton skeleton = SampleSkeleton();
    std::vector<BoneTransform> bones(2, BoneTransform(7.0f));

    EXPECT_FALSE(PoseSkeleton(skeleton, AnimationState{}, {}, NoHead, bones));
    EXPECT_EQ(bones[0], BoneTransform(7.0f));
}
} // namespace
