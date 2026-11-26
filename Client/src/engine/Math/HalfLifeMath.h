#pragma once

#include "EngineApi.h"

#include <glm/ext/matrix_float4x3.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>

// The Half-Life math the original poses a model with (studiomdl's mathlib, on the x87):
// Client/docs/research/05-formatos-de-datos.md, "Modelos .bmd", and 06, "El modelo en el binario". glm where it gives
// the same result, by hand what glm does differently (question 5 of the CL6 plan).
namespace Client::Math
{
// Half-Life's 3 × 4 (m[row][column], translation in column 3) in a glm::mat4x3, which is column-major: m[3] is the
// translation. Copying indices without transposing gives a different rotation.
using BoneTransform = glm::mat4x3;

// 0x5fcd20: the quaternion of the rotation Rz · Ry · Rx from three Euler angles in radians, with the half angle. Same
// formula as glm::quat(glm::vec3).
[[nodiscard]] ENGINE_API glm::quat AngleQuaternion(const glm::vec3& radians);

// 0x5fce90: from p to q by t, as Half-Life does and not as glm::slerp. Flips q if Σ(p − q)² > Σ(p + q)², and goes
// linear if 1 − cos ≤ 1e-8 with the cos in float. The branch for opposite p and q (towards a quaternion perpendicular
// to p) exists, but after the flip the product is not negative and only a NaN reaches it. Does not normalize.
[[nodiscard]] ENGINE_API glm::quat QuaternionSlerp(const glm::quat& p, const glm::quat& q, float t);

// 0x5fc700: all four components within 0.001. With two such keys the pose copies the first one and does not call the
// slerp (0x4eb860).
[[nodiscard]] ENGINE_API bool QuaternionsAlmostEqual(const glm::quat& p, const glm::quat& q);

// 0x5fcdb0: rotation of a unit quaternion, zero translation. Same as glm::mat3_cast.
[[nodiscard]] ENGINE_API BoneTransform QuaternionMatrix(const glm::quat& q);

// 0x5fc8f0: rotation Rz · Ry · Rx from three angles in degrees (π/180 as a float at 0x6ba360), zero translation. Same
// rotation as QuaternionMatrix(AngleQuaternion(…)) in radians.
[[nodiscard]] ENGINE_API BoneTransform AngleMatrix(const glm::vec3& degrees);

// 0x5fca90 (R_ConcatTransforms): parent · child, both as 4 × 4 with (0, 0, 0, 1) as the bottom row.
[[nodiscard]] ENGINE_API BoneTransform ConcatTransforms(const BoneTransform& parent, const BoneTransform& child);

// 0x5fccc0: a point by the rotation and translation of `m`.
[[nodiscard]] ENGINE_API glm::vec3 VectorTransform(const glm::vec3& v, const BoneTransform& m);

// 0x5fcbd0: rotation only, no translation (as for a normal).
[[nodiscard]] ENGINE_API glm::vec3 VectorRotate(const glm::vec3& v, const BoneTransform& m);
} // namespace Client::Math
