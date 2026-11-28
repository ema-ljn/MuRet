#include "Combat/Attackable.h"

#include "Core/Expected.h"
#include "Guild/GuildState.h"
#include "Kernel/ActorState.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Maps/MapId.h"
#include "Social/SocialState.h"
#include "Tables/SkillId.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Combat
{
namespace
{
constexpr std::uint8_t PlayerObject = 1;
constexpr std::uint8_t MonsterObject = 2;
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
constexpr std::int32_t BrakeSkill = Tables::NovaSkill;
constexpr std::int32_t BrakeMilliseconds = 300;
constexpr std::uint32_t SiegeShield = 0x4000;
constexpr std::uint8_t MinimumOutlawPk = 6;
constexpr std::uint16_t NoGuild = 0xffff;

bool IsSiegeAttacker(std::uint8_t role)
{
    return role == 9 || role == 6 || role == 10 || role == 11 || role == 7 || role == 8;
}
bool IsSiegeDefender(std::uint8_t role)
{
    return role == 13 || role == 12;
}
bool HasAnyFlag(std::uint32_t flags, std::uint32_t mask)
{
    return (flags & mask) == mask;
}
bool HasSiegeMark(std::uint32_t flags)
{
    return HasAnyFlag(flags, 0x80000) || HasAnyFlag(flags, 0x400000) || HasAnyFlag(flags, 0x800000);
}

std::string DuelNameOf(const std::array<std::byte, Social::DuelNameSize>& bytes)
{
    std::string name;
    for (const std::byte letter : bytes)
    {
        if (letter == std::byte{0})
            break;
        name.push_back(static_cast<char>(letter));
    }
    return name;
}

// 0x558c2a..0x558d6d: with the siege, opposed +0x360 posts and the +0x78 marks decide by the chosen one's 0x4000
// shield; otherwise it goes on as without the siege.
std::optional<bool> SiegeDecides(const Kernel::ActorState& hero, const Kernel::ActorState& target)
{
    const bool shielded = (target.flags78 & SiegeShield) == SiegeShield;
    if (IsSiegeAttacker(hero.byte360) && IsSiegeDefender(target.byte360))
        return !shielded;
    if (IsSiegeDefender(hero.byte360) && IsSiegeAttacker(target.byte360))
        return !shielded;
    if (HasSiegeMark(hero.flags78))
    {
        if (HasSiegeMark(target.flags78))
            return std::nullopt;
        return !shielded;
    }
    if (HasAnyFlag(hero.flags78, 0x40000) && !HasAnyFlag(target.flags78, 0x40000))
        return !shielded;
    return std::nullopt;
}

// 0x558d97..0x558df3 (0x558aa9 in 0x5588d0, 0x558f29 in 0x558eb0): in the guild war (0x576796c), a player with a PK of
// 6 or more and a guild (+0x1f2 other than 0xffff) strcmps its guild's name (0x787aa55 + i · 0x58, the table's +0xd)
// with the hero's. The hero's index is not checked: with −1 it reads what is before the table (GuildNameAt, Q-C1281).
// An index outside the table would read other memory: it stops.
Core::Expected<bool, std::string> SameGuildAsTheHero(const Guild::GuildState& guild, const Kernel::ActorState& hero,
                                                     const Kernel::ActorState& target, std::string_view where)
{
    const auto theirs = Guild::GuildNameAt(guild, static_cast<std::int16_t>(target.word1f2));
    const auto ours = Guild::GuildNameAt(guild, static_cast<std::int16_t>(hero.word1f2));
    if (!theirs || !ours)
        return Core::Unexpected(std::format("{}: el gremio de +0x1f2 cae fuera de la tabla (0x787aa48)", where));
    return *theirs == *ours;
}

// 0x558bb0 after the cursor check with the chosen one, and 0x5588d0 with its argument: the same code. `guildCompare` is
// where each compares the guild names (0x558da4 and 0x558aa9).
Core::Expected<bool, std::string> AttackableTarget(const Kernel::WorldState& world, const Kernel::ActorState& target,
                                                   bool controlDown, std::string_view guildCompare)
{
    const Kernel::WorldRuntime& runtime = world.Runtime();
    const Kernel::ActorState& hero = world.Hero();
    const bool isHero = &target == &hero;
    const Guild::GuildState& guild = runtime.guild;
    if (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap && !isHero)
        return true;
    // 0x558c07: in the war (0x576796c), a monster only with 0x576796d (the war's type, the 61 and the 62).
    if (target.byte88 == MonsterObject)
        return guild.warOn == 0 || guild.warType != 0;
    if (target.byte88 != PlayerObject)
        return false;
    if (runtime.castleFlags.siegeActive)
    {
        if (const auto decided = SiegeDecides(hero, target))
            return *decided;
    }
    // 0x558d6e..0x558e9b (0x558a73..0x558ba3 in 0x5588d0).
    if (target.byte370 == 2 || target.byte370 == 8)
        return true;
    if (guild.warOn != 0 && target.pk >= MinimumOutlawPk && target.word1f2 != NoGuild)
    {
        const auto same = SameGuildAsTheHero(guild, hero, target, guildCompare);
        if (!same.HasValue())
            return Core::Unexpected(same.Error());
        if (same.Value())
            return false;
    }
    // 0x558dff..0x558e3d: with a duel on (the BYTE of 0x78abad8), only the rival: the key with MOVSX against the DWORD
    // of 0x78abadc and strcmp of +0x1d9 with 0x78abae0 (SocialState::duel).
    const Social::DuelState& duel = runtime.social.duel;
    if (duel.active != 0)
    {
        const bool rival = static_cast<std::int32_t>(static_cast<std::int16_t>(target.key)) == duel.rivalKey;
        return rival && CNameOf(target) == DuelNameOf(duel.rivalName);
    }
    if (guild.warOn != 0)
        return target.byte374 == 2 && !isHero;
    if (target.pk >= MinimumOutlawPk)
        return true;
    return controlDown && !isHero;
}
} // namespace

std::string CNameOf(const Kernel::ActorState& actor)
{
    std::string name;
    for (const std::byte letter : actor.name)
    {
        if (letter == std::byte{0})
            return name;
        name.push_back(static_cast<char>(letter));
    }
    if (actor.byte1e3 != 0)
        name.push_back(static_cast<char>(actor.byte1e3));
    return name;
}

Core::Expected<bool, std::string> IsAttackable(const Kernel::WorldState& world, bool controlDown)
{
    const Kernel::WorldRuntime& runtime = world.Runtime();
    if (runtime.cursorItemCount > 0)
        return false;
    const std::int32_t chosen = runtime.selectedTargets[2];
    if (chosen == -1)
        return false;
    const auto actors = world.Actors();
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= actors.size())
        return Core::Unexpected(std::format("0x558bce: el elegido {} está fuera del pool", chosen));
    return AttackableTarget(world, actors[static_cast<std::size_t>(chosen)], controlDown, "0x558da4");
}

