#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// The 0x139 (Skill\Javelin), the three of 0x4fd5f0's skill 0x2d on or with the target as owner (0x504a3a, 0x504a72,
// 0x504aa9): its creation (0x51dacb) and step (0x529b86). It is drawn by the common path (0x52da92 → 0x602e90) and the
// tail animates it without moving it (0x52ccb0 → 0x52cd94). 06, "Los efectos con modelo de los tipos altos".
namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectContext;
struct HighEffectStepContext;

inline constexpr std::int16_t JavelinEffect = 0x139;

// 0x51dacb..0x51db6e: life 35, +0xe4 at 2, +0xd8 at 10, scale 1.2f, speed (0, −5, 0), the angles copied into +0x28, the
// start at the owner's position (+0x108; without an owner the original reads address 0x10, MuRet cuts) and z 150 more.
// Then, rand() % 80 + 10 (r): +0x28's z plus (subtype · r − r).
[[nodiscard]] Core::Expected<void, std::string> BeginTheJavelin(Effects::ModelEffect& effect,
                                                                const HighEffectContext& context);

// 0x529b86..0x529de9: with an owner, the start at its position with z 150 more; scale 0.015f more. With 25 or more
// life, +0xe4 10 more and it advances with +0x28. Otherwise, below 20, 0x4e9810 turns +0x28 toward the start by +0xd8
// at a time: within 100, to the start, scale 0.04f more and, with life a multiple of 3, the particle 0x6ad/1; farther,
// the particle always. Then (from 24), +0xd8 1.5 more, it advances with +0x28, +0xe4 at rand() % 30 + 30 and,
// above −50, the speed's y 8 less. Always: the angles' z plus +0xe4, +0x68 and the alpha at life · 0.1f, z at the
// start's plus (the /1) or minus (the /2) FSIN(life · 0.1f) · 30 and the dynamic light (1, 0.6f, 0.3f) of radius 2.
[[nodiscard]] Core::Expected<void, std::string> StepTheJavelin(Effects::ModelEffect& effect,
                                                               const HighEffectStepContext& context);
} // namespace Client::Effects
