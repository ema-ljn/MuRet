#pragma once

#include "Core/Expected.h"
#include "Kernel/CharacterClass.h"

#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

// The 0x57cfc0 branch of classes 1, 3 and 4 (Dark Knight, Magic Gladiator and Dark Lord: the hero's +0x1d4 & 7,
// 0x58187c..0x58188e), 0x581894..0x5897a7. Contract: Client/docs/research/06-render-escenas-y-ui.md, "Las skills del
// caballero".
namespace Client::Skills
{
struct HeroSkillContext;

// With the own record open from end to end (its 3848 rand() on closing, 0x5854be..0x5897a2): the skill's mana and
// stamina (0x551c70); nothing if the hero is not a player (0x20a) or if the published hero has no right weapon or has a
// shield (0xc12 to 0xe11) in one hand, except with 0x30, 0x3e, 0x3f, 0x40, 0x41, 0x4b and 0x4c. The party (0x3f),
// Fenrir (0x4c), the horse's durability (0x3e) and Chaos Castle say whether it can be used; without the mana,
// 0x558110(3) looks for the potion (UseAManaPotion: with one, it cuts at 0x581b9e) and it cannot; the stamina, the
// delay (0x552c50), the energy (0x551f40) and, for the Dark Lord, leadership (0x5521f0, which ends without it). Then,
// by skill (0x58238c): 0x29, 0x2a, 0x37, 0x38, 0x3e, 0x41 and 0x4c send the C1 1E and 0x30, 0x3f, 0x40 and 0x4b the
// C1 19, both as C3 (two rand() before checking the socket) and without "webzen" in the published hero's name; the
// hero's logical cells cost four rand() each. The others, nothing. It does not draw: the replies place the effects. The
// Magic Gladiator then continues in the Wizard's branch (0x5897b7), which the caller cuts.
[[nodiscard]] Core::Expected<void, std::string> UseTheKnightSkill(Kernel::ActorState& hero,
                                                                  Kernel::CharacterClass heroClass, std::uint8_t skill,
                                                                  const HeroSkillContext& context);
} // namespace Client::Skills
