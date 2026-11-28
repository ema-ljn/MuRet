#pragma once

#include "Core/Expected.h"
#include "Combat/ActorCombat.h"

#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The skills' hit action (0x4f5290), called by the elf, classes 1, 3 and 4 and Wizard branches of 0x57cfc0 (0x57f997,
// 0x58016f, 0x5808cb, 0x58c156, 0x58fcd4, 0x591840 and 0x5925a1) and by 0x55cbc0; not by the tail of 0x43 to 0x48
// (HeroSkillsTail.h). It is the same one SkillReplies uses with the incoming skills, with the hit context.
namespace Client::Skills
{

// 0x4ea030(character, action) with the skeleton of its type (+2): Player.bmd for a player (0x20a) and the model
// library's for the others. Without it, cuts.
[[nodiscard]] Core::Expected<void, std::string> SetTheActorAction(Kernel::ActorState& actor, std::uint8_t action,
                                                                  const Combat::ActorCombatContext& context);

// 0x4f5290: on a player, the speeds (0x4f3f80) and the hit action: 0x97 riding Uniria or Dinorant (0x1c14, 0x1c15)
// outside the safe zone, 0x5d on Fenrir (0x1c37), 0x92 the elf and otherwise 0x8e + (rand() & 1). On another, 3 or 4
// by +0x38f (every third, 3), which goes up.
[[nodiscard]] Core::Expected<void, std::string> SetTheHitAction(Kernel::ActorState& actor,
                                                                const Combat::ActorCombatContext& context);
} // namespace Client::Skills
