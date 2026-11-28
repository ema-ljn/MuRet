#pragma once

#include "Core/Expected.h"
#include "Skills/HeroSkills.h"

#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The Wizard branch of 0x57cfc0 (0x5897aa..0x593874; 06, "Usar una skill"). Used by the Wizard (0) and the Magic
// Gladiator (3), which arrives from the end of the branch of classes 1, 3 and 4 (0x5854c1 → 0x5897a7). Continues at
// 0x5808da (skills 0x43 to 0x48, for all classes: HeroSkillsTail.h).
namespace Client::Skills
{

// 0x5897a7..0x599d5d with `skill`, the slot's (0x28 already charged as 0x3a by the right button), and the reach of
// GameMovementState::selectedSkillRange. With a class other than 0 or 3, straight to 0x5808da.
// - The head (0x5897b7..0x58a13a), with the own record open: mana and stamina (0x551c70, that of 0x28 for 0x3a and
//   0x28, and 0x28 without stamina), the energy (0x551f40) against +0x1e + +0x30, the mana against +0x24 (without it,
//   the mana potion: HeroSkillCasts), the stamina against +0x38 (without it, 0x3a and 0x28 release the right button)
//   and the delay (0x552c50). With the record closed, the target (0x57ce80).
// - By skill: 0x3a and 0x28 (the 19 without a target and action 0x44 or 0x45), 0x10 (Soul Barrier, with the party),
//   0xa and 0xe (the 1E of Hellfire and Inferno), 0x4c (with 0x4e9920 and the magic key) and the others by reach
//   (0x58c15e): out of it, the path to the chosen target (+0x379 at 5); in it and with the line of sight, 0x55cbc0 with
//   an attackable chosen target and, with the target, the angle and 5 (Flame), 8 and 9, 0xc (Aqua Beam) and 0x26 and
//   0x27 (the 1E), and for the Wizard 6 (Teleport, the 1C) and 0xf (Teleport Ally, the B0). 0x12 to 0x17 and 0x2f,
//   nothing.
// The sends go through HeroSkillContext::send; the C3s draw their two rand() first. The party (0x59e680 and 0x59272c)
// is Party::IsInTheParty and the potion (0x589936), UseAManaPotion.
[[nodiscard]] Core::Expected<void, std::string> UseTheWizardSkill(Kernel::ActorState& hero, std::uint8_t skill,
                                                                  const HeroSkillContext& context);
} // namespace Client::Skills
