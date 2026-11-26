#include "Render/Pass3D.h"

#include "Core/PixelSize.h"
#include "Math/RoundedMath.h"

#include <gtest/gtest.h>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/geometric.hpp>

#include <numbers>
#include <optional>
#include <utility>

// The viewport of 0x620db0, the gluPerspective perspective with the far × 1.4 and the view of the three glRotatef and
// the glTranslatef; and the mouse ray of 0x620480 (06 § Cámara and § El picking).
namespace
{
using Client::Core::PixelSize;
using Client::Math::RoundedCos;
using Client::Math::RoundedSin;
using Client::Render::Camera3D;
using Client::Render::MakePass3D;
using Client::Render::Pass3D;
using Client::Render::ViewportRect;

constexpr PixelSize Window1024{1024, 768};
constexpr PixelSize Window800{800, 600};

// The world pass of the selection and the login (0x620db0(0, 25, 640, 430)).
constexpr ViewportRect WorldRect{0, 25, 640, 430};

// cot(17.5°): the [1][1] of the 35° perspective (06 § Cámara, the selection trace).
constexpr float CotangentOf17_5 = 3.171595f;

// Any camera, with angles on all three axes so that the order shows.
Camera3D SomeCamera()
{
    Camera3D camera;
    camera.position = {13909.0f, 14482.0f, 543.0f};
    camera.angles = {-84.5f, 10.0f, -45.0f};
    camera.fieldOfView = 35.0f;
    camera.nearPlane = 20.0f;
    camera.farPlane = 11519.9f;
    return camera;
}

// The glRotatef(degrees, axis) matrix as the GL 1.x specification gives it, column-major.
glm::mat4 GlRotation(float degrees, int axis)
{
    const double radians = static_cast<double>(degrees) * std::numbers::pi / 180.0;
    const auto c = static_cast<float>(RoundedCos(radians));
    const auto s = static_cast<float>(RoundedSin(radians));
    glm::mat4 m(1.0f);
    const int a = (axis + 1) % 3;
    const int b = (axis + 2) % 3;
    m[a][a] = c;
    m[b][b] = c;
    m[a][b] = s;  // column a, row b
    m[b][a] = -s; // column b, row a
    return m;
}

glm::mat4 GlTranslation(const glm::vec3& by)
{
    glm::mat4 m(1.0f);
    m[3] = glm::vec4(by, 1.0f);
    return m;
}

void ExpectNear(const glm::mat4& actual, const glm::mat4& expected, float tolerance)
{
    for (int column = 0; column < 4; ++column)
    {
        for (int row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(actual[column][row], expected[column][row], tolerance) << "[" << column << "][" << row << "]";
        }
    }
}

// x · W / 640 and y · H / 480 in integers: at 800 × 600, 25 · 600 / 480 = 31 and 430 · 600 / 480 = 537, and GL's y
// is 600 − 31 − 537 = 32.
TEST(Pass3DTest, ScalesTheViewportInIntegers)
{
    EXPECT_EQ(MakePass3D(Window1024, WorldRect, SomeCamera()).viewport, glm::ivec4(0, 40, 1024, 688));
    EXPECT_EQ(MakePass3D(Window800, WorldRect, SomeCamera()).viewport, glm::ivec4(0, 32, 800, 537));
}

// 0x620d10: glViewport(x, H − y − h, w, h). The top strip, 0x620db0(0, 0, 640, 48), gives the trace's 0,692,1024,76.
TEST(Pass3DTest, TheGlYIsFromTheBottom)
{
    EXPECT_EQ(MakePass3D(Window1024, ViewportRect{0, 0, 640, 48}, SomeCamera()).viewport, glm::ivec4(0, 692, 1024, 76));
    EXPECT_EQ(MakePass3D(PixelSize{640, 480}, ViewportRect{100, 50, 200, 100}, SomeCamera()).viewport,
              glm::ivec4(100, 330, 200, 100));
}

// gluPerspective with the viewport's width and height, not the window's: 1024/688.
TEST(Pass3DTest, TheAspectIsThatOfTheViewport)
{
    const Pass3D pass = MakePass3D(Window1024, WorldRect, SomeCamera());

    EXPECT_NEAR(pass.projection[1][1], CotangentOf17_5, 1e-5f);
    EXPECT_NEAR(pass.projection[0][0], CotangentOf17_5 / (1024.0f / 688.0f), 1e-5f);
}

// 0x6b8c00: the far that reaches gluPerspective is the camera's times 1.4.
TEST(Pass3DTest, TheFarIsOnePointFourTimesTheCamera)
{
    Camera3D camera = SomeCamera();
    camera.farPlane = 2000.0f;
    const Pass3D pass = MakePass3D(Window1024, WorldRect, camera);

    // Not named `near` or `far`: windows.h defines them as empty macros.
    const float farPlane = 2000.0f * 1.4f;
    const float nearPlane = 20.0f;
    EXPECT_NEAR(pass.projection[2][2], -(farPlane + nearPlane) / (farPlane - nearPlane), 1e-6f);
    EXPECT_NEAR(pass.projection[3][2], -2.0f * farPlane * nearPlane / (farPlane - nearPlane), 1e-4f);
    EXPECT_FLOAT_EQ(pass.farPlane, 2000.0f);
}

// The selection camera at 1024 × 768, against the trace (06 § Cámara): the viewport, the cotangent of 17.5°, the
// aspect 1024/688, the far of about 11520 times 1.4, and the rotations Y 0°, X −84.5° and Z −45°, in that order, with
// the position subtracted at the end.
TEST(Pass3DTest, MatchesTheMatricesOfTheSelectionTrace)
{
    Camera3D camera = SomeCamera();
    camera.angles = {-84.5f, 0.0f, -45.0f};
    const Pass3D pass = MakePass3D(Window1024, WorldRect, camera);

    EXPECT_EQ(pass.viewport, glm::ivec4(0, 40, 1024, 688));
    EXPECT_NEAR(pass.projection[0][0], 2.130915f, 1e-5f);
    EXPECT_NEAR(pass.projection[1][1], CotangentOf17_5, 1e-5f);
    const float farPlane = 11520.0f * 1.4f;
    EXPECT_NEAR(pass.projection[2][2], -(farPlane + 20.0f) / (farPlane - 20.0f), 1e-6f);
    EXPECT_FLOAT_EQ(pass.projection[2][3], -1.0f);
    const glm::mat4 expected = GlRotation(0.0f, 1) * GlRotation(-84.5f, 0) * GlRotation(-45.0f, 2) *
                               GlTranslation(-glm::vec3(13909.0f, 14482.0f, 543.0f));
    ExpectNear(pass.view, expected, 2e-3f);
}

// With all three angles nonzero the order shows: Y, X and Z, like the three glRotatef of 0x620db0. The traces have Y
// at 0, and there X and Y commute.
TEST(Pass3DTest, TheRotationsGoInTheOrderYXZ)
{
    const Pass3D pass = MakePass3D(Window1024, WorldRect, SomeCamera());

    const glm::mat4 expected = GlRotation(10.0f, 1) * GlRotation(-84.5f, 0) * GlRotation(-45.0f, 2) *
                               GlTranslation(-glm::vec3(13909.0f, 14482.0f, 543.0f));
    ExpectNear(pass.view, expected, 2e-3f);
}

// DAT_07d36899: without the X rotation, only Y and Z.
TEST(Pass3DTest, SkipXRotationLeavesOnlyYAndZ)
{
    Camera3D camera = SomeCamera();
    camera.skipXRotation = true;
    const Pass3D pass = MakePass3D(Window1024, WorldRect, camera);

    const glm::mat4 expected =
        GlRotation(10.0f, 1) * GlRotation(-45.0f, 2) * GlTranslation(-glm::vec3(13909.0f, 14482.0f, 543.0f));
    ExpectNear(pass.view, expected, 2e-3f);
}

// 0x620480: the segment goes from the camera to the pixel's point at the camera's far distance (without the × 1.4),
// measured along its axis.
TEST(Pass3DTest, TheRayGoesThroughThePixel)
{
    const Camera3D camera = SomeCamera();
    const Pass3D pass = MakePass3D(Window1024, WorldRect, camera);

    const auto [origin, end] = pass.RayThrough(300.0f, 400.0f);

    EXPECT_NEAR(glm::distance(origin, camera.position), 0.0f, 0.05f);
    const glm::vec4 endInCamera = pass.view * glm::vec4(end, 1.0f);
    EXPECT_NEAR(endInCamera.z, -camera.farPlane, 0.5f);
    const std::optional<glm::vec2> projected = pass.Project(end);
    ASSERT_TRUE(projected.has_value());
    EXPECT_NEAR(projected->x, 300.0f, 0.01f);
}

// Q-C109: 0x6203c0 multiplies the vertical tangent by the screen height over the viewport's (DAT_006d5d64 /
// DAT_07d36860, 0x62045c..0x62046c), and not the horizontal one: the ray of a row other than the center one comes out
// further from the center than the pixel, 768 / 688 in this viewport.
TEST(Pass3DTest, TheRayIsStretchedVerticallyLikeTheOriginal)
{
    const Pass3D pass = MakePass3D(Window1024, WorldRect, SomeCamera());

    const auto [origin, end] = pass.RayThrough(300.0f, 400.0f);

    const glm::vec4 endInCamera = pass.view * glm::vec4(end, 1.0f);
    // The viewport is (0, 40, 1024, 688): row 400 from the top is GL's 368.
    const float dy = 2.0f * (368.0f - 40.0f) / 688.0f - 1.0f;
    const float exact = dy * pass.farPlane / pass.projection[1][1];
    EXPECT_NEAR(endInCamera.y, exact * 768.0f / 688.0f, 0.05f);
}

TEST(Pass3DTest, TheRayThroughTheCentreGoesStraightAhead)
{
    const Pass3D pass = MakePass3D(Window1024, WorldRect, SomeCamera());
    // The center of the viewport (0, 40, 1024, 688) with y from the top: 768 − 40 − 344.
    const auto [origin, end] = pass.RayThrough(512.0f, 384.0f);

    const glm::vec4 endInCamera = pass.view * glm::vec4(end, 1.0f);
    EXPECT_NEAR(endInCamera.x, 0.0f, 0.05f);
    EXPECT_NEAR(endInCamera.y, 0.0f, 0.05f);
}

// Project gives, with y from the top, the pixel the ray goes through: the same x, and y shifted from the viewport
// center by the screen height over the viewport's (Q-C109). A point behind the camera does not land on the screen.
TEST(Pass3DTest, ProjectIsTheInverseOfTheRay)
{
    const Camera3D camera = SomeCamera();
    const Pass3D pass = MakePass3D(Window800, WorldRect, camera);
    const float centreY = static_cast<float>(pass.screen.height) -
                          (static_cast<float>(pass.viewport.y) + static_cast<float>(pass.viewport.w) * 0.5f);
    const float stretch = static_cast<float>(pass.screen.height) / static_cast<float>(pass.viewport.w);
    for (const auto& [x, y] : {std::pair{0.0f, 40.0f}, std::pair{799.0f, 560.0f}, std::pair{123.0f, 321.0f}})
    {
        const auto [origin, end] = pass.RayThrough(x, y);
        const glm::vec3 halfway = (origin + end) * 0.5f;

        const std::optional<glm::vec2> projected = pass.Project(halfway);

        ASSERT_TRUE(projected.has_value()) << x << ", " << y;
        EXPECT_NEAR(projected->x, x, 0.01f);
        EXPECT_NEAR(projected->y, centreY + (y - centreY) * stretch, 0.01f);
    }
    const auto [origin, end] = pass.RayThrough(400.0f, 300.0f);
    EXPECT_FALSE(pass.Project(origin - (end - origin)).has_value());
}
} // namespace
