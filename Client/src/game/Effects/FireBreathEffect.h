#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// The effect 0x6ac of 0x4fd5f0's skill 0x31 (0x4fec15), without a model or drawing: it is born with forty joints
// 0x617/1 in two mouths, drops fire 0x5db/0xa to the sides each step and ends with sparks 0x5c5/2 and 0x51fef0's
// 0x5ee/0. Its creation (0x51f5fe) and its step (0x52c7f0). 06, "Los efectos con modelo de los tipos altos".
namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectContext;
struct HighEffectStepContext;

inline constexpr std::int16_t FireBreathEffect = 0x6ac;

// 0x51f5fe..0x51f7f8: life 10, +0xd8 at 1, the velocity (0, −30, 0) and the position shifted by (0, −20, 50) turned by
// its angles. Then two mouths, (−20, −20, 60) and (30, −20, 60) turned and added to the position the creation receives
// (not the shifted one): in each, twenty rounds of 18 with rand() % 20 and the joint 0x617/1 (scale 10, without an
// owner) there, with the angles (rand() % 20 + x + 5; the y that adds up the rounds; z). Any subtype.
[[nodiscard]] Core::Expected<void, std::string> BeginTheFireBreath(Effects::ModelEffect& effect,
                                                                   const HighEffectContext& context);

// 0x52c7f0..0x52c97c: the radius 2 dynamic light with the local light `luminosity` (0x5208c5) times (0.5; 0.5; 0.8f);
// the scale (rand() % 3 + 2) · (15 − life) · 0.05f; at 20 to each side (the angles with z 90 more), the start and on
// it the fire 0x5db/0xa with its angles, the light (1, 1, 1) and its scale. With life at 1, 0x51fef0(position, 1).
[[nodiscard]] Core::Expected<void, std::string> StepTheFireBreath(Effects::ModelEffect& effect, float luminosity,
                                                                  const HighEffectStepContext& context);
} // namespace Client::Effects
