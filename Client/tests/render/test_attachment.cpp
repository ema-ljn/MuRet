#include "Render/Models/Attachment.h"

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"
#include "Render/Models/Skeleton.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <vector>

// Client::Render::Models::Attachment: de qué hueso cuelga cada cosa del personaje y con qué
// desplazamiento (0x511290, 0x50ba30), la raíz de 0x4ec190 y la pose de 0x4eb860 con su séptimo
// argumento en 1, que es la de 0x509340 (06 § El modelo en el binario). La espada del hueso 33 y la
// mascota del 34 están verificadas contra la traza (GoldenSkinningTrace).
namespace
{
using Client::Render::Models::AngleMatrix;
using Client::Render::Models::Attachment;
using Client::Render::Models::AttachmentOf;
using Client::Render::Models::AttachmentRoot;
using Client::Render::Models::AttachmentSlot;
using Client::Render::Models::BoneKey;
using Client::Render::Models::BoneTransform;
using Client::Render::Models::DrawsThePet;
using Client::Render::Models::PoseAttached;
using Client::Render::Models::Skeleton;
using Client::Render::Models::SkeletonBone;

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

// Q-C98: la mascota cuelga del 34, con (20, 0, 0), y solo se dibuja si es 0x1c13 (Helper02).
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

// 0x4ec190: el desplazamiento gira con el hueso y no lleva la escala del personaje, que va en cada
// vértice: con el hueso girado 90° en z y en (1, 2, 3), (20, 0, 0) suma (0, 20, 0).
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

// 0x4eb860 con el séptimo argumento en 1: los huesos sin padre van con la raíz que da quien llama,
// no con el giro del objeto.
TEST(AttachmentTest, AnAttachedPoseHangsFromTheRoot)
{
    Skeleton skeleton;
    SkeletonBone bone;
    bone.parent = -1;
    bone.actions = {{BoneKey{{10.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {}}}};
    bone.actions[0][0].rotation = Client::Render::Models::AngleQuaternion({0.0f, 0.0f, 0.0f});
    skeleton.bones = {bone};
    skeleton.actions = {{.repeats = true, .speed = 0.0f, .keyCount = 1, .lockPositions = false}};
    BoneTransform root = AngleMatrix({0.0f, 0.0f, 90.0f});
    root[3] = glm::vec3(100.0f, 0.0f, 0.0f);
    std::vector<BoneTransform> bones(1, BoneTransform(1.0f));

    ASSERT_TRUE(PoseAttached(skeleton, {}, root, bones));

    ExpectNear(bones[0][3], {100.0f, 10.0f, 0.0f});
}
} // namespace
