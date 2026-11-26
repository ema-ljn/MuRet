#pragma once

#include "EngineApi.h"
#include "Math/HalfLifeMath.h"

#include <glm/ext/vector_float3.hpp>

#include <array>

// The original's rotation as the x87 does it: a float wherever the binary stores one, 53 bits elsewhere.
// Render/Models/HalfLifeMath gives the same rotation through quaternions, but not the same bits. Verified against the
// emulated code: 0x5fc8f0 (AngleMatrix) and 0x5fcbd0 (VectorRotate) in 400 cases (tests/vectors/world55/rotation.txt),
// 0x5fccc0 (VectorTransform) and 0x4ec110 (TransformPosition) with the scepter glow
// (tests/vectors/world55/scepter.txt).
namespace Client::Math
{
// The 3 × 4 matrix of 0x5fc8f0, by rows (m[i · 4 + j]), with zero translation.
using OriginalMatrix = std::array<float, 12>;

// A BoneTransform (column-major, translation in column 3) as the original's row-major 3 × 4.
[[nodiscard]] ENGINE_API OriginalMatrix OriginalMatrixOf(const BoneTransform& m);

// 0x5fc8f0: the rotation from three angles in degrees (x, y, z; π/180 as a float at 0x6ba360).
[[nodiscard]] ENGINE_API OriginalMatrix OriginalAngleMatrix(const glm::vec3& degrees);

// Inverse of OriginalMatrixOf: the row-major 3 × 4 as a BoneTransform (translation in m[3]).
[[nodiscard]] ENGINE_API BoneTransform BoneTransformOf(const OriginalMatrix& rows);

// 0x5fca90 (R_ConcatTransforms) as the x87 does it: parent · child, each product exact in 53 bits, the sums in the
// binary's order (different per row) and each element stored as float.
[[nodiscard]] ENGINE_API OriginalMatrix OriginalConcatTransforms(const OriginalMatrix& parent,
                                                                 const OriginalMatrix& child);

// 0x5fcbd0: `v` by the rotation of `m`, no translation.
[[nodiscard]] ENGINE_API glm::vec3 OriginalVectorRotate(const glm::vec3& v, const OriginalMatrix& m);

// 0x5fcbd0 with input and output in the same vector: y reads the already rotated x, and z reads both.
ENGINE_API void OriginalVectorRotateInPlace(glm::vec3& v, const OriginalMatrix& m);

// 0x5fcc30: `v` by the transpose of the rotation of `m`, with the original's sum order.
[[nodiscard]] ENGINE_API glm::vec3 OriginalVectorIRotate(const glm::vec3& v, const OriginalMatrix& m);

// 0x5fccc0: `v` by the rotation and translation of `m`.
[[nodiscard]] ENGINE_API glm::vec3 OriginalVectorTransform(const glm::vec3& v, const OriginalMatrix& m);

// 0x4ec110 with Translate at 1: the point `offset` of a bone in the world, VectorTransform times the body scale (model
// +0x68) plus its origin (+0x6c).
[[nodiscard]] ENGINE_API glm::vec3 OriginalTransformPosition(const OriginalMatrix& bone, const glm::vec3& offset,
                                                             float bodyScale, const glm::vec3& bodyOrigin);
} // namespace Client::Math
