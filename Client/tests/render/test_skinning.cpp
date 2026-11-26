#include "Models/Skinning.h"

#include "Data/Models/Model.h"
#include "Math/HalfLifeMath.h"
#include "Math/OriginalRotation.h"
#include "Models/Pose.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <bit>
#include <cstdint>
#include <vector>

// 0x4ebc80, one bone per vertex and no weights.
namespace
{
using Client::Data::Models::Mesh;
using Client::Math::AngleMatrix;
using Client::Math::BoneTransform;
using Client::Models::FlattenOnTheGround;
using Client::Models::NormalLight;
using Client::Models::ObjectTransform;
using Client::Models::SkinMesh;
using Client::Models::SkinMeshAroundBones;
using Client::Models::SkinMeshInWorld;
using Client::Models::SkinnedMesh;

constexpr float Tolerance = 1e-4f;

void ExpectNear(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, Tolerance);
    EXPECT_NEAR(actual.y, expected.y, Tolerance);
    EXPECT_NEAR(actual.z, expected.z, Tolerance);
}

// Two bones: 0 shifted to (1, 2, 3), and 1 rotated 90° about z and shifted to (10, 0, 0).
std::vector<BoneTransform> TwoBones()
{
    BoneTransform moved(1.0f);
    moved[3] = glm::vec3(1, 2, 3);
    BoneTransform turned = AngleMatrix({0, 0, 90});
    turned[3] = glm::vec3(10, 0, 0);
    return {moved, turned};
}

// One vertex and one normal on each bone.
Mesh TwoVertexMesh()
{
    Mesh mesh;
    mesh.vertices = {{0, {1, 0, 0}}, {1, {1, 0, 0}}};
    mesh.normals = {{0, {0, 0, 1}, 0}, {1, {1, 0, 0}, 1}};
    return mesh;
}

TEST(SkinningTest, AVertexFollowsItsBone)
{
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMesh(TwoVertexMesh(), TwoBones(), ObjectTransform{}, skinned));

    ASSERT_EQ(skinned.positions.size(), 2u);
    ExpectNear(skinned.positions[0], {2, 2, 3});
    ExpectNear(skinned.positions[1], {10, 1, 0});
}

// The object's scale and position go on each vertex, after the bone (0x4ebc80 with its last argument at 1, like a
// character's body).
TEST(SkinningTest, TheObjectScalesAndMovesTheVertices)
{
    const ObjectTransform object{{100, 200, 300}, {0, 0, 0}, 2.0f, 0.0f};
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMesh(TwoVertexMesh(), TwoBones(), object, skinned));

    ExpectNear(skinned.positions[0], {104, 204, 306});
    ExpectNear(skinned.positions[1], {120, 202, 300});
}

// 0x4ebc80 with DAT_006c881c other than 1, as 0x60cd60 sets it for the outline: the vertex rotated by its bone and
// times the scale, plus the bone's translation; then the object's scale and position.
TEST(SkinningTest, TheOutlineScalesAroundTheBone)
{
    const ObjectTransform object{{100, 200, 300}, {0, 0, 0}, 2.0f, 0.0f};
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMeshAroundBones(TwoVertexMesh(), TwoBones(), object, 1.5f, skinned));

    // (1, 0, 0) · 1.5 + (1, 2, 3), times 2 plus the object.
    ExpectNear(skinned.positions[0], {105, 204, 306});
    // Rotated 90°: (0, 1.5, 0) + (10, 0, 0), times 2 plus the object.
    ExpectNear(skinned.positions[1], {120, 203, 300});
}

// With the scale exactly 1, the original takes the usual path (0x5fccc0).
TEST(SkinningTest, AnOutlineScaleOfOneIsTheNormalSkinning)
{
    const ObjectTransform object{{100, 200, 300}, {0, 0, 0}, 2.0f, 0.0f};
    SkinnedMesh scaled;
    SkinnedMesh normal;

    ASSERT_TRUE(SkinMeshAroundBones(TwoVertexMesh(), TwoBones(), object, 1.0f, scaled));
    ASSERT_TRUE(SkinMesh(TwoVertexMesh(), TwoBones(), object, normal));

    EXPECT_EQ(scaled.positions, normal.positions);
}

