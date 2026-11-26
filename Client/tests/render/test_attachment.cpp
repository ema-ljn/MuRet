#include "Models/Attachment.h"

#include "Math/HalfLifeMath.h"
#include "Models/Pose.h"
#include "Math/RoundedMath.h"
#include "Models/Skeleton.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <numbers>
#include <vector>

// Which bone each thing hangs from and with what offset (0x511290, 0x50ba30), the root of 0x4ec190 and the pose of
// 0x4eb860 with its seventh argument at 1, which is that of 0x509340 (06 § El modelo en el binario). The sword on bone
// 33 and the pet on 34 are verified against the trace (GoldenSkinningTrace).
namespace
{
using Client::Math::AngleMatrix;
using Client::Math::BoneTransform;
using Client::Math::RoundedCos;
using Client::Math::RoundedSin;
using Client::Models::Attachment;
using Client::Models::AttachmentOf;
using Client::Models::AttachmentRoot;
using Client::Models::AttachmentSlot;
using Client::Models::BoneKey;
using Client::Models::DrawsThePet;
using Client::Models::PlacedRoot;
using Client::Models::PoseAttached;
using Client::Models::QuiverPlacement;
using Client::Models::Skeleton;
using Client::Models::SkeletonBone;

constexpr float Tolerance = 1e-4f;

void ExpectNear(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, Tolerance);
    EXPECT_NEAR(actual.y, expected.y, Tolerance);
    EXPECT_NEAR(actual.z, expected.z, Tolerance);
}

TEST(AttachmentTest, PlayerHandsAreBones33And42)
{
    EXPECT_EQ(AttachmentOf(AttachmentSlot::RightHand).bone, 33);
    EXPECT_EQ(AttachmentOf(AttachmentSlot::LeftHand).bone, 42);
    EXPECT_EQ(AttachmentOf(AttachmentSlot::RightHand).offset, glm::vec3(0.0f));
    EXPECT_EQ(AttachmentOf(AttachmentSlot::LeftHand).offset, glm::vec3(0.0f));
}

TEST(AttachmentTest, WingsAreBone47)
{
    EXPECT_EQ(AttachmentOf(AttachmentSlot::Wings).bone, 47);
    EXPECT_EQ(AttachmentOf(AttachmentSlot::Wings).offset, glm::vec3(0.0f, 0.0f, 15.0f));
}

// Q-C98: the pet hangs from 34, with (20, 0, 0), and is drawn only if it is 0x1c13 (Helper02).
TEST(AttachmentTest, PetIsBone34OnlyFor0x1c13)
{
    const Attachment pet = AttachmentOf(AttachmentSlot::Pet);

    EXPECT_EQ(pet.bone, 34);
    EXPECT_EQ(pet.offset, glm::vec3(20.0f, 0.0f, 0.0f));
    EXPECT_TRUE(DrawsThePet(0x1c13));
    for (const int other : {0x1c12, 0x1c14, 0x1c15, 0x1c16, 0x1c37})
    {
        EXPECT_FALSE(DrawsThePet(static_cast<std::uint16_t>(other))) << other;
    }
}

// 0x509340 with its flag, on the quiver (bolts 0xa19 and arrows 0xa21): AngleMatrix(70, 0, 90) with the translation
// (−10, 5, 10).
TEST(AttachmentTest, TheQuiverPlacementIsThatOf0x509340)
{
    EXPECT_EQ(QuiverPlacement.angles, glm::vec3(70.0f, 0.0f, 90.0f));
    EXPECT_EQ(QuiverPlacement.translation, glm::vec3(-10.0f, 5.0f, 10.0f));
}

// The root is the bone times that matrix (R_ConcatTransforms): with the bone rotated 90° about z and at (1, 2, 3), the
// translation turns with it, and the axes carry both rotations.
TEST(AttachmentTest, ThePlacedRootIsTheBoneByTheAngleMatrix)
{
    BoneTransform parent = AngleMatrix({0.0f, 0.0f, 90.0f});
    parent[3] = glm::vec3(1.0f, 2.0f, 3.0f);
    const float seventy = 70.0f * std::numbers::pi_v<float> / 180.0f;

    const BoneTransform root = PlacedRoot(parent, QuiverPlacement);

    ExpectNear(root[3], {-4.0f, -8.0f, 13.0f});
    ExpectNear(root[0], {-1.0f, 0.0f, 0.0f});
    ExpectNear(root[1], {0.0f, -RoundedCos(seventy), RoundedSin(seventy)});
}

// 0x4ec190: the offset turns with the bone and does not carry the character's scale, which goes on each vertex: with
// the bone rotated 90° about z and at (1, 2, 3), (20, 0, 0) adds (0, 20, 0).
TEST(AttachmentTest, TheOffsetTurnsWithTheBone)
{
    BoneTransform parent = AngleMatrix({0.0f, 0.0f, 90.0f});
    parent[3] = glm::vec3(1.0f, 2.0f, 3.0f);

    const BoneTransform root = AttachmentRoot(parent, {20.0f, 0.0f, 0.0f});

    ExpectNear(root[3], {1.0f, 22.0f, 3.0f});
    ExpectNear(root[0], parent[0]);
    ExpectNear(root[1], parent[1]);
    ExpectNear(root[2], parent[2]);
}

// 0x4eb860 with the seventh argument at 1: parentless bones use the caller's root, not the object's rotation.
TEST(AttachmentTest, AnAttachedPoseHangsFromTheRoot)
{
    Skeleton skeleton;
    SkeletonBone bone;
    bone.parent = -1;
    bone.actions = {{BoneKey{{10.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {}}}};
    bone.actions[0][0].rotation = Client::Math::AngleQuaternion({0.0f, 0.0f, 0.0f});
    skeleton.bones = {bone};
    skeleton.actions = {{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    BoneTransform root = AngleMatrix({0.0f, 0.0f, 90.0f});
    root[3] = glm::vec3(100.0f, 0.0f, 0.0f);
    std::vector<BoneTransform> bones(1, BoneTransform(1.0f));

    ASSERT_TRUE(PoseAttached(skeleton, {}, root, bones));

    ExpectNear(bones[0][3], {100.0f, 10.0f, 0.0f});
}
} // namespace
