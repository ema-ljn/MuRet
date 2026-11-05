#include "Render/Billboard.h"

#include "Render/Models/OriginalRotation.h"
#include "Render/SpriteVertex.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace Client::Render
{
namespace
{
float F32(double value)
{
    return static_cast<float>(value);
}

// 0x5fccc0: el punto por la matriz de 3 × 4, cada componente en 53 bits con el orden de sus sumas, más
// la traslación.
glm::vec3 VectorTransform(const glm::vec3& v, const Models::OriginalMatrix& m)
{
    const auto at = [&m](std::size_t k) { return static_cast<double>(m[k]); };
    const double x = v.x;
    const double y = v.y;
    const double z = v.z;
    return {F32((((at(1) * y) + (at(2) * z)) + (x * at(0))) + at(3)),
            F32((((at(4) * x) + (at(5) * y)) + (at(6) * z)) + at(7)),
            F32((((at(8) * x) + (at(9) * y)) + (at(10) * z)) + at(11))};
}

constexpr int ThreeComponents = 3;
constexpr double Half = 0.5;
} // namespace

Models::OriginalMatrix ViewRows(const glm::mat4& view)
{
    Models::OriginalMatrix rows{};
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 4; ++j)
        {
            rows[static_cast<std::size_t>((i * 4) + j)] = view[j][i];
        }
    }
    return rows;
}

Billboard MakeBillboard(const Models::OriginalMatrix& view, const glm::vec3& position, float width, float height,
                        const glm::vec3& light, float rotation, glm::vec2 uv, glm::vec2 uvSize, std::uint16_t slot,
                        int components)
{
    const glm::vec3 eye = VectorTransform(position, view);
    // El medio ancho queda en la x87 (exacto) y también se guarda en float; el medio alto, en float.
    const double halfWidth = static_cast<double>(width) * Half;
    const float halfWidthFloat = F32(halfWidth);
    const float halfHeight = F32(static_cast<double>(height) * Half);
    Billboard billboard;
    auto& corners = billboard.corners;
    if (rotation == 0.0f)
    {
        const float left = F32(static_cast<double>(eye.x) - halfWidth);
        const float right = F32(static_cast<double>(eye.x) + static_cast<double>(halfWidthFloat));
        const float bottom = F32(static_cast<double>(eye.y) - static_cast<double>(halfHeight));
        const float top = F32(static_cast<double>(eye.y) + static_cast<double>(halfHeight));
        corners[0].position = {left, bottom, eye.z};
        corners[1].position = {right, bottom, eye.z};
        corners[2].position = {right, top, eye.z};
        corners[3].position = {left, top, eye.z};
    }
    else
    {
        const std::array<glm::vec3, 4> local{
            glm::vec3(-halfWidthFloat, -halfHeight, eye.z), glm::vec3(halfWidthFloat, -halfHeight, eye.z),
            glm::vec3(halfWidthFloat, halfHeight, eye.z), glm::vec3(-halfWidthFloat, halfHeight, eye.z)};
        const Models::OriginalMatrix spin = Models::OriginalAngleMatrix({0.0f, 0.0f, rotation});
        for (std::size_t k = 0; k < local.size(); ++k)
        {
            const glm::vec3 rotated = Models::OriginalVectorRotate(local[k], spin);
            corners[k].position = {F32(static_cast<double>(eye.x) + static_cast<double>(rotated.x)),
                                   F32(static_cast<double>(eye.y) + static_cast<double>(rotated.y)), rotated.z};
        }
    }
    const float uRight = F32(static_cast<double>(uv.x) + static_cast<double>(uvSize.x));
    const float vBottom = F32(static_cast<double>(uv.y) + static_cast<double>(uvSize.y));
    corners[0].uv = {uv.x, vBottom};
    corners[1].uv = {uRight, vBottom};
    corners[2].uv = {uRight, uv.y};
    corners[3].uv = {uv.x, uv.y};
    if (components == ThreeComponents)
    {
        billboard.color = Color{light.x, light.y, light.z, 1.0f};
    }
    else
    {
        billboard.color = Color{light.x, light.y, light.z, slot == OpaqueAlphaBillboardSlot ? 1.0f : light.x};
    }
    return billboard;
}
} // namespace Client::Render
