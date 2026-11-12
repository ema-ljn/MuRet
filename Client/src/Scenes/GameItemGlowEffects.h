#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include "Render/Models/Shine.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
struct DrawnObject;
struct ItemDrawContext;
// 69fd3c: FTOL64 truncado, con la palabra baja firmada que conservan los callers.
[[nodiscard]] std::int32_t ItemClockWord(float time);

// Lo que 4ecc30 saca del reloj para las UV de un pase de brillo: la palabra baja firmada para los
// períodos, su fase `t` y, solo en Chrome4 y Chrome5, el coseno y el seno PC53 (0x4ed0fb..0x4ed11a). No depende de la
// normal, así que es el mismo en todas las esquinas: se saca una vez por pase y no por esquina (#306).
struct ItemShineClock
{
    std::int32_t word = 0;
    float t = 0.0f;
    float chrome4Cosine = 0.0f;
    float chrome4Sine = 0.0f;
};

// 4ecc30: el reloj del pase `pass`, con el float completo para las fases.
[[nodiscard]] ItemShineClock ItemShineClockOf(Render::Models::ShinePass pass, Core::Duration clock);

// 4ecc30: las UV de una esquina con la normal en el mundo y el reloj de su pase.
[[nodiscard]] glm::vec2 ItemShineTexCoords(Render::Models::ShinePass pass, const glm::vec3& normal,
                                           const ItemShineClock& clock);

// 60be05..60c5c3: stores y emisores de los ítems, sobre la pose retenida del caller.
[[nodiscard]] Core::Expected<void, std::string> PrepareItemGlowEffects(DrawnObject& object, Core::Duration clock,
                                                                       std::uint8_t level,
                                                                       const ItemDrawContext& context,
                                                                       std::optional<float>& storedAlpha);
} // namespace Client::Scenes
