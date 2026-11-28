#pragma once

#include "Core/Expected.h"
#include "Skills/HeroSkills.h"

#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The elf branch of 0x57cfc0 (0x57d790..0x58187c; 06, "La rama del elfo"). Used by the elf (+0x1d4 & 7 at 2);
// continues at 0x5808da (skills 0x43 to 0x48, for all classes: HeroSkillsTail.h).
namespace Client::Skills
{

// 0x57d790..0x58187c with `skill`, the slot's, and the reach of GameMovementState::selectedSkillRange ([ebp−0x50]).
// - The head, with the own record open: mana and stamina (0x551c70; without the mana, HeroSkillCasts's mana potion,
//   0x57d837), stamina against +0x38, the delay (0x552c50) and the energy (0x551f40) against +0x1e + +0x30. With the
//   record closed, the target (0x57ce80), without checking its result.
// - 0x1e to 0x24 (the summons, 0x57f252): on map 10 or in Chaos Castle, nothing; otherwise, without "webzen" and with
//   the brake, the C1 06 19 with the player key (0x57678bc); 0x4f5290 and nothing more.
// - 0x34 (0x57e14d): a player with the published hero's bow or crossbow, arrows (0x561c50) and the target's cell in
//   reach: the heading towards +0x3a8, the key of 0x558eb0 with 0x7758588 (0xffff without it), without "webzen"
//   0x5767948 and the C1 0C 1E with the logical cell, and 0x4f4a10. Continues like the others.
// - The others (0x57f008): with a chosen target, 0x7758580..88; out of reach, the path to the chosen target (a player,
//   with 0x1a to 0x1c or attackable, +0x379 at 5) and, except for 0x43 to 0x47 and 0x4c, nothing more. With the line
//   of sight and a chosen target, 0x55faa0 (0x1a to 0x1c on a player, and nothing more; 0x33 and 0x2e with the weapon,
//   and it continues). By skill: 0x1a to 0x1c and 0x35, the 19 with the player key and 0x4f5290 (0x35 with +0x378 at
//   0); 0x4c, with 0x4e9920 and the magic key, like the Wizard's; the others, to 0x5808da.
// The sends go through HeroSkillContext::send; the C3s draw their two rand() first. What belongs to another milestone
// cuts with its address: the potion (0x57d837, milestone 5).
[[nodiscard]] Core::Expected<void, std::string> UseTheElfSkill(Kernel::ActorState& hero, std::uint8_t skill,
                                                               const HeroSkillContext& context);
} // namespace Client::Skills
