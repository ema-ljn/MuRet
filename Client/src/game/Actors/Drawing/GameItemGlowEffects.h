#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"

#include "Models/Shine.h"

#include <glm/ext/vector_float2.hpp>
#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace Client::Actors::Drawing
{
struct DrawnObject;
struct ItemDrawContext;
// 0x69fd3c: truncated FTOL64, with the signed low word the callers keep.
[[nodiscard]] std::int32_t ItemClockWord(float time);

// What 0x4ecc30 takes from the clock for the UVs of a glow pass: the signed low word for the periods, its phase `t`
// and, only in Chrome4 and Chrome5, the PC53 cosine and sine (0x4ed0fb..0x4ed11a). It does not depend on the normal, so
// it is the same at every corner: it is computed once per pass, not per corner.
struct ItemShineClock
{
    std::int32_t word = 0;
    float t = 0.0f;
    float chrome4Cosine = 0.0f;
    float chrome4Sine = 0.0f;
};

// 0x4ecc30: the clock of pass `pass`, with the full float for the phases.
[[nodiscard]] ItemShineClock ItemShineClockOf(Models::ShinePass pass, Core::Duration clock);

// 0x4ecc30: the UVs of a corner with the normal in the world and its pass's clock.
[[nodiscard]] glm::vec2 ItemShineTexCoords(Models::ShinePass pass, const glm::vec3& normal,
                                           const ItemShineClock& clock);

// 0x60be05..0x60c5c3: the items' stores and emitters, on the caller's retained pose.
[[nodiscard]] Core::Expected<void, std::string> PrepareItemGlowEffects(DrawnObject& object, Core::Duration clock,
                                                                       std::uint8_t level,
                                                                       const ItemDrawContext& context,
                                                                       std::optional<float>& storedAlpha);
} // namespace Client::Actors::Drawing
