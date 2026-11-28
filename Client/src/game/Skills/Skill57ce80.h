#pragma once

#include "Core/Expected.h"
#include "Skills/HeroSkills.h"

#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// A skill's target (0x57ce80), called by the head of 0x57cfc0 (0x57d4c2, without checking the result), the elf branch
// (0x57e12b) and the Wizard's (0x58a118, which stores it in [ebp−0x3d]).
namespace Client::Skills
{

// 0x57ce80(hero): with a chosen target (0x6c8e30), its cell (ftol of the position · 0.01) in 0x77e1ba8/0x77e1bac and
// its position in the hero's +0x3a8..+0x3b0, and 1. Without one, the terrain point under the mouse (0x5fc260(1) and
// 0x5fac90): if it hits, the point in +0x3a8..+0x3b0 and its cell, and 1; otherwise 0 without touching anything. No
// rand().
[[nodiscard]] Core::Expected<bool, std::string> AimTheHeroSkill(Kernel::ActorState& hero,
                                                                const HeroSkillContext& context);
} // namespace Client::Skills
