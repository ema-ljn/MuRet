#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// The effects of Kundun's actions 3 and 4 (0x43d051 and 0x43d0fd, KalimaSiegeHelpers): the 0x128, with no model, which
// in its step drops ten 0x126 (Skill\phoenix, 0x527b37) with the /0 or the 0x125 (Skill\dragonhead, 0x527bb8 and
// 0x527c13) with the /1. Their creations (0x518f38, 0x51cebf and 0x51cdc9) and steps (0x527ad6, 0x52778c and 0x5276b7).
// The 0x125's draw is the case of 0x438fe0 that calls 0x6002e0 (0x43909e, DrawTheHighEffectBody in
// Effects/HighEffectDrawing), which only runs with the alpha at 0.01 or more (0x5ffdf0, 0x5ffe2a..0x5ffe3d): the 0x125
// is born with the alpha at 0 (0x51cdf5) and only its joint 0x6ee/0x12 lights it (0x53949a); the 0x126 and the 0x128 go
// by the common path. 06, "Los efectos con modelo de los tipos altos".
namespace Client::Kernel
{
struct ModelEffectArguments;
} // namespace Client::Kernel

namespace Client::Effects
{
struct ModelEffect;
class ModelEffects;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectContext;
struct HighEffectStepContext;

inline constexpr std::int16_t KundunSpiritEffect = 0x125;
inline constexpr std::int16_t KundunPhoenixEffect = 0x126;
inline constexpr std::int16_t KundunCallEffect = 0x128;

// 0x518f38..0x518f90, the 0x128 of slot `slot` after 0x518cf0's prefix: the /0 and the /1, life 30 (0x51cf97). The /2
// (that of Kundun's fall, 0x43f144) leaves the life at 40 and the key (+0x8a) at 0 and re-enters 0x518cf0 as the
// 0x127/0 with itself as owner (0x518f90, CreateTheKundunEcho in Effects/KundunFallEffects). The other subtypes, only
// the common part. No rand().
void BeginTheKundunCall(Effects::ModelEffects& effects, std::size_t slot);

// 0x518cf0(0x128, ...) on `effects` without the rest of the world, as 0x43d051 and 0x43d0fd ask for it: the prefix
// (Effects::ModelEffects::Begin) and BeginTheKundunCall. With the table full, nothing. Returns the slot.
[[nodiscard]] Core::Expected<std::optional<std::size_t>, std::string>
CreateTheKundunCall(Effects::ModelEffects& effects, const Kernel::ModelEffectArguments& arguments);

// 0x527ad6..0x527c51, the 0x128's step:
// - /0: with life at 30, ten 0x126 (0x527b37, i = 0, 8, …, 72) at its position and with its light, no owner, with
//   angles (0, 0, i + z + 320) on the x87 with a single fstp.
// - /1: with life at 30, twenty 0x125 (0x527bb8); then, with the key (+0x8a, MOVSX) between 6 and 9, another
//   (0x527c13); all at its position and with its light, with angles (0, 0, key · 30). Then the key, one more.
// - /2: with the key at 0 and life below 10, the key at 1.
// It cuts where the creation of one of them cuts, with what came before.
[[nodiscard]] Core::Expected<void, std::string> StepTheKundunCall(Effects::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);

// 0x51cebf..0x51cf6c, the 0x126: +0xd8 at 0.34f, life 20, +0x64 at −2, scale 0.7f, light at 0.5 and alpha at 0; the
// joint 0x613/0xf with the effect as owner, width 100 and key 0 (0x51cf45), at its position, with angles
// (0, 0, z · 0.01745329f); then the frame (+0x114) at rand() % 5. Any subtype. The 0x613/0xf (SpiritJoints.cpp) draws
// no rand() when born; in its step it writes the 0x126's angles, alpha and position (0x539249).
[[nodiscard]] Core::Expected<void, std::string> BeginTheKundunPhoenix(Effects::ModelEffect& effect,
                                                                      const HighEffectContext& context);

// 0x52778c..0x5277a9: with life below 5 (edi, 0x5208df), the alpha at life · 0.2f.
void StepTheKundunPhoenix(Effects::ModelEffect& effect);

// 0x51cdc9..0x51cebe: life 30, +0x64 at −2, scale 1, light at 0.5, alpha at 0 and z 100 more; the turn rand() % 360 and
// the radius rand() % 600 + 200, and the joint 0x6ee/0x12 with the effect as owner, width 100 and key 0 (0x51ce91), at
// (FSIN(turn) · radius + x, FCOS(turn) · radius + y, 0) with the previous x and y; then the angles at
// (−30, 0, rand() % 360). Any subtype. The 0x6ee/0x12 (SpiritJoints.cpp) draws no rand(); in its step it sets the
// 0x125's alpha and position (0x539494).
[[nodiscard]] Core::Expected<void, std::string> BeginTheKundunSpirit(Effects::ModelEffect& effect,
                                                                     const HighEffectContext& context);

// 0x5276b7..0x527787: the angles' z rand() % 10 + 10 more; with z between 300 and 600 (exclusive), if rand() & 1 gives
// 0, the joint 0x6ee/0xe with no owner, width 100 and key 0 (0x52777f), at (x, y, 350) with angles
// (0, 0, (rand() % 24) · 30).
[[nodiscard]] Core::Expected<void, std::string> StepTheKundunSpirit(Effects::ModelEffect& effect,
                                                                    const HighEffectStepContext& context);
} // namespace Client::Effects
