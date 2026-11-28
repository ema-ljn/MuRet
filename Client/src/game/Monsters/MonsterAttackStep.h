#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <string>

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The monster cases of 0x4f5b70, run on each +0x381 step after 0x4f97b0. 06, "El paso del ataque" and "Los casos de
// los monstruos (0x4f5b70)".
namespace Client::Monsters
{

// 0x4f5b70 after its rand() at 0x4f5b97 (drawn by StepTheAttack): the six map helpers (0x43a7a0 in Kalima, 0x4436f0 in
// map 31, 0x42dcf0 in 30, 0x429110 in 33 and 54, 0x42fce0 in 35 and 0x4324c0 in Crywolf), which can exit (AL at 1,
// 0x4f903c); the switch on +0x376 (0x4f5c7e) and the tail with the +0x3a4 target in [0, 400): +0x38e with 0x11 (the
// 0x4f8147 switch) or with 3 (the 0x4f8a56 one). Writes +0x78, +0x381 and +0x17c and requests the sounds (CL10). Cuts,
// keeping what it did before and its rand(), at unported effects (0x518cf0), joints (0x530860) and particles (CL11),
// at the arrow's cuts (0x4f816f, FireTheArrow) and at the 0x4ec110 with the unwritten offset of 0x432acc (Q-C1050).
// Skill 3 without a case cuts at 0x4f8fef when a joint is free. `slot` is `actor`'s place in the pool.
[[nodiscard]] Core::Expected<void, std::string> StepTheAttackCases(Kernel::ActorState& actor, std::size_t slot,
                                                                   const Actors::CharacterStepContext& context);
} // namespace Client::Monsters
