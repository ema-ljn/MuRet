#pragma once

#include "Core/Expected.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace Client::Kernel
{
class WorldState;
struct ActorState;
} // namespace Client::Kernel

namespace Client::Combat
{
// The name from +0x1d9 as strcmp sees it: the ten bytes, +0x1e3 and +0x1e4, which MuRet does not have and reads as 0.
[[nodiscard]] std::string CNameOf(const Kernel::ActorState& actor);

// 0x558bb0: whether the chosen one (0x6c8e30, selectedTargets[2]) can be attacked. The cursor count [0x7879fbc] is
// signed, the siege is 0x4294b0 and Ctrl is GetAsyncKeyState(0x11) at 0x558e7f. In a guild war (0x576796c, 0x576796d)
// a guild name equal to the hero's (strcmp at 0x558da4 on 0x787aa55) is not attacked.
[[nodiscard]] Core::Expected<bool, std::string> IsAttackable(const Kernel::WorldState& world, bool controlDown);
// 0x5588d0(target), as the target loop of skill 0x4c in the attack step calls it (0x5024c3). Guild names at 0x558aa9.
[[nodiscard]] Core::Expected<bool, std::string> IsAttackableActor(const Kernel::WorldState& world,
                                                                  const Kernel::ActorState& target, bool controlDown);

// 0x558eb0(c, [0x6c8e30]), the sibling of 0x558bb0 that the skill step leaves in o+0x8a (0x4fdc10): the chosen one's
// key (+0x1f4, MOVSX) or −1. `attacker` must be the published hero ([0x743a170]); Ctrl at 0x558fd6; in a duel, the
// rival's key without comparing names; guild names at 0x558f29. No rand().
[[nodiscard]] Core::Expected<std::int32_t, std::string> ChosenKeyOf(const Kernel::WorldState& world,
                                                                    std::size_t attacker, bool controlDown);
// 0x558eb0(c, `chosen`), as the elf calls it with 0x7758588 (0x562a7f, 0x563ea3).
[[nodiscard]] Core::Expected<std::int32_t, std::string>
ChosenKeyOf(const Kernel::WorldState& world, std::size_t attacker, std::int32_t chosen, bool controlDown);

// The 300 ms brake (0x57f2d1..0x57f2f9, the same at 0x559890, 0x55b6f0, 0x55cbc0 and 0x55faa0) on [0x576791c]: a
// signed 32-bit difference (CDQ/XOR/SUB). The original calls GetTickCount twice.
[[nodiscard]] bool PassesTheActionBrake(std::uint32_t& last, std::int32_t skill,
                                        const std::function<std::uint32_t()>& tickCount);
} // namespace Client::Combat