template <typename Vector>
concept ProjectsWithoutSpread =
    requires(const Vector& point, const Vector& origin) { FlattenOnTheGround(point, origin); };

TEST(SkinningTest, TheCallerSuppliesTheShadowSpread)
{
    EXPECT_FALSE(ProjectsWithoutSpread<glm::vec3>);
}

// 0x4eff40, the shadow: with x' and z' from the object's position, X = (x' + 2000) · z' / (z' − 4000) + x' + px, y
// stays, and Z = pz + 5.
TEST(SkinningTest, TheShadowFlattensOnTheGround)
{
    const glm::vec3 flat = FlattenOnTheGround({110.0f, 77.0f, 80.0f}, {10.0f, 20.0f, 30.0f}, 2000.0f);

    EXPECT_NEAR(flat.x, (100.0f + 2000.0f) * 50.0f / (50.0f - 4000.0f) + 100.0f + 10.0f, 1e-3f);
    EXPECT_FLOAT_EQ(flat.y, 77.0f);
    EXPECT_FLOAT_EQ(flat.z, 35.0f);
}

// 0x4f0040..0x4f0074: X comes off the x87 stack in double with a single FSTP. With this vertex, doing the math in float
// gives 0x1.f8c508p+9, one ULP more; y goes through float before py is added and z is pz + 5.
TEST(SkinningTest, TheShadowXIsRoundedOnceFromDouble)
{
    const glm::vec3 flat = FlattenOnTheGround({0x1.00b06cp+10f, 77.0f, 0x1.31a20cp+8f},
                                              {0x1.0f8af6p+10f, 0x1.9005a8p+13f, 0x1.0e74c8p+8f}, 2000.0f);

    EXPECT_EQ(flat.x, 0x1.f8c506p+9f);
    EXPECT_EQ(flat.y, 0x1.34p+6f);
    EXPECT_EQ(flat.z, 0x1.1374c8p+8f);
}

// The per-vertex light of 0x4ebc80 (with model +0x44): max(0.2; 0.8 · L·n + 0.4) with L = (1.3; 0; 2), the
// DAT_006c8818 one, which is 1 from .data and nobody writes. The normal facing z gives 2; the x one, 1.44; a sideways
// one in y, 0.4; the −z one, the 0.2 floor. A normal carrying the object's scale (1.6) gives 0.8 · 1.6 · 2 + 0.4.
TEST(SkinningTest, TheVertexLightComesFromXAndZ)
{
    EXPECT_FLOAT_EQ(NormalLight({0.0f, 0.0f, 1.0f}), 2.0f);
    EXPECT_FLOAT_EQ(NormalLight({1.0f, 0.0f, 0.0f}), 1.44f);
    EXPECT_FLOAT_EQ(NormalLight({0.0f, -1.0f, 0.0f}), 0.4f);
    EXPECT_FLOAT_EQ(NormalLight({0.0f, 0.0f, -1.0f}), 0.2f);
    EXPECT_NEAR(NormalLight({0.0f, 0.0f, 1.6f}), (0.8f * 1.6f * 2.0f) + 0.4f, 1e-6f);
    // Exactly at the floor: 0.8 · (−0.25) + 0.4 = 0.2, with n = (0; 0; −0.125).
    EXPECT_NEAR(NormalLight({0.0f, 0.0f, -0.125f}), 0.2f, 1e-6f);
}

// Neither the bone's translation, nor the scale, nor the object's position.
TEST(SkinningTest, TheNormalOnlyRotates)
{
    const ObjectTransform object{{100, 200, 300}, {0, 0, 0}, 2.0f, 0.0f};
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMesh(TwoVertexMesh(), TwoBones(), object, skinned));

    ASSERT_EQ(skinned.normals.size(), 2u);
    ExpectNear(skinned.normals[0], {0, 0, 1});
    ExpectNear(skinned.normals[1], {0, 1, 0});
}

glm::vec3 Bits(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return {std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)};
}

