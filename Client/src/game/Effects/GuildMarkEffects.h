#pragma once

#include "Core/Expected.h"
#include "Tables/PlayerBones.h"

#include <glm/ext/vector_float3.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

// The 0x140, the mark 0x510ef0 puts on a character with +0x369 at 1 (0x510f4d): its creation (0x51e0cb) and its step
// (0x529f0c). Its drawing is 0x6002e0's own case (0x602005, DrawTheHighEffectBody of Effects/HighEffectDrawing): two
// chrome passes. 06, "Los efectos con modelo de los tipos altos".
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Particles
{
class Particles;
} // namespace Client::Particles

namespace Client::Effects
{
class ModelEffects;
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Effects
{
struct HighEffectStepContext;

inline constexpr std::int16_t GuildMarkEffect = 0x140;

// The owner bone the mark follows: 0x4ec110(owner's model, +0x120 + 0x3c0, (0, 0, 0), point, 1).
inline constexpr std::size_t GuildMarkBone = static_cast<std::size_t>(Tables::PlayerHeadBone);

// 0x51e0cb..0x51e233, after 0x518cf0's prefix in `slot`: the alpha at 0, the angles' z at 45, the x and y of the
// owner's bone 20 point (`bonePoint`) and z ((point + owner's z) − owner's z) + 60, life 130, scale 3.2f, the light at
// 1 and the start at the position. Then, ten times: the turn rand() % 360 and two radii rand() % 20 + 15 (each drawn
// right before being used), and the spark 0x5c6/4 (scale 0.6f, with the effect as owner, toward its position) at
// (FSIN(turn) · r + x, FCOS(turn) · r2 + y, z). Thirty rand(); the spark's, none.
[[nodiscard]] Core::Expected<void, std::string> BeginTheGuildMark(Effects::ModelEffect& effect, std::size_t slot,
                                                                  const Kernel::ActorState& owner,
                                                                  const glm::vec3& bonePoint,
                                                                  Particles::Particles& particles,
                                                                  Crt::CrtRandom& random, float worldClock);

// 0x510ef0 up to 0x518cf0: +0x369 at 1 and +0x36c (a float) at 0 or a NaN, or the world clock more than 5000 later.
[[nodiscard]] bool GuildMarkIsDue(const Kernel::ActorState& actor, float worldClock);

// 0x510ef0 with +0x369 at 1: if +0x36c (a float) is 0 or a NaN, or if the world clock is more than 5000 ahead of it,
// the 0x140 (0x518cf0(0x140, position, angles, light, 0, the character, −1, 0, 0, 0, 0), with the table full
// nothing), and +0x36c becomes the clock. `bonePoint` is the point of the character's bone 20 with the model as its
// drawing left it.
[[nodiscard]] Core::Expected<void, std::string> RaiseTheGuildMark(Kernel::ActorState& actor, const glm::vec3& bonePoint,
                                                                  Effects::ModelEffects& effects,
                                                                  Particles::Particles& particles,
                                                                  Crt::CrtRandom& random, float worldClock);

// 0x529f0c..0x52a176. Without an owner (+0x108), life at 0. With the alpha at 1 or more, the angles' z 5 more. With
// life between 121 and 124, five sparks 0x5c5/6 (scale 1.5f, 0x529f71); between 96 and 120, the alpha 0.4f more until
// it passes 1 (and then 1); below 50, 0.1f less until it passes 0 downward (and then 0). It follows the owner's bone
// 20: x and y at owner − hero ([0x743a170]) + point, and z toward ((point + z) − z) + 60, by a tenth if it is higher or
// by half otherwise. With life between 40 and 59: the turn rand() % 360, two radii rand() % 26 − 13 and rand() % 5, and
// the spark 0x5c5/5 (scale 2, 0x52a16e) at (FSIN(turn) · r + x, FCOS(turn) · r2 + y, z + 48 − life + k). The 0x5c5/5
// and /6 keep the owner mark (its slot in the table, the ebx of 0x52a134 and 0x529f5b); the /5 reads its position at
// birth (0x5403ad) and both follow it in their step. With the particles full, no sparks nor their creation's rand();
// the four of 0x52a0c2..0x52a12c are drawn anyway.
[[nodiscard]] Core::Expected<void, std::string> StepTheGuildMark(Effects::ModelEffect& effect,
                                                                 const HighEffectStepContext& context);
} // namespace Client::Effects
