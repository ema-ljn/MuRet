#pragma once

#include "EngineApi.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

// glm::rotate and glm::perspective with the sine, cosine and tangent of RoundedMath.h. glm takes them from the C
// library, whose results change with the system.
namespace Client::Render::Models
{
[[nodiscard]] ENGINE_API glm::mat4 RoundedRotate(const glm::mat4& m, float radians, const glm::vec3& axis);

[[nodiscard]] ENGINE_API glm::mat4 RoundedPerspective(float fieldOfViewRadians, float aspect, float nearPlane,
                                                      float farPlane);
} // namespace Client::Render::Models