Core::Expected<bool, std::string> IsAttackableActor(const Kernel::WorldState& world, const Kernel::ActorState& target,
                                                    bool controlDown)
{
    if (world.Runtime().cursorItemCount > 0)
        return false;
    return AttackableTarget(world, target, controlDown, "0x558aa9");
}

Core::Expected<std::int32_t, std::string> ChosenKeyOf(const Kernel::WorldState& world, std::size_t attacker,
                                                      bool controlDown)
{
    return ChosenKeyOf(world, attacker, world.Runtime().selectedTargets[2], controlDown);
}

Core::Expected<std::int32_t, std::string> ChosenKeyOf(const Kernel::WorldState& world, std::size_t attacker,
                                                      std::int32_t chosen, bool controlDown)
{
    constexpr std::int32_t NoKey = -1;
    const Kernel::WorldRuntime& runtime = world.Runtime();
    // 0x558eb3..0x558ed8: the cursor (JG, signed), the chosen one and the published hero.
    if (runtime.cursorItemCount > 0 || chosen == -1 || attacker != runtime.hero)
        return NoKey;
    const auto actors = world.Actors();
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= actors.size())
        return Core::Unexpected(std::format("0x558ede: el elegido {} está fuera del pool", chosen));
    const Kernel::ActorState& target = actors[static_cast<std::size_t>(chosen)];
    const bool isHero = static_cast<std::size_t>(chosen) == runtime.hero;
    const auto key = static_cast<std::int32_t>(static_cast<std::int16_t>(target.key));
    // 0x558ef0..0x558f08.
    if (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap)
        return key;
    const Guild::GuildState& guild = runtime.guild;
    // 0x558f09..0x558f7c: the guild names, as at 0x558da4; equal, −1.
    if (guild.warOn != 0 && target.pk >= MinimumOutlawPk && target.word1f2 != NoGuild)
    {
        const auto same = SameGuildAsTheHero(guild, actors[attacker], target, "0x558f29");
        if (!same.HasValue())
            return Core::Unexpected(same.Error());
        if (same.Value())
            return NoKey;
    }
    // 0x558f84..0x558fed: with a duel on, the rival's key or −1, without the name.
    if (const Social::DuelState& duel = runtime.social.duel; duel.active != 0)
        return key == duel.rivalKey ? key : NoKey;
    if (target.byte370 == 2 || target.byte370 == 8)
        return key;
    if (guild.warOn != 0)
        return target.byte374 == 2 && !isHero ? key : NoKey;
    if (target.pk >= MinimumOutlawPk)
        return key;
    return controlDown && !isHero ? key : NoKey;
}

bool PassesTheActionBrake(std::uint32_t& last, std::int32_t skill, const std::function<std::uint32_t()>& tickCount)
{
    if (skill != BrakeSkill)
    {
        const auto difference = static_cast<std::int32_t>(tickCount() - last);
        const auto magnitude =
            difference < 0 ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(difference)) : difference;
        if (magnitude <= BrakeMilliseconds)
            return false;
    }
    last = tickCount();
    return true;
}
} // namespace Client::Combat
