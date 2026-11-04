#include "Render/Pass3D.h"

#include "App/WindowState.h"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_float4.hpp>
#include <glm/ext/vector_int4.hpp>
#include <glm/matrix.hpp>
#include <glm/trigonometric.hpp>

#include <cmath>
#include <optional>
#include <utility>

namespace Client::Render
{
namespace
{
// El 640 × 480 del original.
constexpr int OriginalWidth = 640;
constexpr int OriginalHeight = 480;

// 0x6b8c00: el far que llega a gluPerspective.
constexpr float FarScale = 1.4f;

// Los ejes de los tres glRotatef.
constexpr glm::vec3 XAxis{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 YAxis{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 ZAxis{0.0f, 0.0f, 1.0f};

// 0x620db0: el rectángulo escalado en enteros y la y de GL desde abajo (0x620d10).
glm::ivec4 ScaledViewport(App::PixelSize screen, const ViewportRect& rect)
{
    const int x = rect.x * screen.width / OriginalWidth;
    const int y = rect.y * screen.height / OriginalHeight;
    const int width = rect.width * screen.width / OriginalWidth;
    const int height = rect.height * screen.height / OriginalHeight;
    return {x, screen.height - y - height, width, height};
}

// glRotatef en Y, en X si va, en Z, y glTranslatef(−posición): cada uno multiplica a la derecha,
// como en la pila de GL.
glm::mat4 CameraView(const Camera3D& camera)
{
    glm::mat4 view = glm::rotate(glm::mat4(1.0f), glm::radians(camera.angles.y), YAxis);
    if (!camera.skipXRotation)
    {
        view = glm::rotate(view, glm::radians(camera.angles.x), XAxis);
    }
    view = glm::rotate(view, glm::radians(camera.angles.z), ZAxis);
    return glm::translate(view, -camera.position);
}
} // namespace

Pass3D MakePass3D(App::PixelSize screen, const ViewportRect& rect, const Camera3D& camera)
{
    Pass3D pass;
    pass.screen = screen;
    pass.viewport = ScaledViewport(screen, rect);
    const float aspect = static_cast<float>(pass.viewport.z) / static_cast<float>(pass.viewport.w);
    pass.projection =
        glm::perspective(glm::radians(camera.fieldOfView), aspect, camera.nearPlane, camera.farPlane * FarScale);
    pass.view = CameraView(camera);
    pass.farPlane = camera.farPlane;
    return pass;
}

// 0x620480 arma el punto (dx · tx · d, −dy · ty · d, −d) en la cámara, con d el far de la cámara y
// las tangentes de la perspectiva, y lo lleva al mundo con la inversa de la vista. Acá dx y dy son
// las del píxel en el viewport, de −1 a 1, y las tangentes salen de la perspectiva misma.
std::pair<glm::vec3, glm::vec3> Pass3D::RayThrough(float x, float y) const
{
    const float glY = static_cast<float>(screen.height) - y;
    const float dx = 2.0f * (x - static_cast<float>(viewport.x)) / static_cast<float>(viewport.z) - 1.0f;
    const float dy = 2.0f * (glY - static_cast<float>(viewport.y)) / static_cast<float>(viewport.w) - 1.0f;
    const float tangentX = 1.0f / projection[0][0];
    const float tangentY = 1.0f / projection[1][1];
    const glm::vec4 inCamera(dx * tangentX * farPlane, dy * tangentY * farPlane, -farPlane, 1.0f);
    const glm::mat4 toWorld = glm::inverse(view);
    const glm::vec3 origin(toWorld * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    const glm::vec3 end(toWorld * inCamera);
    return {origin, end};
}

std::optional<glm::vec2> Pass3D::Project(const glm::vec3& world) const
{
    const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f)
    {
        return std::nullopt;
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float glX = static_cast<float>(viewport.x) + (ndcX + 1.0f) * 0.5f * static_cast<float>(viewport.z);
    const float glY = static_cast<float>(viewport.y) + (ndcY + 1.0f) * 0.5f * static_cast<float>(viewport.w);
    return glm::vec2(glX, static_cast<float>(screen.height) - glY);
}
} // namespace Client::Render
