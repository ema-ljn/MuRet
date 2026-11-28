#pragma once

#include "Core/Expected.h"
#include "Skills/AttackSkillCases.h"

#include <glm/ext/vector_float3.hpp>

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

namespace Client::Monsters
{
struct MonstersState;
} // namespace Client::Monsters

// What a character's body (0x4fd5f0) does after striking. 06, "El paso del ataque".
namespace Client::Skills
{

// 0x4fdb9a..0x50539e, the skill of the body step. With +0x381 nonzero, 0x4f97b0 (the attack frames, with +0x38e and
// the own record), 0x4f5b70 (the rand() and the monster helpers) and +0x381 += 1. From [0x6c88ac]
// (WorldRuntime::skillThreshold): +0x381 at 0, o+0x8a with 0x558eb0 (ChosenKeyOf), +0x38e through the protection
// table, what follows the switch with and without the target of +0x3a4, the write of +0x38e to 0 and +0x382 and
// +0x380 at 0. The arrow (0x53e2b0, after 0x59fb80) is FireTheArrow (ArrowEffects.h). It cuts, with what it did before
// and its rand(), at the skill cases, the monsters and the transformed, the arrow's cuts and the unported effects.
// `slot` is `actor`'s slot in the pool and `light` the local light of 0x4fd5f0 (Skills::BodyLight or the 0x5ac
// particle's, 0x4fdb3f) that the skill cases read. `monsters` goes to the monster cases (Monsters::StepTheAttackCases).
[[nodiscard]] Core::Expected<void, std::string> StepTheAttack(Kernel::ActorState& actor, std::size_t slot,
                                                              const Actors::CharacterStepContext& context,
                                                              const glm::vec3& light = Skills::BodyLight,
                                                              const Monsters::MonstersState* monsters = nullptr);
} // namespace Client::Skills
