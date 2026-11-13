#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>
#include <string>

namespace Client::Render::Models
{
class CrtRandom;
}

// CL9 hito 4: 0x517a20, los blancos de una skill alrededor de un punto y el C1 10 que los manda (FoxAnti: 1D). Lo llama
// el choque de las flechas de skill (0x520170 en 0x520497, Scenes/ArrowEffects); la matriz lo pone en la CL11 (06,
// "La flecha").
namespace Client::Scenes
{
class WorldState;

// Los seis argumentos de 0x517a20.
struct SkillHitsRequest
{
    // El lugar de la skill en el registro propio (el byte de +0x6c + lugar, [0x7681eb0]).
    std::uint8_t skillIndex = 0;
    glm::vec3 point{};
    float radius = 0.0f;
    // La cuenta de la tanda, que va en el paquete (+0x8c de la flecha).
    std::uint8_t volley = 0;
    // La clave (+0x8a de la flecha): un jugador (+0x88 en 1) entra solo con ella en +0x1f4.
    std::uint16_t key = 0;
    // Lo que va detrás de cada clave en el paquete.
    std::uint8_t extra = 0;
};

// 0x517a20: la skill del lugar con el registro propio abierto y cerrado (3.848 rand() si queda en 0); en el mapa 30
// con el asedio prendido corta (0x517b1e, CL9.5). Después, de los 400 en orden, los que están vivos, se ven
// (+0x16c), no son el héroe ([0x743a170]) ni se están muriendo (+0x389), a `radius` o menos en el plano (la raíz en la
// x87 con PC53, guardada en float; NaN entra), y son monstruos (+0x88 en 2) o el jugador de la clave: hasta cinco.
// Con la skill 8 o 9, a cada uno con +0x394 en 0 le pone +0x398 en 10. Sin ninguno, false. Con "webzen" en el nombre
// del héroe, true sin mandar. Si no, el C1 10 (Protocol::Skill::WriteSkillHits) con la celda del punto (ftol de
// 0x69fd3c; fuera del entero, corta en 0x517deb) y en C3: sus dos rand() (el relleno y el serial) salen también sin
// `send`. true.
[[nodiscard]] Core::Expected<bool, std::string>
HitTheTargetsAround(const SkillHitsRequest& request, WorldState& world, Render::Models::CrtRandom& random,
                    const std::function<void(Protocol::Outgoing)>& send);
} // namespace Client::Scenes
