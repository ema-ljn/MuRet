#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Ui
{
struct GameHudContext;
} // namespace Client::Ui

// The tooltip of a bar skill (06 § Los tooltips, "El cuerpo de la skill", CL8 milestone 5): 0x5b90b0, which 0x5f7bc0
// calls with the hero's skill slot (0x6c9320), and the help of the raven's commands (0x421c10), which replaces it with
// the Dark Lord's slots 0x78 to 0x7b. Both write into HudState::tooltip without clearing it, without touching 0x78abb30
// or 0x78abb34, and draw the box (0x5af090) with mode 2 and the frame.
namespace Client::Tooltips
{

// 0x5b90b0(x, y, slot, siege skill), cdecl. With the commands' help (0x421c10) it exits without opening anything.
// Otherwise, it opens the own record and writes, with the skill of the record's byte +0x6c + slot:
//   - "\n", the name from 0x551c70 (1200 with 0x3c and option 0x42 on the right) in blue and bold, "\n";
//   - the damage by class: the magic of 0x5513d0 (170) for the Wizard, unless the siege skill is 0x43, 0x44 or 0x45,
//     and the three of 0x10; the skill's (0x5516e0 plus the attack of 0x5b8b80, 879) for classes 1 to 4, or 1237 with
//     0x3e, except the skills in the table of 0x5ba73c;
//   - the stat damage of 0x4c (0x552470), the elf's three (0x1a, 0x1b and 0x1c), the entry's range, mana and stamina,
//     the Dark Knight's (0x2f and the combo), the attribute (0x5519f0, 1080 + byte), what the entry's +0x31 says (who
//     the skill comes from or the guild war), the Dark Lord's (0x3f without a party) and the two notes of 0x4c;
//   - a "\n" at the end.
// It closes the own record (3848 rand() if it is left at 0) and puts the box above y: ((N − 3) · cy + 3 · cy / 2) / ry.
// Each access to a Skill.bmd entry draws 60 rand() when it closes at 0. What MuRet does not have cuts with its address.
[[nodiscard]] Core::Expected<void, std::string> DrawTheSkillTooltip(Ui::GameHudContext& context, std::int32_t x,
                                                                    std::int32_t y, std::int32_t slot,
                                                                    std::int32_t siegeSkill);
} // namespace Client::Tooltips
