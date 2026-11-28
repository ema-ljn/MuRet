#pragma once

#include "Core/Expected.h"
#include "Skills/HeroSkills.h"

#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The weapon skill on its target in reach (0x55cbc0). Called by the Wizard branch of 0x57cfc0 (0x58eecc, with the hero
// twice) and the arrival of 0x5650c0 (0x5663b2, milestone 3).
namespace Client::Skills
{

// 0x55cbc0(p1, p2): the skill of the weapon skill's slot (0x7758584, in the own record opened and closed). With 0x2b,
// 0x2f or 0x49 and the published hero's right hand (+0x2b8, MOVSX) outside 0xc12 to 0xe11, nothing. Otherwise: the
// target's position (0x7758588, without checking the −1: MuRet cuts a target outside the pool, Q-C1239) in
// +0x3a8..+0x3b0 of p1 and p2's angle towards it (0x4e93f0); by skill − 1 (table 0x55fa54):
// - 1, 2, 3, 4, 7, 0xb, 0x11 and 0x2d: without "webzen" and with the brake, the C1 06 19 with the target's key;
//   0x4f5290(p1) and the turning D3 with the published hero's cell (X and Y) and angle.
// - 0x49: the same, with 0x4ea030(p2, 0x86) and 0x4f3f80 instead of 0x4f5290.
// - 0xd: without "webzen" or brake, 0x5767948 and the C1 0C 1E with the cell of p1's +0x3a8, p2's heading and the
//   target's key through 0x558eb0 (the original's 0xffff with −1 never arrives: it already cut); 0x4f5290(p1) and the
//   D3.
// - The others, nothing.
[[nodiscard]] Core::Expected<void, std::string>
UseTheSkillOnItsTarget(Kernel::ActorState& user, Kernel::ActorState& turned, const HeroSkillContext& context);
} // namespace Client::Skills
