#include "World/SphereFrustum.h"

#include "App/WindowState.h"
#include "Render/Pass3D.h"
#include "Scenes/SceneCameras.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <bit>
#include <cstdint>
#include <cstddef>
#include <limits>

// P-C55(b), lectura estática de 0x5fbb00 y 0x5fbf00: cinco planos, sin plano cercano. Estos tests
// prueban la reimplementación; no son vectores de una ejecución del original.
namespace
{
using Client::World::SphereFrustum;
using Client::World::SphereFrustumOf;
using Client::World::SphereVisible;

TEST(SphereFrustumTest, EachOfTheFivePlanesCanRejectASphere)
{
    for (std::size_t plane = 0; plane < 5; ++plane)
    {
        SphereFrustum frustum;
        frustum.normals[plane] = {1.0f, 0.0f, 0.0f};
        EXPECT_TRUE(SphereVisible(frustum, {-400.0f, 0.0f, 0.0f}, 400.0f));
        EXPECT_FALSE(SphereVisible(frustum, {std::nextafter(-400.0f, -401.0f), 0.0f, 0.0f}, 400.0f));
        frustum.offsets[plane] = 100.0f;
        EXPECT_TRUE(SphereVisible(frustum, {-500.0f, 0.0f, 0.0f}, 400.0f));
        EXPECT_FALSE(SphereVisible(frustum, {-501.0f, 0.0f, 0.0f}, 400.0f));
    }
}

TEST(SphereFrustumTest, TheDistanceKeepsTheRegisterPrecisionAndTheSumOrder)
{
    SphereFrustum frustum;
    frustum.normals[0] = {1.0f, 1.0f, 1.0f};
    frustum.offsets[0] = -0.5f;
    // (nx*x + nz*z) + ny*y, sin store f32 intermedio: da 1, no 0.
    EXPECT_TRUE(SphereVisible(frustum, {16777216.0f, -16777216.0f, 1.0f}, 0.0f));
    // En 53 bits, 2^53 + 1 redondea a 2^53. Sumar x+y primero daría 1 y lo dejaría pasar.
    EXPECT_FALSE(SphereVisible(frustum, {9007199254740992.0f, -9007199254740992.0f, 1.0f}, 0.0f));
}

TEST(SphereFrustumTest, UnorderedComparisonsRejectTheSphere)
{
    SphereFrustum frustum;
    frustum.normals[0] = {1.0f, 0.0f, 0.0f};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SphereVisible(frustum, {nan, 0.0f, 0.0f}, 400.0f));
    EXPECT_FALSE(SphereVisible(frustum, {}, nan));
    frustum.offsets[4] = nan;
    EXPECT_FALSE(SphereVisible(frustum, {}, 400.0f));
}

TEST(SphereFrustumTest, TheCameraBuildsFourSidesAndTheFarPlane)
{
    Client::Render::Camera3D camera;
    camera.position = {1000.0f, 2000.0f, 3000.0f};
    camera.farPlane = 1000.0f;
    camera.fieldOfView = 0.0f;
    // Con fov 0, w=100 y h=75; lejos=R32(1000*0.9f)=900. La vista identidad mira hacia -z.
    const SphereFrustum frustum = SphereFrustumOf(glm::mat4(1.0f), camera);
    EXPECT_TRUE(SphereVisible(frustum, {1000.0f, 2000.0f, 2550.0f}, 0.0f));
    EXPECT_FALSE(SphereVisible(frustum, {1100.0f, 2000.0f, 2550.0f}, 0.0f));
    EXPECT_FALSE(SphereVisible(frustum, {900.0f, 2000.0f, 2550.0f}, 0.0f));
    EXPECT_FALSE(SphereVisible(frustum, {1000.0f, 2075.0f, 2550.0f}, 0.0f));
    EXPECT_FALSE(SphereVisible(frustum, {1000.0f, 1925.0f, 2550.0f}, 0.0f));
    EXPECT_TRUE(SphereVisible(frustum, {1000.0f, 2000.0f, 1700.0f}, 400.0f));
    EXPECT_FALSE(SphereVisible(frustum, {1000.0f, 2000.0f, 1699.0f}, 400.0f));
    // Sin near plane: una esfera en la cámara sigue pasando.
    EXPECT_TRUE(SphereVisible(frustum, camera.position, 1.0f));
}

TEST(SphereFrustumTest, TheLoginCameraSeesTheDragonButNotAnEffectBehindIt)
{
    const auto camera = Client::Scenes::LoginCamera();
    const auto pass =
        Client::Render::MakePass3D(Client::App::PixelSize{640, 480}, Client::Scenes::WorldViewport, camera);
    const SphereFrustum frustum = SphereFrustumOf(pass.view, camera);
    EXPECT_TRUE(SphereVisible(frustum, {5600.0f, 23000.0f, 3400.0f}, 400.0f));
    EXPECT_FALSE(SphereVisible(frustum, {5600.0f, -100000.0f, 3400.0f}, 400.0f));
}

TEST(SphereFrustumTest, RotatedPointsUseTheTransposedViewAndThenTheCameraTranslation)
{
    glm::mat4 view(1.0f);
    // La inversa de este giro lleva (x,y,z) a (y,z,x).
    view[0][0] = 0.0f;
    view[0][1] = 1.0f;
    view[1][1] = 0.0f;
    view[1][2] = 1.0f;
    view[2][2] = 0.0f;
    view[2][0] = 1.0f;
    Client::Render::Camera3D camera;
    camera.position = {10.0f, 20.0f, 30.0f};
    camera.farPlane = 1000.0f;
    camera.fieldOfView = 0.0f;
    const auto points = Client::World::FrustumPointsOf(view, camera);
    EXPECT_EQ(points[0], glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(points[1], glm::vec3(85.0f, -880.0f, -70.0f));
    EXPECT_EQ(points[2], glm::vec3(85.0f, -880.0f, 130.0f));
    EXPECT_EQ(points[3], glm::vec3(-65.0f, -880.0f, 130.0f));
    EXPECT_EQ(points[4], glm::vec3(-65.0f, -880.0f, -70.0f));
    camera.fieldOfView = 35.0f;
    // A mitad de profundidad, el semiancho es ~192 con k=1 y ~121 con k=0,5. El punto local x=150
    // queda dentro solo en el primero; en el mundo, el x local se convirtió en z.
    EXPECT_TRUE(SphereVisible(SphereFrustumOf(view, camera, 1.0f), {10.0f, -430.0f, 180.0f}, 0.0f));
    EXPECT_FALSE(SphereVisible(SphereFrustumOf(view, camera, 0.5f), {10.0f, -430.0f, 180.0f}, 0.0f));
}
TEST(SphereFrustumTest, TheFarPlaneOffsetUsesTheFirstRoundedCorner)
{
    Client::Render::Camera3D camera;
    camera.position = {6866.58740234375f, 12395.0068359375f, 1174.236572265625f};
    camera.angles = {-48.5f, 0.0f, -45.0f};
    camera.fieldOfView = 35.0f;
    camera.farPlane = 5000.0f;
    const auto pass =
        Client::Render::MakePass3D(Client::App::PixelSize{640, 480}, Client::Scenes::WorldViewport, camera);
    const auto frustum = SphereFrustumOf(pass.view, camera);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(frustum.offsets[4]), 0x45cfcddeu);
    EXPECT_TRUE(SphereVisible(frustum, {4271.5732421875f, 14989.9921875f, -2072.60107421875f}, 400.0f));
}
} // namespace
