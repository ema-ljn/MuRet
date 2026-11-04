#include "Render/Pass3D.h"

#include "App/WindowState.h"

#include <gtest/gtest.h>

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/geometric.hpp>

#include <cmath>
#include <numbers>
#include <optional>
#include <utility>

// Client::Render::MakePass3D: el viewport de 0x620db0, la perspectiva de gluPerspective con el far
// × 1,4 y la vista de los tres glRotatef y el glTranslatef; y el rayo del mouse de 0x620480 (06 §
// Cámara y § El picking).
namespace
{
using Client::App::PixelSize;
using Client::Render::Camera3D;
using Client::Render::MakePass3D;
using Client::Render::Pass3D;
using Client::Render::ViewportRect;

constexpr PixelSize Window1024{1024, 768};
constexpr PixelSize Window800{800, 600};

// El pase del mundo de la selección y del login (0x620db0(0, 25, 640, 430)).
constexpr ViewportRect WorldRect{0, 25, 640, 430};

// cot(17,5°): la [1][1] de la perspectiva de 35° (06 § Cámara, la traza de la selección).
constexpr float CotangentOf17_5 = 3.171595f;

// Una cámara cualquiera, con ángulos en los tres ejes para que se note el orden.
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

// La matriz de glRotatef(degrees, eje), como la da la especificación de GL 1.x, por columnas.
glm::mat4 GlRotation(float degrees, int axis)
{
    const double radians = static_cast<double>(degrees) * std::numbers::pi / 180.0;
    const auto c = static_cast<float>(std::cos(radians));
    const auto s = static_cast<float>(std::sin(radians));
    glm::mat4 m(1.0f);
    const int a = (axis + 1) % 3;
    const int b = (axis + 2) % 3;
    m[a][a] = c;
    m[b][b] = c;
    m[a][b] = s;  // columna a, fila b
    m[b][a] = -s; // columna b, fila a
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

// x · W / 640 e y · H / 480 en enteros: en 800 × 600, 25 · 600 / 480 = 31 y 430 · 600 / 480 = 537,
// y la y de GL es 600 − 31 − 537 = 32.
TEST(Pass3DTest, ScalesTheViewportInIntegers)
{
    EXPECT_EQ(MakePass3D(Window1024, WorldRect, SomeCamera()).viewport, glm::ivec4(0, 40, 1024, 688));
    EXPECT_EQ(MakePass3D(Window800, WorldRect, SomeCamera()).viewport, glm::ivec4(0, 32, 800, 537));
}

// 0x620d10: glViewport(x, H − y − h, w, h). La franja de arriba, 0x620db0(0, 0, 640, 48), da el
// 0,692,1024,76 de la traza.
TEST(Pass3DTest, TheGlYIsFromTheBottom)
{
    EXPECT_EQ(MakePass3D(Window1024, ViewportRect{0, 0, 640, 48}, SomeCamera()).viewport, glm::ivec4(0, 692, 1024, 76));
    EXPECT_EQ(MakePass3D(PixelSize{640, 480}, ViewportRect{100, 50, 200, 100}, SomeCamera()).viewport,
              glm::ivec4(100, 330, 200, 100));
}

// gluPerspective con el ancho y el alto del viewport, no los de la ventana: 1024/688.
TEST(Pass3DTest, TheAspectIsThatOfTheViewport)
{
    const Pass3D pass = MakePass3D(Window1024, WorldRect, SomeCamera());

    EXPECT_NEAR(pass.projection[1][1], CotangentOf17_5, 1e-5f);
    EXPECT_NEAR(pass.projection[0][0], CotangentOf17_5 / (1024.0f / 688.0f), 1e-5f);
}

// 0x6b8c00: el far que llega a gluPerspective es el de la cámara por 1,4.
TEST(Pass3DTest, TheFarIsOnePointFourTimesTheCamera)
{
    Camera3D camera = SomeCamera();
    camera.farPlane = 2000.0f;
    const Pass3D pass = MakePass3D(Window1024, WorldRect, camera);

    // No se llaman `near` ni `far`: windows.h los define como macros vacías.
    const float farPlane = 2000.0f * 1.4f;
    const float nearPlane = 20.0f;
    EXPECT_NEAR(pass.projection[2][2], -(farPlane + nearPlane) / (farPlane - nearPlane), 1e-6f);
    EXPECT_NEAR(pass.projection[3][2], -2.0f * farPlane * nearPlane / (farPlane - nearPlane), 1e-4f);
    EXPECT_FLOAT_EQ(pass.farPlane, 2000.0f);
}

// La cámara de la selección en 1024 × 768, contra la traza (06 § Cámara): el viewport, la
// cotangente de 17,5°, el aspect 1024/688, el far de unos 11.520 por 1,4, y las rotaciones Y 0°,
// X −84,5° y Z −45°, en ese orden, con la posición restada al final.
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

// Con los tres ángulos distintos de 0 se ve el orden: Y, X y Z, como los tres glRotatef de
// 0x620db0. Las trazas tienen la Y en 0, y ahí X e Y conmutan.
TEST(Pass3DTest, TheRotationsGoInTheOrderYXZ)
{
    const Pass3D pass = MakePass3D(Window1024, WorldRect, SomeCamera());

    const glm::mat4 expected = GlRotation(10.0f, 1) * GlRotation(-84.5f, 0) * GlRotation(-45.0f, 2) *
                               GlTranslation(-glm::vec3(13909.0f, 14482.0f, 543.0f));
    ExpectNear(pass.view, expected, 2e-3f);
}

// DAT_07d36899: sin la rotación en X, solo Y y Z.
TEST(Pass3DTest, SkipXRotationLeavesOnlyYAndZ)
{
    Camera3D camera = SomeCamera();
    camera.skipXRotation = true;
    const Pass3D pass = MakePass3D(Window1024, WorldRect, camera);

    const glm::mat4 expected =
        GlRotation(10.0f, 1) * GlRotation(-45.0f, 2) * GlTranslation(-glm::vec3(13909.0f, 14482.0f, 543.0f));
    ExpectNear(pass.view, expected, 2e-3f);
}

// 0x620480: el segmento va de la cámara al punto del píxel a la distancia del far de la cámara (sin
// el × 1,4), medida sobre su eje.
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
    EXPECT_NEAR(projected->y, 400.0f, 0.01f);
}

// En el centro del viewport, el rayo va por el eje de la cámara.
TEST(Pass3DTest, TheRayThroughTheCentreGoesStraightAhead)
{
    const Pass3D pass = MakePass3D(Window1024, WorldRect, SomeCamera());
    // El centro del viewport (0, 40, 1024, 688) con la y desde arriba: 768 − 40 − 344.
    const auto [origin, end] = pass.RayThrough(512.0f, 384.0f);

    const glm::vec4 endInCamera = pass.view * glm::vec4(end, 1.0f);
    EXPECT_NEAR(endInCamera.x, 0.0f, 0.05f);
    EXPECT_NEAR(endInCamera.y, 0.0f, 0.05f);
}

// Project da, con la y desde arriba, el píxel por el que pasa el rayo; un punto detrás de la cámara
// no cae en la pantalla.
TEST(Pass3DTest, ProjectIsTheInverseOfTheRay)
{
    const Camera3D camera = SomeCamera();
    const Pass3D pass = MakePass3D(Window800, WorldRect, camera);
    for (const auto& [x, y] : {std::pair{0.0f, 40.0f}, std::pair{799.0f, 560.0f}, std::pair{123.0f, 321.0f}})
    {
        const auto [origin, end] = pass.RayThrough(x, y);
        const glm::vec3 halfway = (origin + end) * 0.5f;

        const std::optional<glm::vec2> projected = pass.Project(halfway);

        ASSERT_TRUE(projected.has_value()) << x << ", " << y;
        EXPECT_NEAR(projected->x, x, 0.01f);
        EXPECT_NEAR(projected->y, y, 0.01f);
    }
    const auto [origin, end] = pass.RayThrough(400.0f, 300.0f);
    EXPECT_FALSE(pass.Project(origin - (end - origin)).has_value());
}
} // namespace
