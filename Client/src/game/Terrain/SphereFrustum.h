#pragma once

#include "Render/Pass3D.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>

// The five planes of 0x5fbb00 and the sphere test of 0x5fbf00 (06, "El frustum del juego").
namespace Client::Terrain
{
using FrustumPoints = std::array<glm::vec3, 5>;

struct SphereFrustum
{
    std::array<glm::vec3, 5> normals{};
    std::array<float, 5> offsets{};
};

// Camera and far corners with the view's transpose, as TerrainRangeOf. Finite geometry.
[[nodiscard]] FrustumPoints FrustumPointsOf(const glm::mat4& view, const Render::Camera3D& camera,
                                            float widthFactor = 1.0f);
// Four sides and the far plane, no near. Requires a camera with five non-degenerate triangles of finite length:
// positive far, non-zero width and height and an invertible view (FaceNormal, 0x5fd000).
[[nodiscard]] SphereFrustum SphereFrustumOf(const glm::mat4& view, const Render::Camera3D& camera,
                                            float widthFactor = 1.0f);
// ((nx*x + nz*z) + ny*y) + d >= -radius in 53 bits. Equality passes; unordered fails.
[[nodiscard]] bool SphereVisible(const SphereFrustum& frustum, const glm::vec3& center, float radius);
} // namespace Client::Terrain
