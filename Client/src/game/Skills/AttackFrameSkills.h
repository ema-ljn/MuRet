#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// CL9 milestone 4, phase 6a: the skill cases of 0x4f97b0, the attack frames (P-C258). 06, "Las habilidades de los
// frames del ataque".
namespace Client::Skills
{

// 0x4f9899..0x4fab5c: the switch of 0x4f97b0 by skill − 0x28 (0x4fb10c's table), with `skill` already read from
// +0x38e and [0x6c88ac] at 15 (0x4f987d). false: the skill has no case and 0x4f97b0 goes on at 0x4fab5d. true: the
// case ran and 0x4f97b0 leaves (each case ends in a RET), with what it wrote: +0x381 at 15 when the action ends (at 10
// for 0x41 when it did not end it), 0x34's frame and [0x6c88ac] at 5 (0x4f9ee2, WorldRuntime::skillThreshold), 0x37's
// o+0x8a (0x558eb0), 0x2b's o+0x194, and 0x2c's four 0x617/0 joints and four 0x5d9/2. It stops, with what it did
// before and its rand(), at the effects, joints and particles MuRet lacks (CL11) and without the skeleton of 0x2b's
// model. `slot` is `actor`'s slot in the pool.
[[nodiscard]] Core::Expected<bool, std::string> StepTheFrameSkill(Kernel::ActorState& actor, std::uint8_t skill,
                                                                  std::size_t slot,
                                                                  const Actors::CharacterStepContext& context);
} // namespace Client::Skills
