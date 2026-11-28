#pragma once

#include "Core/Expected.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <string>

// The 0x628, the skull (Skill\Skull.jpg as sprite 0x628; the effect has no model or case in 0x52d620): its creation
// (0x51ee23) and step (0x52b59b), and what its callers do with flag 0x40 (skill 0x37). 06, "Los efectos con modelo de
// los tipos altos".
namespace Client::Crt
{
class CrtRandom;
} // namespace Client::Crt

namespace Client::Effects
{
struct ModelEffect;
} // namespace Client::Effects

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Effects
{
struct HighEffectStepContext;

inline constexpr std::int16_t SkullEffect = 0x628;

// 0x51ee23..0x51ee95: life 1000; the /1, 940; the /2, 8 and z 150 more; the /4, 20, +0xd8 at 2 and speed (0, −10, 0).
// No rand().
void BeginTheSkull(Effects::ModelEffect& effect);

// 0x52b59b..0x52b8a1. The /4 falls (the speed's y goes down by +0xd8, the angles' x by 1) and leaves the sprite 0x628
// of scale 10 with its light. The others look at their owner (+0x108, a character): dead, life at 0 and the live byte
// at 0. Alive, by subtype (0x52d18c's table): the /0, with bit 0x40 of its own +0x78, life at 10 and, with 0x4000
// too, +0x16c at 0 and nothing more; otherwise, like the /1 and the /3, three rounds with the particle 0x5de/5 and the
// sprite 0x628 turning over the owner; the /2, the sprite 0x628 of scale 1.5f with the light life · 0.1f; the /5, three
// stacked particles 0x5ac/5 that turn. No rand(). It cuts at the first particle MuRet does not have, with room in the
// table (CL11).
[[nodiscard]] Core::Expected<void, std::string> StepTheSkull(Effects::ModelEffect& effect,
                                                             const HighEffectStepContext& context);

// 0x52b795..0x52b829: the point of round `turn` (0 to 2) of the /5 with the effect's life `life` and T (`worldClock`,
// 0x5767b88): the angle turn · 2π/3 + life · 0.17f + T · 0.003f, at 50 from the owner in x (FSIN) and in y (FCOS), with
// the PC53 profile, and at (turn + 1) · its scale · 50 above its z. Its 0x5ac/5 is born there.
[[nodiscard]] Core::Expected<glm::vec3, std::string> SkullStackPoint(int turn, std::int32_t life, float worldClock,
                                                                     const Kernel::ActorState& owner);

// 0x518ac0(0x628, character, 0) and 0x518cf0(0x628, +0x10, +0x1c, (1, 1, 1), 0, character, −1, 0, 0, 0, 0): turns off
// the character's 0x628/0 and creates another with it as owner (with the table full, none). No rand(). `address` is the
// caller's 0x518cf0 one, for the error.
[[nodiscard]] Core::Expected<void, std::string> PutTheSkullOn(Kernel::ActorState& actor, Kernel::WorldState& world,
                                                              Crt::CrtRandom& random, std::uint32_t address);
} // namespace Client::Effects
