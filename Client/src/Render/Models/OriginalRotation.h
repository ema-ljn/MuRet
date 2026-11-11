#pragma once

#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/vector_float3.hpp>

#include <array>

// El giro del original como lo hace la x87 (CL7.6 A): 0x5fc8f0 (AngleMatrix) y 0x5fcbd0 (VectorRotate),
// con cada float donde el binario guarda un float y en 53 bits donde no. Render/Models/HalfLifeMath da
// el mismo giro por cuaterniones, pero no los mismos bits. Verificado contra el código real emulado
// en 400 casos (tests/vectors/world55/rotation.txt). La CL7.6 B1 suma 0x5fccc0 (VectorTransform) y
// 0x4ec110 (TransformPosition), verificados con el brillo del cetro (tests/vectors/world55/scepter.txt).
namespace Client::Render::Models
{
// La matriz de 3 × 4 de 0x5fc8f0, por filas (m[i · 4 + j]), con la traslación en 0.
using OriginalMatrix = std::array<float, 12>;

// Una BoneTransform (por columnas, la traslación en la 3) como la 3 × 4 del original, por filas.
[[nodiscard]] OriginalMatrix OriginalMatrixOf(const BoneTransform& m);

// 0x5fc8f0: el giro de tres ángulos en grados (x, y, z; el π/180 en float de 0x6ba360).
[[nodiscard]] OriginalMatrix OriginalAngleMatrix(const glm::vec3& degrees);

// La inversa de OriginalMatrixOf: la 3 × 4 por filas como BoneTransform (la traslación en m[3]).
[[nodiscard]] BoneTransform BoneTransformOf(const OriginalMatrix& rows);

// 0x5fca90 (R_ConcatTransforms) como la x87: parent · child, cada producto exacto en 53 bits, las sumas
// en el orden del binario (distinto por fila) y cada elemento guardado en float.
[[nodiscard]] OriginalMatrix OriginalConcatTransforms(const OriginalMatrix& parent, const OriginalMatrix& child);

// 0x5fcbd0: `v` por el giro de `m`, sin la traslación.
[[nodiscard]] glm::vec3 OriginalVectorRotate(const glm::vec3& v, const OriginalMatrix& m);

// 0x5fcc30: `v` por la transpuesta del giro de `m`, con el orden de sumas del original.
[[nodiscard]] glm::vec3 OriginalVectorIRotate(const glm::vec3& v, const OriginalMatrix& m);

// 0x5fccc0: `v` por el giro y la traslación de `m`.
[[nodiscard]] glm::vec3 OriginalVectorTransform(const glm::vec3& v, const OriginalMatrix& m);

// 0x4ec110 con Translate en 1: el punto `offset` de un hueso en el mundo, VectorTransform por la escala
// del cuerpo (+0x68 del modelo) más su origen (+0x6c).
[[nodiscard]] glm::vec3 OriginalTransformPosition(const OriginalMatrix& bone, const glm::vec3& offset, float bodyScale,
                                                  const glm::vec3& bodyOrigin);
} // namespace Client::Render::Models
