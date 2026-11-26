#include "Render/Pass3D.h"

#include "Core/PixelSize.h"
#include "Math/RoundedTransforms.h"

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
constexpr int OriginalWidth = 640;
constexpr int OriginalHeight = 480;

// 0x6b8c00: scales the far that reaches gluPerspective.
constexpr float FarScale = 1.4f;

constexpr glm::vec3 XAxis{1.0f, 0.0f, 0.0f};
constexpr glm::vec3 YAxis{0.0f, 1.0f, 0.0f};
constexpr glm::vec3 ZAxis{0.0f, 0.0f, 1.0f};

// 0x620db0: the rectangle scaled in integers and GL's y from the bottom (0x620d10).
glm::ivec4 ScaledViewport(Core::PixelSize screen, const ViewportRect& rect)
{
    const int x = rect.x * screen.width / OriginalWidth;
    const int y = rect.y * screen.height / OriginalHeight;
    const int width = rect.width * screen.width / OriginalWidth;
    const int height = rect.height * screen.height / OriginalHeight;
    return {x, screen.height - y - height, width, height};
}

// glRotatef in Y, in X if needed, in Z, and glTranslatef(−position): each multiplies on the right, as on GL's stack.
glm::mat4 CameraView(const Camera3D& camera)
{
    glm::mat4 view = Math::RoundedRotate(glm::mat4(1.0f), glm::radians(camera.angles.y), YAxis);
    if (!camera.skipXRotation)
    {
        view = Math::RoundedRotate(view, glm::radians(camera.angles.x), XAxis);
    }
    view = Math::RoundedRotate(view, glm::radians(camera.angles.z), ZAxis);
    return glm::translate(view, -camera.position);
}
} // namespace

Pass3D MakePass3D(Core::PixelSize screen, const ViewportRect& rect, const Camera3D& camera)
{
    Pass3D pass;
    pass.screen = screen;
    pass.viewport = ScaledViewport(screen, rect);
    const float aspect = static_cast<float>(pass.viewport.z) / static_cast<float>(pass.viewport.w);
    pass.projection = Math::RoundedPerspective(glm::radians(camera.fieldOfView), aspect, camera.nearPlane,
                                               camera.farPlane * FarScale);
    pass.view = CameraView(camera);
    pass.farPlane = camera.farPlane;
    return pass;
}

// 0x620480 builds the point (dx · tx · d, −dy · ty · d, −d) in camera space, with d the camera's far and 0x6203c0's
// tangents, and takes it to the world with the inverse view. Here dx and dy are the pixel's in the viewport, −1 to 1,
// and the tangents come from the perspective itself; the vertical one times screen height over viewport height, as
// 0x6203c0 builds it (Q-C109): the ray misses the pixel in y except on the center row.
std::pair<glm::vec3, glm::vec3> Pass3D::RayThrough(float x, float y) const
{
    const float glY = static_cast<float>(screen.height) - y;
    const float dx = 2.0f * (x - static_cast<float>(viewport.x)) / static_cast<float>(viewport.z) - 1.0f;
    const float dy = 2.0f * (glY - static_cast<float>(viewport.y)) / static_cast<float>(viewport.w) - 1.0f;
    const float tangentX = 1.0f / projection[0][0];
    const float tangentY =
        1.0f / projection[1][1] * (static_cast<float>(screen.height) / static_cast<float>(viewport.w));
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
