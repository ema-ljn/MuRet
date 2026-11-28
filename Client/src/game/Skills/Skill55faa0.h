#pragma once

#include "Core/Expected.h"
#include "Skills/HeroSkills.h"

#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The elf's skills on their target in reach (0x55faa0). Called by the elf branch of 0x57cfc0 (0x57f239 and 0x57fa20,
// with the hero twice) and the arrival of 0x5650c0 (0x56654a and 0x5666db, case 5).
namespace Client::Skills
{

// 0x55faa0(p1, p2): first the turning D3 with the published hero's logical cell (X, +0x41c, and Y, +0x420, each
// opened and closed) and its angle at that moment. Then the skill of slot 0x7758584 (without checking 0x7758580) in the
// own record opened and closed; the target's position (0x7758588, without checking the −1) in +0x3a8..+0x3b0 of p1 and
// p2's angle towards it (0x4e93f0); by skill − 0x1a (table 0x56181c):
// - 0x1a, 0x1b and 0x1c: without "webzen" and with the brake, the C1 06 19 with the target's key (+0x1f4);
//   0x4f5290(p1).
// - 0x33: without arrows (0x561c50), nothing; otherwise the 19 as above and 0x4f4a10(p1).
// - 0x2e: without arrows, nothing; otherwise the 19 and 0x4f48e0(p1), the shooting action by hands.
// - The others, nothing.
[[nodiscard]] Core::Expected<void, std::string>
UseTheElfSkillOnItsTarget(Kernel::ActorState& user, Kernel::ActorState& turned, const HeroSkillContext& context);
} // namespace Client::Skills
