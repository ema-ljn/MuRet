#pragma once

#include "Core/Expected.h"
#include "Skills/HeroSkills.h"

#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The tail of 0x57cfc0 with skills 0x43 to 0x48 (0x5808da..0x599d5d; 06, "La cola de las 0x43 a 0x48"), for all
// classes. Arriving here: the elf (the table of 0x57fa31 and the end of 0x35 and 0x4c), classes 1 and 4 after their
// own (0x5897b1), the Wizard and the Magic Gladiator from 0x5925ae and classes 5 to 7 (0x5897b1).
namespace Client::Skills
{

// 0x5808da(hero, skill): outside 0x43 to 0x48, nothing. Otherwise, with the own record open until 0x599d58 (0x40f370):
// - The head (0x5808ef..0x594002): the cost (0x551c70); without a player (0x20a), nothing more. Without the mana
//   (+0x24), the mana potion (0x558110(3); with one, 0x5af520 and the C1 26 are milestone 5's); with it and the stamina
//   (+0x38), the delay of the published hero's slot (0x552c50). Then the energy (0x551f40). None of that stops the
//   skill (Q-C1226).
// - 0x43 (0x594009..0x594e3d): the heading towards +0x3a8, the target's position (0x5ae300) from the logical cell (Y
//   and X), the cell of 0x4e9920 and the key of 0x558eb0 with 0x7758588 (0xffff with −1); without "webzen", 0x5767948
//   and the 1E with the logical cell (X and Y) and the magic key of 0x57cf90 with a null pointer, 0. Then 0x4f3f80.
// - 0x44 to 0x48 (0x594e42, 0x595e25, 0x596dca, 0x597d7e and 0x598d65): without "webzen" and with the 300 ms brake,
//   the 19 with the chosen target's key (0x6c8e30, +0x1f4) or, without one, the player key (0x57678bc).
// - The action (0x4ea030) by mount outside the safe zone: 0x52 the dark horse, 0x40 Uniria, 0x41 Dinorant and 0x5d
//   Fenrir; without one, 0x87 for 0x44 and 0x47, and 0x3f for the others. +0x378 at 0.
// No effects or sounds; the potion's C1 26 and its sound (0x593f73..0x593fa1) come with milestone 5.
[[nodiscard]] Core::Expected<void, std::string> UseTheSharedTail(Kernel::ActorState& hero, std::uint8_t skill,
                                                                 const HeroSkillContext& context);
} // namespace Client::Skills