// 0x4ebc80 with Translate at 0 on a world bone of 0x4eb860 with the flag at 1 (the one at (−10, 0, 30) times 0.7f at
// (1050, 1060, 300)): each vertex through 0x5fccc0 and each normal through 0x5fcbd0 with that matrix, the scale inside
// the normal. The bits come from a separate x87 emulation (exact fractions, PC53, each FSTP to float).
TEST(SkinningTest, InTheWorldVerticesAndNormalsGoThroughTheWorldBoneLikeTheX87)
{
    const Client::Math::OriginalMatrix rows{
        std::bit_cast<float>(0x3f1b3116u), std::bit_cast<float>(0xbeb07a40u), std::bit_cast<float>(0xbd78f129u),
        std::bit_cast<float>(0x44834000u), std::bit_cast<float>(0x3eb33333u), std::bit_cast<float>(0x3f18d583u),
        std::bit_cast<float>(0x3dd79713u), std::bit_cast<float>(0x44848000u), 0.0f,
        std::bit_cast<float>(0xbdf8f129u), std::bit_cast<float>(0x3f307a40u), std::bit_cast<float>(0x43960000u)};
    const std::vector<BoneTransform> bones{Client::Math::BoneTransformOf(rows)};
    Mesh mesh;
    mesh.vertices = {{0, {0, 0, 10}}, {0, {1, 0, 10}}, {0, {0, 1, 10}}};
    mesh.normals = {{0, {0, 0, 1}, 0}, {0, {0.6f, -0.8f, 0}, 0}};
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMeshInWorld(mesh, bones, skinned));

    ASSERT_EQ(skinned.positions.size(), 3u);
    EXPECT_EQ(skinned.positions[0], Bits(0x44832c8du, 0x4484a1b0u, 0x43997263u));
    EXPECT_EQ(skinned.positions[1], Bits(0x44833ff3u, 0x4484ace3u, 0x43997263u));
    EXPECT_EQ(skinned.positions[2], Bits(0x44832186u, 0x4484b4cau, 0x439962d4u));
    ASSERT_EQ(skinned.normals.size(), 2u);
    EXPECT_EQ(skinned.normals[0], Bits(0xbd78f129u, 0x3dd79713u, 0x3f307a40u));
    EXPECT_EQ(skinned.normals[1], Bits(0x3f23b4c1u, 0xbe8903b3u, 0x3dc72754u));

    Mesh broken = mesh;
    broken.normals[1].bone = 1;
    skinned.positions = {glm::vec3(7.0f)};
    EXPECT_FALSE(SkinMeshInWorld(broken, bones, skinned));
    EXPECT_EQ(skinned.positions, std::vector<glm::vec3>{glm::vec3(7.0f)});
}

// A missing bone, also exactly at the edge: nothing is written.
TEST(SkinningTest, ABoneOutOfRangeIsRefused)
{
    struct Case
    {
        bool inANormal;
        std::int16_t bone;
    };
    for (const Case bad : {Case{false, -1}, Case{false, 2}, Case{true, -1}, Case{true, 2}})
    {
        Mesh mesh = TwoVertexMesh();
        (bad.inANormal ? mesh.normals[1].bone : mesh.vertices[1].bone) = bad.bone;
        SkinnedMesh skinned;
        skinned.positions = {glm::vec3(7.0f)};

        EXPECT_FALSE(SkinMesh(mesh, TwoBones(), ObjectTransform{}, skinned)) << bad.inANormal << " " << bad.bone;
        ASSERT_EQ(skinned.positions.size(), 1u);
        EXPECT_EQ(skinned.positions[0], glm::vec3(7.0f));
    }
}

// The SkinnedMesh can be reused frame after frame: it holds the last mesh's data, not the earlier one's.
TEST(SkinningTest, ReusesItsBuffers)
{
    Mesh small = TwoVertexMesh();
    small.vertices.pop_back();
    small.normals.pop_back();
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMesh(TwoVertexMesh(), TwoBones(), ObjectTransform{}, skinned));
    ASSERT_TRUE(SkinMesh(small, TwoBones(), ObjectTransform{}, skinned));

    ASSERT_EQ(skinned.positions.size(), 1u);
    ASSERT_EQ(skinned.normals.size(), 1u);
    ExpectNear(skinned.positions[0], {2, 2, 3});
}
} // namespace
