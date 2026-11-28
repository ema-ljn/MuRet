#pragma once

#include "Core/Expected.h"
#include "Protocol/Outgoing.h"
#include "Skills/HeroSkills.h"

#include <cstdint>
#include <string>
#include <string_view>

// What the class branches of 0x57cfc0 and the sending functions share (06, "Usar una skill"). All with
// UseTheHeroSkill's context; each class's branch calls them with the own record open, like the original.
namespace Client::Kernel
{
struct ItemState;
}

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Skills
{

// What 0x551c70(skill, 1, 0, &mana, 0, &stamina) leaves in its pointers: WORD +0x22 and WORD +0x24 of the Skill.bmd
// entry.
struct SkillCost
{
    std::uint16_t mana = 0;
    std::uint16_t stamina = 0;
};

// 0x551c70 with mana and stamina: opens the skill's entry, reads and closes it (60 rand() if its count returns to 0).
// Without Skill.bmd, cuts with `where`.
[[nodiscard]] Core::Expected<SkillCost, std::string> SkillCostOf(std::uint8_t skill, const HeroSkillContext& context,
                                                                 std::string_view where);

// The skills' no-mana path (0x559b29, 0x55a398, 0x56205f, 0x57d821, 0x58093e, 0x581b86 and 0x589920): with the own
// record's mana (+0x24) below the skill's, 0x558110(3) looks for a mana potion (0x1c04 to 0x1c06) in the inventory, no
// rand(). Without one, nothing: the caller continues on its "no" path. With one, 0x5af520 checks the windows, the
// delay and the C1 26 uses it through H5's inventory; it changes no mana and consumes no item before the GS reply.
// Capability errors carry `where`, the caller's address.
[[nodiscard]] Core::Expected<void, std::string> UseAManaPotion(const HeroSkillContext& context, std::string_view where);

// 0x5526f0(skill, c): the reach (+0x26 of Skill.bmd, with the entry's access), 2 more with `caster` on the 0x1c16.
[[nodiscard]] Core::Expected<float, std::string> SkillRangeOf(std::uint8_t skill, const Kernel::ActorState& caster,
                                                              const HeroSkillContext& context);
// 0x5526f0(skill, 0): without `c` it does not check the 0x1c16 (skill 0x4c's loop in the attack step, 0x50243d).
[[nodiscard]] Core::Expected<float, std::string> SkillRangeOf(std::uint8_t skill, const HeroSkillContext& context);

// What the skills send by inline send(): a C1 (Delivery::Clear, the D3 and the DF) goes out with no rand(); a C3
// (Delivery::Encrypted, the 19, the 1E and the 26) first draws the two rand() of the padding and the serial
// (0x5767913), also without the session, like the original with the socket at −1.
void SendTheSkillPacket(const HeroSkillContext& context, Protocol::Outgoing packet);

// The 300 ms brake of the sends (0x576791c, PassesTheActionBrake) with the context's GetTickCount. Without it, cuts
// with `where`.
[[nodiscard]] Core::Expected<bool, std::string> PassesTheSkillBrake(std::int32_t skill, const HeroSkillContext& context,
                                                                    std::string_view where);

// 0x559510(c, hero, item, skill): with no chosen target (0x6c8e30 at −1 or less), 0. Otherwise its cell (ftol of the
// position · 0.01) in 0x77e1ba8/0x77e1bac, 0x7758580..0x7758588 at 0, `skill` and the chosen target, and with the
// skill's entry open (60 rand() when closed at 0): the reach (0x5526f0 · 1.2; 1.8 in Blood Castle with 0x13 to 0x17)
// against the distance from `hero` to the cell's center. In reach, 0x55b6f0(c, hero); outside, the PathFinding2 path
// up to the reach from c's logical cells (+0x420 and +0x41c, 4 rand() each) and, if there is one, +0x378 at 1 and
// +0x379 at 5 (the arrival of case 5 of 0x5650c0). Gives 1 with 0x55b6f0 or with the path.
[[nodiscard]] Core::Expected<bool, std::string> UseTheSkillOnTheChosen(Kernel::ActorState& caster,
                                                                       Kernel::ActorState& hero, std::uint8_t skill,
                                                                       const HeroSkillContext& context);

// 0x55b6f0(c, hero): the skill of 0x7758584 (with 0x7758580, that of the slot in the own record) on the character of
// 0x7758588. The turning D3 with the published hero's logical cell (+0x41c and +0x420) and its heading, through the
// socket; c's +0x378 at 0; the action (with a player, 0x4f3f80 and that of table 0x55cb60; otherwise, 0x4f4a10 on c);
// except for 0x3c, 0x3d and 0x4a, the 0x5fe shine in the particle pool (0x55be23, with owner `hero`) and sound rand()
// % 2 + 0x3c (one rand(), to the last sound); the target in c's +0x3a8 and the heading towards it in `hero`'s +0x24;
// with the name without "webzen" and the brake passed, the C3 19; c's +0x381 at 1 and, except with 0x20 in `hero`'s
// +0x78 or c on the 0x1c16, the target's cell (moved by the heading in Blood Castle, with 0x3c, 0x39 and 0x2c or with
// monsters 0x115, 0x11b, 0x116 and 0x120) and, if it is not a wall (4 or 8 in the attribute) or one of the weapon's
// skills, the DF with that cell.
[[nodiscard]] Core::Expected<void, std::string>
CastTheSkillAtTheTarget(Kernel::ActorState& caster, Kernel::ActorState& hero, const HeroSkillContext& context);

// 0x559afc..0x55a2e5: the skills of 0x559890's list (0x31, 0x3c, 0x3d, 0x2c, 0x4a, and 0x2b, 0x2f and 0x39 with their
// weapon). The cost (0x551c70); without mana, the potion (UseAManaPotion, 0x559b41) and 0; with more stamina than
// +0x38, 0; without 0x552c50 (the delay of slot 0x7758584), 0; if 0x558bb0 does not let the chosen target be attacked,
// the weapon's options (UseTheWeaponOptions); if it does, 0x559510(hero, hero, item, skill).
[[nodiscard]] Core::Expected<bool, std::string> UseTheListedWeaponSkill(Kernel::ActorState& hero,
                                                                        const Kernel::ItemState& item,
                                                                        std::uint8_t skill,
                                                                        const HeroSkillContext& context);

// 0x559a21..0x55b4b4: with the own record open, each weapon option (+0x2b...) equal to the skill of the published
// hero's slot +0x425 (0x55a368): its cost; without mana, the potion (0x55a3b0) or the next one; with the delay
// (0x552c50), option 0x12 sets action 0x98 (0x4f4a10 if not a player) and, with the name without "webzen" and the brake
// passed, sends the C3 19 with the published hero's key; 0x13 to 0x17 and 0x31, if 0x558bb0 lets attack, 0x559510.
// It continues with the other options; the result is that of the last one that set it.
[[nodiscard]] Core::Expected<bool, std::string>
UseTheWeaponOptions(Kernel::ActorState& hero, const Kernel::ItemState& item, const HeroSkillContext& context);

// 0x561f60(hero, item) from 0x562024: with the own record open, each weapon option equal to the skill of the published
// hero's slot +0x425: its cost; without mana, the potion (0x562075) or the next one; with more stamina than +0x38 or
// without the delay (0x552c50), 0 at once. The reach (0x5526f0) and, with 0x18 or 0x36: without arrows (0x561c50) the
// options end; with the chosen target's cell (0x77e1ba8) in reach, c's heading towards +0x3a8, the key of 0x558eb0
// with 0x7758588 and, with the name without "webzen" (no brake), 0x5767948 and the 1E (0x36 with c's logical cell
// towards the target and the back); then 0x4f4a10 and, if c is not a player, the arrow (ShootTheArrow, not ported).
// It continues with the other options; gives 1 if any fired.
[[nodiscard]] Core::Expected<bool, std::string>
UseTheElfWeaponOptions(Kernel::ActorState& hero, const Kernel::ItemState& item, const HeroSkillContext& context);
} // namespace Client::Skills
