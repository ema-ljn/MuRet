#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// The 0x11b: the /1, the three rings that Kalima's ability 0x37 (0x439b20) drops over its owner, with the two joints
// 0x618/0xa and /0xb that follow its points +0x40 and +0x4c; the /0, the flash of the joint 0x613/0x13 (0x539b08). Its
// creation (0x51c782) and step (0x526507). Its draw is 0x6002e0's own case (0x601e68, DrawTheHighEffectBody in
// Effects/HighEffectDrawing), which in each pass leaves +0x40 and +0x4c at bones 2 and 3. 06, "Los efectos con modelo
// de los tipos altos".
namespace Client::Effects
{
class ModelEffects;
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectContext;
struct HighEffectStepContext;

inline constexpr std::int16_t RingEffect = 0x11b;

// 0x51c782..0x51c883, no rand(). The /0: life 1 and scale 2.3f. The /1: life 49, scale 2, +0x64 at −2, +0x68 at
// 0.5, the copied angles (+0x28) at 0, speed (0, −45, 0), the start (+0x17c), the destination (+0x1b0) and the
// points +0x40 and +0x4c at the position, and the joints 0x618/0xa and /0xb (0x51c857 and 0x51c875) at the position,
// with the effect as owner and width 30. Another subtype, nothing (0x51db73).
[[nodiscard]] Core::Expected<void, std::string> BeginTheRing(Effects::ModelEffect& effect,
                                                             const HighEffectContext& context);

// 0x518cf0(0x11b, point, angles, light, 0, 0, −1, 0, 0, 0, 0) from the step of the joint 0x613/0x13 (0x539b08): the
// prefix, life 1 and scale 2.3f (0x51c789), no rand(). Full, none.
std::optional<std::size_t> CreateTheRingSpark(Effects::ModelEffects& effects, const glm::vec3& position,
                                              const glm::vec3& angles, const glm::vec3& light);

// 0x526507..0x5266e5, the /1 with an owner (+0x108, the character): with life a multiple of 10, the destination
// (+0x1b0) at the position; the start at the owner 200 higher and 0x4e9810 turns toward it by 10 at a time. Two
// (rand() & 31) − 16 shake the copied angles (x · 0.2f and z · 0.8f), which are added to the angles and braked (0.6f
// and 0.8f). With under 10 life, the alpha times 0.909f, +0x68 times 0.833f and the angles' x 5 more until it passes
// 80; otherwise, with 0x5f93c0, under 100 above the terrain it rises (x at −5) and over 400 it goes down (x at 5), with
// the copied x at 0. Then, the particle 0x5d9/5 (light 1, scale 1.2f) and the dynamic light (l, l · 0.3f, l · 0.1f) of
// radius 4. Without an owner or with another subtype, nothing.
[[nodiscard]] Core::Expected<void, std::string> StepTheRing(Effects::ModelEffect& effect, float luminosity,
                                                            const HighEffectStepContext& context);
} // namespace Client::Effects
