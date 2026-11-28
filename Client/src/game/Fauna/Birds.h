#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <system_error>

#include "Fauna/IBirdStepServices.h"

namespace Client::Crt
{
class CrtRandom;
}

// Credited pure parts of 0x446120/0x446b60; the canonical state and the RNG belong to Core.
namespace Client::Fauna
{
struct BirdStepResult
{
    std::size_t visited;
    std::size_t finalized;
    std::error_code error;
};

// 0x446120: requires exactly the views of the 40 PHYSICAL slots of the shared canonical pool: birds[i].data() ==
// canonicalBase + i*BirdRecordSize, order/base stable for the whole call, with no duplicates, overlaps, shifts or
// substitutions; the span of views is not mutated either. Main guarantees this association before invoking the step
// and uses the same base/count 40 in services (0x445ec0→0x4e9a40 excludes by index). A binding precondition; not
// another pool or identity store. Keeps prefixes on error. Approved PC53/RN profile: double R53 per operation and F32
// stores, without contraction/fast-math and with RN set by the binding; no early FP guard/probe.
// SinSpill/Wave/AudioDistance/ConvertTileCoordinate still require an explicit numeric backend.
[[nodiscard]] BirdStepResult StepBirds(std::span<BirdRecord> birds, IBirdStepServices& services);

// Prefix for a stable map. Does not consume the terminal draw of Kalima's index 2: the full step must do it
// separately before leaving, besides the gates of the two processable slots.
[[nodiscard]] std::size_t BirdStepCount(std::int32_t map);

// Views of the actor's canonical fields, without another pool or copy of the animation state.
struct BirdStepTail
{
    std::uint8_t& alive;
    std::int32_t& state;
    std::int32_t& timer;
    glm::vec3& angles;
};

// Ordinary tail 0x4468df..0x44696a, AFTER 0x445ec0 and BEFORE the audio/distance/0x4e9980. Rereads map and
// tileAttributes only with timer<=0; the turn needs map 7 and the exact word 1. Always deactivates with state>=2 and
// decrements the timer, even after deactivating. Does not include other handlers.
void FinishOrdinaryBirdMotion(const BirdStepTail& bird, const std::int32_t& map, const std::uint16_t& tileAttributes);

// Color of the AE/BC cases after the render. BC must call it after its first transform and reuse the result for both
// sprites, without another draw or reinitialization between them. Other types: nullopt without consuming RNG; does
// not represent the rest of the drawing or its effects.
[[nodiscard]] std::optional<glm::vec3> BirdGlowLight(std::int16_t model, Crt::CrtRandom& random);
} // namespace Client::Fauna
