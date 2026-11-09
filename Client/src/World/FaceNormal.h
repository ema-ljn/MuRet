#pragma once

#include <glm/ext/vector_float3.hpp>

// 0x5fd000, la normal de una cara. La usan la luz del terreno al cargar un mundo (0x5f8b10, 06 § La
// luz del terreno en un punto) y el picking en cada triángulo (0x5fac90, 06 § El rayo contra el
// terreno): una sola implementación para los dos.
namespace Client::World
{
// (p2 − p1) × (p3 − p1), normalizada, con el perfil PC53/nearest del ASM y sin FMA: uy, uz, vz, nz y
// el largo quedan en el registro (53 bits); vy, nx, vx, ux y ny pasan por un float de la pila, y cada
// componente se divide por el largo en 53 bits antes de guardarse en float. No fija la CW del
// original ni reproduce flags o excepciones x87.
// Con largo 0 o NaN el original no escribe la normal; esta función no tiene ese camino y requiere un
// largo finito y no nulo. Los dos callers lo cumplen: sus triángulos tienen alturas finitas y anchos
// positivos en x y en y, así que nz es positivo (10.000 en la luz).
[[nodiscard]] glm::vec3 FaceNormal(const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3);
} // namespace Client::World
