#pragma once

#include <glm/ext/vector_float3.hpp>

#include <array>

// El giro del original como lo hace la x87 (CL7.6 A): 0x5fc8f0 (AngleMatrix) y 0x5fcbd0 (VectorRotate),
// con cada float donde el binario guarda un float y en 53 bits donde no. Render/Models/HalfLifeMath da
// el mismo giro por cuaterniones, pero no los mismos bits. Verificado contra el código real emulado
// en 400 casos (tests/vectors/world55/rotation.txt).
namespace Client::Render::Models
{
// La matriz de 3 × 4 de 0x5fc8f0, por filas (m[i · 4 + j]), con la traslación en 0.
using OriginalMatrix = std::array<float, 12>;

// 0x5fc8f0: el giro de tres ángulos en grados (x, y, z; el π/180 en float de 0x6ba360).
[[nodiscard]] OriginalMatrix OriginalAngleMatrix(const glm::vec3& degrees);

// 0x5fcbd0: `v` por el giro de `m`, sin la traslación.
[[nodiscard]] glm::vec3 OriginalVectorRotate(const glm::vec3& v, const OriginalMatrix& m);
} // namespace Client::Render::Models
