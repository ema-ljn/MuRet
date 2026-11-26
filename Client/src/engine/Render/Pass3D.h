#pragma once

#include "Core/PixelSize.h"
#include "EngineApi.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/ext/vector_int4.hpp>

#include <optional>
#include <utility>

// A 3D pass as 0x620db0 builds it (06 § Cámara): a camera's viewport, perspective and view, without GL.
namespace Client::Render
{
// What 0x620db0 receives: a rectangle in 640 × 480, y from the top.
struct ViewportRect
{
    int x = 0;
    int y = 0;
    int width = 640;
    int height = 480;
};

struct Camera3D
{
    glm::vec3 position{};
    // In degrees: those of the three glRotatef, x the pitch and z the yaw (DAT_07d36868, 6c and 70).
    glm::vec3 angles{};
    // DAT_07d36899: skips the glRotatef in X.
    bool skipXRotation = false;
    float fieldOfView = 35.0f; // degrees, vertical
    float nearPlane = 20.0f;   // `near` and `far` are Windows macros
    // Without the × 1.4: MakePass3D applies it for the perspective, and the mouse ray reaches this far.
    float farPlane = 0.0f;
};

struct ENGINE_API Pass3D
{
    Core::PixelSize screen;
    // x, y from the bottom, width and height, in GL pixels: glViewport's.
    glm::ivec4 viewport{};
    glm::mat4 projection{1.0f};
    glm::mat4 view{1.0f};
    // The camera's, without the × 1.4.
    float farPlane = 0.0f;
    // glClear(GL_DEPTH_BUFFER_BIT) when the pass starts with the material (ModelBatch::Begin with initializeMaterial):
    // only the card portrait's (0x495949), which clears the world's depth before its model.
    bool clearDepth = false;

    // 0x620480's mouse segment: from the camera position to the point of screen pixel (x, y), y from the top, which
    // lies farPlane ahead of the camera along its axis.
    [[nodiscard]] std::pair<glm::vec3, glm::vec3> RayThrough(float x, float y) const;

    // Where a world point lands, in pixels with y from the top; std::nullopt if it is behind the camera.
    [[nodiscard]] std::optional<glm::vec2> Project(const glm::vec3& world) const;
};

// 0x620db0 on a `screen`-pixel screen: the rectangle scaled in integers (x · W / 640, y · H / 480), GL's y from the
// bottom (0x620d10: H − y − h), gluPerspective(fov, w / h, near, far · 1.4) with the viewport's width and height
// (0x6203c0, 0x6b8c00), and the view of glRotatef in Y, in X (unless skipXRotation) and in Z, and
// glTranslatef(−position).
[[nodiscard]] ENGINE_API Pass3D MakePass3D(Core::PixelSize screen, const ViewportRect& rect, const Camera3D& camera);
} // namespace Client::Render
