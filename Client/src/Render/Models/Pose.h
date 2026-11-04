#pragma once

#include "Render/Models/HalfLifeMath.h"
#include "Render/Models/Skeleton.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <span>

// La pose de un esqueleto como 0x4eb860 (Client/docs/research/06-render-escenas-y-ui.md, "El modelo
// en el binario" y "Cuánto avanza la animación").
namespace Client::Render::Models
{
// Lo que el objeto guarda de su animación (+0x111, +0x114, +0x112 y +0x118 del objeto).
struct AnimationState
{
    std::uint8_t action = 0;
    // Con la parte fraccionaria: la pose interpola con ella.
    float frame = 0.0f;
    // La acción y la key que guardó el avance al cruzar una (0x4ec200). El original guarda el frame
    // anterior en un float, y la pose lo trunca.
    std::uint8_t previousAction = 0;
    std::int16_t previousKey = 0;
};

// Lo que el modelo copia del objeto antes de posar (0x6001b0 en un personaje, 0x5ffdf0 en un monstruo
// o un NPC).
struct ObjectTransform
{
    glm::vec3 position{};
    // En grados: el giro de la raíz (+0x78 del modelo, 0x5fc8f0).
    glm::vec3 angles{};
    float scale = 1.0f;
    // +0x84 del modelo: se suma a la z de la raíz con lock_positions. Los que posan un objeto lo ponen
    // en 0 antes.
    float lockedRootHeight = 0.0f;
};

// 0x4eb860, como lo llama 0x6001b0 para el cuerpo de un personaje (0x50ba30 → 0x6001b0(objeto, 1, …),
// con el último argumento de la pose en 0): la raíz gira con los ángulos del objeto, y la escala y la
// posición las pone SkinMesh en cada vértice.
// Escribe un BoneTransform por hueso en `bones`, del tamaño del esqueleto; los dummy no se tocan.
//   - Interpola entre la key de la acción anterior (previousKey) y la de ahora (el frame truncado),
//     con la parte fraccionaria del frame: el slerp de 0x5fce90 entre los cuaterniones, salvo que
//     0x5fc700 los dé iguales, y lineal entre las posiciones. Una acción o una key que no existe es
//     la 0.
//   - El hueso de la cabeza (Skeleton::headBone) resta headAngles.x a su ángulo x y headAngles.y a su
//     z, en grados, y pasa a cuaternión en el momento.
//   - El hueso 0, si la acción anterior o la de ahora tiene lock_positions, toma x e y de la key 0 de
//     la de ahora, y suma lockedRootHeight a la z interpolada.
// Sin acciones en el esqueleto no hace nada. Devuelve false, sin escribir, si `bones` no es del
// tamaño del esqueleto, si a un hueso que se posa le falta una de las dos keys, o si su padre no es
// −1 ni un hueso de antes (un Skeleton armado a mano, que no pasó por MakeSkeleton).
[[nodiscard]] bool PoseSkeleton(const Skeleton& skeleton, const AnimationState& animation,
                                const ObjectTransform& object, const glm::vec2& headAngles,
                                std::span<BoneTransform> bones);

// 0x4eb860 con su séptimo argumento en 1, como posa 0x509340 lo que cuelga del personaje
// (Attachment.h): los huesos sin padre van con `root`, la que dejó quien llama en 0x68c4a1c, y no
// con el giro del objeto. Sin ángulos de cabeza y con +0x84 en 0. Devuelve false como
// PoseSkeleton.
[[nodiscard]] bool PoseAttached(const Skeleton& skeleton, const AnimationState& animation, const BoneTransform& root,
                                std::span<BoneTransform> bones);
} // namespace Client::Render::Models
