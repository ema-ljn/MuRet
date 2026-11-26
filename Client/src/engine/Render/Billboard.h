#pragma once

#include "EngineApi.h"
#include "Math/OriginalRotation.h"
#include "Render/SpriteVertex.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>

// The original's billboard: 0x621400(texture, position, width, height, light, rotation, u, v, du, dv), the quad of
// sprites and particles, in eye coordinates (after 0x6213e0, glLoadIdentity on the modelview), like the x87. Checked
// against the emulated original in 300 cases (tests/vectors/world55/billboard.txt). 06 § Los efectos del login.
namespace Client::Render
{
struct BillboardCorner
{
    glm::vec3 position{}; // eye space
    glm::vec2 uv{};
};

struct Billboard
{
    // In GL_QUADS order: bottom left, bottom right, top right and top left, with texture row 0 at the top.
    std::array<BillboardCorner, 4> corners{};
    Color color;
};

// Texture `0x5e4`: with 4 components it gets alpha 1, not the red.
inline constexpr std::uint16_t OpaqueAlphaBillboardSlot = 0x5e4;

// The camera view as 0x620370 stores it at 0x7d366f0: the 3 × 4 modelview, by rows.
[[nodiscard]] ENGINE_API Math::OriginalMatrix ViewRows(const glm::mat4& view);

// Shared setup without consuming RGB early; the endpoint places begin/lookup between the two.
[[nodiscard]] ENGINE_API std::array<BillboardCorner, 4> MakeBillboardGeometry(const Math::OriginalMatrix& view,
                                                                              const glm::vec3& position, float width,
                                                                              float height, float rotation,
                                                                              glm::vec2 uv, glm::vec2 uvSize);
[[nodiscard]] ENGINE_API Color MakeBillboardColor(const glm::vec3& light, std::int32_t selector, int components);

// 0x621400: `position` goes through `view` (0x5fccc0); the `width` × `height` quad lies in the screen plane, rotated
// `rotation` degrees about the view axis (0x5fc8f0 and 0x5fcbd0 with (0, 0, rotation)). Color: with a 3-component
// texture, `light` (glColor3fv); with 4, (r, g, b, r), except 0x5e4, with alpha 1.
[[nodiscard]] ENGINE_API Billboard MakeBillboard(const Math::OriginalMatrix& view, const glm::vec3& position,
                                                 float width, float height, const glm::vec3& light, float rotation,
                                                 glm::vec2 uv, glm::vec2 uvSize, std::uint16_t slot, int components);
} // namespace Client::Render
