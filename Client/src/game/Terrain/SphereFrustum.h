#pragma once

#include "Render/Pass3D.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>

// Los cinco planos de 0x5fbb00 y la prueba de esfera de 0x5fbf00 (06, "El frustum del juego").
namespace Client::World
{
using FrustumPoints = std::array<glm::vec3, 5>;

struct SphereFrustum
{
    std::array<glm::vec3, 5> normals{};
    std::array<float, 5> offsets{};
};

// Cámara y esquinas lejanas con la transpuesta de la vista, como TerrainRangeOf. Geometría finita.
[[nodiscard]] FrustumPoints FrustumPointsOf(const glm::mat4& view, const Render::Camera3D& camera,
                                            float widthFactor = 1.0f);
// Cuatro lados y el plano lejano, sin near. Requiere una cámara con cinco triángulos no degenerados,
// de largo finito: far positivo, ancho y alto no nulos y vista invertible (FaceNormal, 0x5fd000).
[[nodiscard]] SphereFrustum SphereFrustumOf(const glm::mat4& view, const Render::Camera3D& camera,
                                            float widthFactor = 1.0f);
// ((nx*x + nz*z) + ny*y) + d >= -radius en 53 bits. La igualdad pasa; sin orden corta.
[[nodiscard]] bool SphereVisible(const SphereFrustum& frustum, const glm::vec3& center, float radius);
} // namespace Client::World
