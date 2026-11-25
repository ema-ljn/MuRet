#include "Math/RoundedTransforms.h"

#include "Math/RoundedMath.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>
#include <glm/geometric.hpp>

namespace Client::Render::Models
{
glm::mat4 RoundedRotate(const glm::mat4& m, float radians, const glm::vec3& axis)
{
    const float c = RoundedCos(radians);
    const float s = RoundedSin(radians);
    const glm::vec3 unit = glm::normalize(axis);
    const glm::vec3 rest = (1.0f - c) * unit;
    const glm::mat3 turn(c + rest.x * unit.x, rest.x * unit.y + s * unit.z, rest.x * unit.z - s * unit.y,
                         rest.y * unit.x - s * unit.z, c + rest.y * unit.y, rest.y * unit.z + s * unit.x,
                         rest.z * unit.x + s * unit.y, rest.z * unit.y - s * unit.x, c + rest.z * unit.z);

    return glm::mat4(m[0] * turn[0][0] + m[1] * turn[0][1] + m[2] * turn[0][2],
                     m[0] * turn[1][0] + m[1] * turn[1][1] + m[2] * turn[1][2],
                     m[0] * turn[2][0] + m[1] * turn[2][1] + m[2] * turn[2][2], m[3]);
}

glm::mat4 RoundedPerspective(float fieldOfViewRadians, float aspect, float nearPlane, float farPlane)
{
    const float tangent = RoundedTan(fieldOfViewRadians / 2.0f);

    glm::mat4 projection(0.0f);
    projection[0][0] = 1.0f / (aspect * tangent);
    projection[1][1] = 1.0f / tangent;
    projection[2][2] = -(farPlane + nearPlane) / (farPlane - nearPlane);
    projection[2][3] = -1.0f;
    projection[3][2] = -(2.0f * farPlane * nearPlane) / (farPlane - nearPlane);
    return projection;
}
} // namespace Client::Render::Models
