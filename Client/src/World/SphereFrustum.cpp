#include "World/SphereFrustum.h"

#include "Render/Pass3D.h"
#include "World/FaceNormal.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>

namespace Client::World
{
namespace
{
// 0x5fbb00: far por 0,9, medio fov, margen y proporción del alto, con sus bits del original.
constexpr float FarFactor = std::bit_cast<float>(0x3F666666u);
constexpr float HalfFieldOfView = std::bit_cast<float>(0x3C0EFA33u);
constexpr float WidthMargin = 100.0f;
constexpr float HeightFactor = 0.75f;

float F32(double value)
{
    return static_cast<float>(value);
}
} // namespace

FrustumPoints FrustumPointsOf(const glm::mat4& view, const Render::Camera3D& camera, float widthFactor)
{
    const float depth = F32(static_cast<double>(camera.farPlane) * FarFactor);
    const double halfWidth =
        (std::tan(static_cast<double>(camera.fieldOfView) * HalfFieldOfView) * depth * widthFactor) + WidthMargin;
    const float w = F32(halfWidth);
    const float h = F32(halfWidth * HeightFactor);
    const FrustumPoints local{glm::vec3(0.0f), glm::vec3(-w, h, -depth), glm::vec3(w, h, -depth),
                              glm::vec3(w, -h, -depth), glm::vec3(-w, -h, -depth)};
    // 0x5fcc30 guarda el giro transpuesto en f32; 0x5fbbdb suma después la posición de cámara.
    const auto m = [&view](int row, int column) { return static_cast<double>(view[column][row]); };
    FrustumPoints points{};
    for (std::size_t k = 0; k < points.size(); ++k)
    {
        const double x = local[k].x;
        const double y = local[k].y;
        const double z = local[k].z;
        const float ox = F32(((m(2, 0) * z) + (m(1, 0) * y)) + (x * m(0, 0)));
        const float oy = F32(((m(0, 1) * x) + (m(2, 1) * z)) + (m(1, 1) * y));
        const float oz = F32(((m(0, 2) * x) + (m(2, 2) * z)) + (m(1, 2) * y));
        points[k] = {F32(static_cast<double>(camera.position.x) + ox), F32(static_cast<double>(camera.position.y) + oy),
                     F32(static_cast<double>(camera.position.z) + oz)};
    }
    return points;
}

SphereFrustum SphereFrustumOf(const glm::mat4& view, const Render::Camera3D& camera, float widthFactor)
{
    const FrustumPoints points = FrustumPointsOf(view, camera, widthFactor);
    // Argumentos de 0x5fd000 en 0x5fbd7b..0x5fbdf6; los offsets eligen su origen aparte.
    constexpr std::array<std::array<std::size_t, 3>, 5> Faces{{{0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 1}, {3, 2, 1}}};
    constexpr std::array<std::size_t, 5> OffsetOrigins{0, 0, 0, 0, 1};
    SphereFrustum frustum;
    for (std::size_t k = 0; k < Faces.size(); ++k)
    {
        const auto& face = Faces[k];
        const glm::vec3& point = points[face[0]];
        frustum.normals[k] = FaceNormal(point, points[face[1]], points[face[2]]);
        const glm::vec3& normal = frustum.normals[k];
        const glm::vec3& origin = points[OffsetOrigins[k]];
        // 0x5fbdfb..0x5fbee5: z+y, después x, negado y guardado en f32; el consumidor usa otro orden.
        frustum.offsets[k] =
            F32(-(((static_cast<double>(normal.z) * origin.z) + (static_cast<double>(normal.y) * origin.y)) +
                  (static_cast<double>(normal.x) * origin.x)));
    }
    return frustum;
}

bool SphereVisible(const SphereFrustum& frustum, const glm::vec3& center, float radius)
{
    for (std::size_t k = 0; k < frustum.normals.size(); ++k)
    {
        const glm::vec3& normal = frustum.normals[k];
        const double distance =
            (((static_cast<double>(normal.x) * center.x) + (static_cast<double>(normal.z) * center.z)) +
             (static_cast<double>(normal.y) * center.y)) +
            frustum.offsets[k];
        if (!(distance >= -radius))
        {
            return false;
        }
    }
    return true;
}
} // namespace Client::World
