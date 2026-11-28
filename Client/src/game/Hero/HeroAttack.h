#pragma once

#include "Core/Expected.h"
#include "Tables/SkillTable.h"
#include "Protocol/Movement/Movement.h"
#include "Protocol/Outgoing.h"
#include "Combat/ActorCombat.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Items
{
struct InventoryServices;
} // namespace Client::Items

namespace Client::Hero
{

// What 0x5650c0 needs besides the world: the attempts of the turning D3 (C1 06 D3 X Y heading, like a one-step
// SendMove) and of the hitting D7. Best-effort like the original socket: a closed transport does not cut the arrival.
struct HeroArrivalContext
{
    Combat::ActorCombatContext combat;
    std::function<void(std::span<const Protocol::Movement::Cell>, Protocol::Movement::Cell, float)> sendWalk{};
    std::function<void(std::uint16_t key, std::uint8_t action, std::uint8_t direction)> sendAttack{};
    // What the case 5 skills (0x56601f) need, as in HeroSkillContext: Skill.bmd (0x5526f0; nullptr cuts), the
    // GetTickCount of the 300 ms brake (empty: the send that checks it cuts), the services of 0x561c50, Ctrl (0x558eb0)
    // and what they send through send() (the C3 with their two rand() already drawn; empty, nothing goes out).
    const Tables::SkillTable* skills = nullptr;
    std::function<std::uint32_t()> tickCount{};
    const Items::InventoryServices* inventory = nullptr;
    bool controlDown = false;
    std::function<void(Protocol::Outgoing)> sendSkill{};
};

// 0x5650c0(hero, hero, 0) on arrival (0x59b2d2): by +0x379 (MOVSX, 1 to 5; any other, nothing). Case 3 hits
// (0x565107..0x56601e): the range by the own record's weapons, the target at 0x6c8ebc, the distance to its cell and, in
// range, 0x4f4a10, the turn (D3) and the D7. Case 5 (0x56601f..0x566754) uses the pending weapon skill
// (0x7758580..0x7758588): the skill (the own record's slot with 0x7758580, or 0x7758584 as is) and its range
// (0x5526f0); by tables 0x56ab4c and 0x56ab34, with the target alive (+0x389 at 0) and its cell at 0x77e1ba8/0x77e1bac:
// - 1 to 4, 7, 0xb, 0x11, 0x2d and 0x49: in range (· 100) and outside the safe zone, with line of sight (0x558800),
//   0x55cbc0 (0x5663b2); without it, nothing.
// - 0x13 to 0x17, 0x2b, 0x2c, 0x31, 0x39, 0x3c, 0x3d and 0x4a, and 0x2f with the published hero on Uniria: in range
//   (· [0x6ba448], 120.0000076) and outside the safe zone, 0x55b6f0 (0x5661f4), without checking line of sight.
// - 0x2e and 0x33 (0x56654a) and 0x1a to 0x1c (0x5666db): like the first ones, with 0x55faa0.
// Out of range or in the safe zone, the path (0x4eac00) to the range (· 1.2 in the second group) and, with it, +0x378
// to 1; +0x379 stays at 5. Other skills, nothing. Cases 1, 2 and 4 belong to other milestones and cut with their
// address.
[[nodiscard]] Core::Expected<void, std::string> InteractOnArrival(Kernel::ActorState& hero,
                                                                  const HeroArrivalContext& context);
} // namespace Client::Hero
