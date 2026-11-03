#include "Render/Models/Skinning.h"

#include "Data/Models/Model.h"
#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Pose.h"

#include <gtest/gtest.h>

#include <glm/ext/vector_float3.hpp>

#include <vector>

// Client::Render::Models::SkinMesh: 0x4ebc80, un hueso por vértice y sin pesos.
namespace
{
using Client::Data::Models::Mesh;
using Client::Render::Models::AngleMatrix;
using Client::Render::Models::BoneTransform;
using Client::Render::Models::ObjectTransform;
using Client::Render::Models::SkinMesh;
using Client::Render::Models::SkinnedMesh;

constexpr float Tolerance = 1e-4f;

void ExpectNear(const glm::vec3& actual, const glm::vec3& expected)
{
    EXPECT_NEAR(actual.x, expected.x, Tolerance);
    EXPECT_NEAR(actual.y, expected.y, Tolerance);
    EXPECT_NEAR(actual.z, expected.z, Tolerance);
}

// Dos huesos: el 0 corrido a (1, 2, 3), y el 1 girado 90° en z y corrido a (10, 0, 0).
std::vector<BoneTransform> TwoBones()
{
    BoneTransform moved(1.0f);
    moved[3] = glm::vec3(1, 2, 3);
    BoneTransform turned = AngleMatrix({0, 0, 90});
    turned[3] = glm::vec3(10, 0, 0);
    return {moved, turned};
}

// Un vértice y una normal en cada hueso.
Mesh TwoVertexMesh()
{
    Mesh mesh;
    mesh.vertices = {{0, {1, 0, 0}}, {1, {1, 0, 0}}};
    mesh.normals = {{0, {0, 0, 1}, 0}, {1, {1, 0, 0}, 1}};
    return mesh;
}

// Cada vértice por la matriz de su hueso, giro y traslación.
TEST(SkinningTest, AVertexFollowsItsBone)
{
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMesh(TwoVertexMesh(), TwoBones(), ObjectTransform{}, skinned));

    ASSERT_EQ(skinned.positions.size(), 2u);
    ExpectNear(skinned.positions[0], {2, 2, 3});
    ExpectNear(skinned.positions[1], {10, 1, 0});
}

// La escala y la posición del objeto van en cada vértice, después del hueso (0x4ebc80 con su último
// argumento en 1, como el cuerpo de un personaje).
TEST(SkinningTest, TheObjectScalesAndMovesTheVertices)
{
    const ObjectTransform object{{100, 200, 300}, {0, 0, 0}, 2.0f, 0.0f};
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMesh(TwoVertexMesh(), TwoBones(), object, skinned));

    ExpectNear(skinned.positions[0], {104, 204, 306});
    ExpectNear(skinned.positions[1], {120, 202, 300});
}

// Una normal solo gira: ni la traslación del hueso, ni la escala, ni la posición del objeto.
TEST(SkinningTest, TheNormalOnlyRotates)
{
    const ObjectTransform object{{100, 200, 300}, {0, 0, 0}, 2.0f, 0.0f};
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMesh(TwoVertexMesh(), TwoBones(), object, skinned));

    ASSERT_EQ(skinned.normals.size(), 2u);
    ExpectNear(skinned.normals[0], {0, 0, 1});
    ExpectNear(skinned.normals[1], {0, 1, 0});
}

// Un hueso que no está: no escribe nada.
TEST(SkinningTest, ABoneOutOfRangeIsRefused)
{
    for (const bool inANormal : {false, true})
    {
        Mesh mesh = TwoVertexMesh();
        if (inANormal)
        {
            mesh.normals[1].bone = -1;
        }
        else
        {
            mesh.vertices[1].bone = 2;
        }
        SkinnedMesh skinned;
        skinned.positions = {glm::vec3(7.0f)};

        EXPECT_FALSE(SkinMesh(mesh, TwoBones(), ObjectTransform{}, skinned)) << inANormal;
        ASSERT_EQ(skinned.positions.size(), 1u);
        EXPECT_EQ(skinned.positions[0], glm::vec3(7.0f));
    }
}
} // namespace
