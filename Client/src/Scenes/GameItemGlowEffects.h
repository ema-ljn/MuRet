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

// 4ecc30: reloj float completo para las fases y palabra baja firmada para los períodos.
[[nodiscard]] glm::vec2 ItemShineTexCoords(Render::Models::ShinePass pass, const glm::vec3& normal,
                                           Core::Duration clock);

// 60be05..60c5c3: stores y emisores de los ítems, sobre la pose retenida del caller.
[[nodiscard]] Core::Expected<void, std::string> PrepareItemGlowEffects(DrawnObject& object, Core::Duration clock,
                                                                       std::uint8_t level,
                                                                       const ItemDrawContext& context,
                                                                       std::optional<float>& storedAlpha);
} // namespace Client::Scenes
