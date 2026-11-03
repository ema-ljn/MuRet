#pragma once

#include <glm/ext/matrix_float4x3.hpp>
#include <glm/ext/quaternion_float.hpp>
#include <glm/ext/vector_float3.hpp>

// La matemática de Half-Life con que el original posa un modelo (la mathlib del studiomdl, en la
// x87): Client/docs/research/05-formatos-de-datos.md, "Modelos .bmd", y 06, "El modelo en el
// binario". La CL6.0 la escribió a mano y la comparó con glm (labs/cl6.0-huesos); acá van los tipos
// de glm, con glm donde da lo mismo y a mano lo que glm hace distinto (pregunta 5 del plan de la CL6).
namespace Client::Render::Models
{
// La 3 × 4 de Half-Life (m[fila][columna], la traslación en la columna 3) en un glm::mat4x3, que
// guarda por columnas: m[3] es la traslación. Copiar índices sin dar vuelta es otro giro.
using BoneTransform = glm::mat4x3;

// 0x5fcd20: el cuaternión del giro Rz · Ry · Rx de tres ángulos de Euler en radianes, con el medio
// ángulo. Es glm::quat(glm::vec3), la misma fórmula.
[[nodiscard]] glm::quat AngleQuaternion(const glm::vec3& radians);

// 0x5fce90: de p a q con t, como Half-Life y no como glm::slerp. Da vuelta q si Σ(p − q)² >
// Σ(p + q)², pasa a lineal si 1 − cos ≤ 1e-8 con el cos en float, y con p y q casi opuestos gira
// hacia un cuaternión perpendicular a p. No normaliza.
[[nodiscard]] glm::quat QuaternionSlerp(const glm::quat& p, const glm::quat& q, float t);

// 0x5fc700: las cuatro componentes a 0,001 o menos. Con dos keys así, la pose copia la primera y no
// llama al slerp (0x4eb860).
[[nodiscard]] bool QuaternionsAlmostEqual(const glm::quat& p, const glm::quat& q);

// 0x5fcdb0: el giro de un cuaternión unitario, con la traslación en 0. Es glm::mat3_cast.
[[nodiscard]] BoneTransform QuaternionMatrix(const glm::quat& q);

// 0x5fc8f0: el giro Rz · Ry · Rx de tres ángulos en grados (el π/180 en float de 0x6ba360), con la
// traslación en 0. Da el mismo giro que QuaternionMatrix(AngleQuaternion(…)) en radianes.
[[nodiscard]] BoneTransform AngleMatrix(const glm::vec3& degrees);

// 0x5fca90 (R_ConcatTransforms): parent · child, las dos como 4 × 4 con (0, 0, 0, 1) abajo. Con la
// del padre en el mundo y la del hueso relativa al padre, da la del hueso en el mundo.
[[nodiscard]] BoneTransform ConcatTransforms(const BoneTransform& parent, const BoneTransform& child);

// 0x5fccc0: un punto por el giro y la traslación de `m`.
[[nodiscard]] glm::vec3 VectorTransform(const glm::vec3& v, const BoneTransform& m);

// 0x5fcbd0: un vector solo por el giro de `m`, sin la traslación: lo que se hace con una normal.
[[nodiscard]] glm::vec3 VectorRotate(const glm::vec3& v, const BoneTransform& m);
} // namespace Client::Render::Models
