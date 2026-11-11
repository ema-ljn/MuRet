#pragma once

#include "Render/Models/Cloth.h"
#include "Render/Models/HalfLifeMath.h"

#include <glm/ext/matrix_float3x3.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <vector>

// Lo que MuRet agrega entre dos frames del original, que el original no tiene (#307). La tela se simula
// una vez por frame del original (Q-C124), pero el cuerpo avanza en cada paso de lógica y se dibuja más
// seguido: en un dibujo intermedio, la tela va llevada con el hueso del que cuelga, del lugar que tenía
// en su última simulación al de ahora, como un sólido. La fila anclada cae sobre el hueso de ahora y el
// resto conserva la forma que le dio la simulación. Sin moverse el hueso, no se lleva nada.
namespace Client::Scenes
{
// El cuerpo con que se simuló la tela por última vez: sus huesos, copiados, porque los del ClothBody
// cambian con la pose de cada frame.
struct SimulatedClothBody
{
    std::vector<Render::Models::BoneTransform> bones;
    float scale = 1.0f;
    glm::vec3 position{};
};

// Un punto de la tela en el mundo lo pone 0x4ec110: posición + escala · (hueso · local). Llevarlo es
// sacarlo del marco del hueso de antes y ponerlo en el de ahora: hasta + lineal · (punto − desde).
struct ClothCarry
{
    glm::mat3 linear{1.0f};
    glm::vec3 from{};
    glm::vec3 to{};

    [[nodiscard]] glm::vec3 Carried(const glm::vec3& point) const
    {
        return to + linear * (point - from);
    }
};

[[nodiscard]] SimulatedClothBody SimulatedBodyOf(const Render::Models::ClothBody& body);

// Lo que se movió el hueso `bone` de `then` a `now`. Ninguno si el hueso, la escala y la posición no
// cambiaron (la tela se dibuja como quedó, bit a bit), si falta el hueso en alguno de los dos o si su
// giro no se puede invertir.
[[nodiscard]] std::optional<ClothCarry> ClothCarryOf(const SimulatedClothBody& then,
                                                     const Render::Models::ClothBody& now, std::int32_t bone);
} // namespace Client::Scenes
