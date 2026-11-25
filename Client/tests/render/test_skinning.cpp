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

// Client::Render::Models::SkinMesh: 0x4ebc80, un hueso por vértice y sin pesos.
namespace
{
using Client::Data::Models::Mesh;
using Client::Render::Models::AngleMatrix;
using Client::Render::Models::BoneTransform;
using Client::Render::Models::FlattenOnTheGround;
using Client::Render::Models::NormalLight;
using Client::Render::Models::ObjectTransform;
using Client::Render::Models::SkinMesh;
using Client::Render::Models::SkinMeshAroundBones;
using Client::Render::Models::SkinMeshInWorld;
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

// 0x4ebc80 con DAT_006c881c distinto de 1, como lo pone 0x60cd60 para el contorno: el vértice girado
// por su hueso y por la escala, más la traslación del hueso; después la escala y la posición del
// objeto.
TEST(SkinningTest, TheOutlineScalesAroundTheBone)
{
    const ObjectTransform object{{100, 200, 300}, {0, 0, 0}, 2.0f, 0.0f};
    SkinnedMesh skinned;

    ASSERT_TRUE(SkinMeshAroundBones(TwoVertexMesh(), TwoBones(), object, 1.5f, skinned));

    // (1, 0, 0) · 1,5 + (1, 2, 3), por 2 y más el objeto.
    ExpectNear(skinned.positions[0], {105, 204, 306});
    // Girado 90°: (0, 1,5, 0) + (10, 0, 0), por 2 y más el objeto.
    ExpectNear(skinned.positions[1], {120, 203, 300});
}

// Con la escala en 1 justo, el original toma el camino de siempre (0x5fccc0).
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

// 0x4eff40, la sombra: con x' y z' desde la posición del objeto, X = (x' + 2000) · z' / (z' − 4000) +
// x' + px, la y queda, y Z = pz + 5.
TEST(SkinningTest, TheShadowFlattensOnTheGround)
{
    const glm::vec3 flat = FlattenOnTheGround({110.0f, 77.0f, 80.0f}, {10.0f, 20.0f, 30.0f}, 2000.0f);

    EXPECT_NEAR(flat.x, (100.0f + 2000.0f) * 50.0f / (50.0f - 4000.0f) + 100.0f + 10.0f, 1e-3f);
    EXPECT_FLOAT_EQ(flat.y, 77.0f);
    EXPECT_FLOAT_EQ(flat.z, 35.0f);
}

// 0x4f0040..0x4f0074: la X sale de la pila x87 en double con un solo FSTP. Con este vértice, hacer las cuentas
// en float da 0x1.f8c508p+9, un ULP más; la y pasa por float antes de sumarle py y la z es pz + 5.
TEST(SkinningTest, TheShadowXIsRoundedOnceFromDouble)
{
    const glm::vec3 flat = FlattenOnTheGround({0x1.00b06cp+10f, 77.0f, 0x1.31a20cp+8f},
                                              {0x1.0f8af6p+10f, 0x1.9005a8p+13f, 0x1.0e74c8p+8f}, 2000.0f);

    EXPECT_EQ(flat.x, 0x1.f8c506p+9f);
    EXPECT_EQ(flat.y, 0x1.34p+6f);
    EXPECT_EQ(flat.z, 0x1.1374c8p+8f);
}

// La luz por vértice de 0x4ebc80 (con +0x44 del modelo): max(0,2; 0,8 · L·n + 0,4) con L = (1,3; 0; 2),
// la de DAT_006c8818, que vale 1 desde el .data y nadie escribe. La normal que mira a z da 2; la de x,
// 1,44; una de costado en y, 0,4; la de −z, el piso de 0,2. Una normal que viene con la escala del
// objeto (1,6) da 0,8 · 1,6 · 2 + 0,4.
TEST(SkinningTest, TheVertexLightComesFromXAndZ)
{
    EXPECT_FLOAT_EQ(NormalLight({0.0f, 0.0f, 1.0f}), 2.0f);
    EXPECT_FLOAT_EQ(NormalLight({1.0f, 0.0f, 0.0f}), 1.44f);
    EXPECT_FLOAT_EQ(NormalLight({0.0f, -1.0f, 0.0f}), 0.4f);
    EXPECT_FLOAT_EQ(NormalLight({0.0f, 0.0f, -1.0f}), 0.2f);
    EXPECT_NEAR(NormalLight({0.0f, 0.0f, 1.6f}), (0.8f * 1.6f * 2.0f) + 0.4f, 1e-6f);
    // Justo en el piso: 0,8 · (−0,25) + 0,4 = 0,2, con n = (0; 0; −0,125).
    EXPECT_NEAR(NormalLight({0.0f, 0.0f, -0.125f}), 0.2f, 1e-6f);
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

glm::vec3 Bits(std::uint32_t x, std::uint32_t y, std::uint32_t z)
{
    return {std::bit_cast<float>(x), std::bit_cast<float>(y), std::bit_cast<float>(z)};
}

// 0x4ebc80 con Translate en 0 sobre un hueso del mundo de 0x4eb860 con la bandera en 1 (el de (−10, 0, 30) por 0,7f en
// (1050, 1060, 300)): cada vértice por 0x5fccc0 y cada normal por 0x5fcbd0 con esa matriz, la escala dentro de la
// normal. Los bits salen de una emulación aparte de la x87 (fracciones exactas, PC53, cada FSTP en float).
TEST(SkinningTest, InTheWorldVerticesAndNormalsGoThroughTheWorldBoneLikeTheX87)
{
    const Client::Render::Models::OriginalMatrix rows{
        std::bit_cast<float>(0x3f1b3116u), std::bit_cast<float>(0xbeb07a40u), std::bit_cast<float>(0xbd78f129u),
        std::bit_cast<float>(0x44834000u), std::bit_cast<float>(0x3eb33333u), std::bit_cast<float>(0x3f18d583u),
        std::bit_cast<float>(0x3dd79713u), std::bit_cast<float>(0x44848000u), 0.0f,
        std::bit_cast<float>(0xbdf8f129u), std::bit_cast<float>(0x3f307a40u), std::bit_cast<float>(0x43960000u)};
    const std::vector<BoneTransform> bones{Client::Render::Models::BoneTransformOf(rows)};
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

// Un hueso que no está, también justo en el borde: no escribe nada.
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

// El SkinnedMesh se puede usar frame tras frame: queda con lo de la última malla, no lo de antes.
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
