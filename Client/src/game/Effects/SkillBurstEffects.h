#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

// CL11, the high types' batch: the undrawn effects that only drop particles, light or a joint in their step, and the
// step of skill 0x2e's shot.
// - The 0x5ca, ability 0x11's ball (0x505053): creation 0x51e529, step 0x52abbe and its 0x518b60 branch.
// - The 0x5da, the 0x33's hit (0x4f4fb9): creation 0x51e773 and step 0x52acc3.
// - The 0x5be, ability 0x37's sweep (0x4f9fae): creation 0x51e397 and step 0x52a6d6.
// - The 0x13a, skill 0x2e's shot (its creation is Effects/ArrowEffects', CreateTheBlast): step 0x529dee.
// The first three are types from 0x151 on with no case in 0x52d620 or in the floor pass (0x52dc20): they are not
// drawn. The 0x13a goes by the common path (0x52da92 → 0x602e90). 06, "Los efectos con modelo de los tipos altos".
namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Effects
{
struct HighEffectStepContext;

inline constexpr std::int16_t SweepEmitterEffect = 0x5be; // drops ability 0x37's joint 0x61c/0
inline constexpr std::int16_t EnergyBallEffect = 0x5ca;   // ability 0x11's ball
inline constexpr std::int16_t FlameBurstEffect = 0x5da;   // the 0x33's hit
inline constexpr std::int16_t ArrowBlastEffect = 0x13a;   // skill 0x2e's shot

// 0x51e397..0x51e3eb, the 0x5be of any subtype: life 30, the light at 0.8f, the copied angles (+0x28) at (0, 0, z + 45)
// with the argument's angles' z (one fadd and one fstp) and the start (+0x17c) at the position. No rand().
void BeginTheSweepEmitter(Effects::ModelEffect& effect);

// 0x51e529..0x51e569, the 0x5ca of any subtype: life 20, speed (0, −60, 0), z 100 more and, with the /1, the scale at
// 1.5f. No rand().
void BeginTheEnergyBall(Effects::ModelEffect& effect);

// 0x51e773..0x51e7d7, the 0x5da of any subtype: life 10 and (0, −60, 0) rotated by the angles added to the position,
// with 130 more in z (on the x87 until the fstp). No rand().
void BeginTheFlameBurst(Effects::ModelEffect& effect);

// 0x52a6d6..0x52a732: with life at 30, the joint 0x61c/0 (0x52a72a) at (x, y, z + 100), with the copied angles, the
// effect's owner (+0x108, the character), width 150, the effect's key (+0x8a) and skill (+0x89). It cuts where the
// joint's creation cuts.
[[nodiscard]] Core::Expected<void, std::string> StepTheSweepEmitter(Effects::ModelEffect& effect,
                                                                    const HighEffectStepContext& context);

// 0x52abbe..0x52ac75: the level life · 0.2f; the color, (level, level, level) with the /0, (1, 0.8f, 0.2f) with the /1
// and the local white (0x520921) with the others; the particles 0x5ca/0 (scale 1) and 0x5c6/0 (scale 4) with that
// color; the dynamic light (level · 0.2f, level · 0.4f, level) of radius 2; and 0x518b60: with the owner (+0x108)
// alive, seen (+0x16c) and at 100 or less in the plane, life at 1 and the particle 0x5c6/1 in white of scale 6. An
// owner that is not one of the world's characters cuts (0x518b7a).
[[nodiscard]] Core::Expected<void, std::string> StepTheEnergyBall(Effects::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);

// 0x52acc3..0x52b3b1: the particle 0x5da/1 in white of scale 1 and the dynamic light (L, L · 0.6f, L · 0.3f) of radius
// 2, with L the step's local light (0x5208c5).
[[nodiscard]] Core::Expected<void, std::string> StepTheFlameBurst(Effects::ModelEffect& effect, float luminosity,
                                                                  const HighEffectStepContext& context);

// 0x529dee..0x529f07: the angles' x 5 less and the speed's y 8 less; with life below 2, the /0 and an owner (+0x108,
// the shot's target): the angles' x at 90, the subtype at 1 and, at the owner's position with rand() % 100 − 50 more in
// x and y and 1200 more in z, the joints 0x632/2 and /3 (0x529eca and 0x529eed) with the effect as owner, width 50 and
// key −1 (JointFamilies::StormJoint, one rand() each); with the joints full, nothing. Otherwise, with the /1, the slot
// freed. An owner that is not one of the world's characters cuts (0x529e3e).
[[nodiscard]] Core::Expected<void, std::string> StepTheArrowBlast(Effects::ModelEffect& effect,
                                                                  const HighEffectStepContext& context);
} // namespace Client::Effects
