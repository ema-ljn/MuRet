#pragma once

#include <cstdint>

namespace Client::Crt
{
class CrtRandom;
}

namespace Client::Actors
{
struct CharacterStepContext;
} // namespace Client::Actors

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The two reads that AttackStep.cpp, AttackFrameSkills.cpp and AttackSkillCases.cpp share in the attack step (06, "El
// paso del ataque"). Internal to those three: MonsterAttackStep.cpp has its own.
namespace Client::Skills
{

// 0x517630 and the inline accesses to +0x38e: a one-byte entry, one rand() when closed at 0.
inline constexpr std::uint16_t SkillCloseRandoms = 1;

// 0x517630 (thiscall on 0x57630c8) and its inline copies: opens +0x38e, reads it and closes it.
[[nodiscard]] std::uint8_t ReadTheSkill(Kernel::ActorState& actor, Crt::CrtRandom& random);

// 0x59fb80(skill, …): opens the own record ([0x7681eb4], 0x40ad90), looks for `skill` among the 64 bytes of +0x6c of
// [0x7681eb0] and closes it (0x40b4d0: 3848 rand() if it is left at 0). Returns its slot, or 0 if absent. The other
// arguments the caller pushes stay on the stack for 0x53e2b0.
[[nodiscard]] std::int32_t OwnSkillIndex(const Actors::CharacterStepContext& context, std::uint8_t skill);
} // namespace Client::Skills
