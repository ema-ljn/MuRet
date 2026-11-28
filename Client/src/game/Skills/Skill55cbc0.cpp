#include "Skills/Skill55cbc0.h"

#include "Core/Expected.h"
#include "Protocol/Skills/Skill.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Combat/ActorCombat.h"
#include "Kernel/ActorState.h"
#include "Combat/CombatActionSpeeds.h"
#include "Movement/GameMovementState.h"
#include "Combat/Attackable.h"
#include "Skills/HeroSkillCasts.h"
#include "Skills/HeroSkillSupport.h"
#include "Skills/HeroSkills.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill4f5290.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Skills
{
namespace
{
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0.01
constexpr float DirectionScale = std::bit_cast<float>(std::uint32_t{0x3f360b61}); // [0x6ba440], 256/360
constexpr std::uint8_t ThrustSkill = 0x49;
constexpr std::uint8_t ThrustAction = 0x86;

// 0x55fa54 by skill − 1 (0x49 entries) into 0x55fa44: 0, the 19 and 0x4f5290; 1, the 1E; 2, the 19 of 0x49.
enum class ReachedCase : std::uint8_t
{
    Strike,
    Duration,
    Thrust,
    Nothing,
};

ReachedCase CaseOf(std::uint8_t skill)
{
    switch (skill)
    {
    case 1:
    case 2:
    case 3:
    case 4:
    case 7:
    case 0xb:
    case 0x11:
    case 0x2d:
        return ReachedCase::Strike;
    case 0xd:
        return ReachedCase::Duration;
    case ThrustSkill:
        return ReachedCase::Thrust;
    default:
        return ReachedCase::Nothing;
    }
}

std::optional<std::uint8_t> LowByteOf(double value)
{
    const auto truncated = Math::OriginalFtolLow(value);
    if (!truncated)
        return std::nullopt;
    return static_cast<std::uint8_t>(*truncated & 0xff);
}

// 0x55d4a9..0x55d4e4 (and 0x55e150.., 0x55f409..): the published hero's X (+0x41c) and Y (+0x420), each opened and
// closed, and the turning D3 with its angle.
Core::Expected<void, std::string> TurnThePublishedHero(const HeroSkillContext& context, std::string_view where)
{
    Kernel::ActorState& published = context.world.Hero();
    const std::uint8_t x = ReadTheSkillCell(published.cellXAccesses, published.cellX, context.random);
    const std::uint8_t y = ReadTheSkillCell(published.cellYAccesses, published.cellY, context.random);
    return SendTheTurn(x, y, published.angles.z, context, where);
}

// 0x55cd47..0x55d499 and 0x55d9da..0x55e133: without "webzen" and with the brake (which with 0x28 does not brake), the
// C1 06 19.
Core::Expected<void, std::string> StrikeTheTarget(std::uint8_t skill, const Kernel::ActorState& target,
                                                  const HeroSkillContext& context, std::string_view where)
{
    if (HasTheSilentName(context.world))
        return {};
    const auto passes = PassesTheSkillBrake(skill, context, where);
    if (!passes.HasValue())
        return Core::Unexpected(passes.Error());
    if (passes.Value())
        SendTheSkillPacket(context, Protocol::Skills::WriteMagicAttack(skill, target.key));
    return {};
}
} // namespace

Core::Expected<void, std::string> UseTheSkillOnItsTarget(Kernel::ActorState& user, Kernel::ActorState& turned,
                                                         const HeroSkillContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const Movement::WeaponSkillRequest& request = runtime.movement.weaponSkill;
    // 0x55cbe4..0x55cc9a: the slot's skill, with the own record opened and closed.
    std::uint8_t skill = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
        skill = Kernel::OwnSkillByte(world.OwnCharacter(), request.slot);
    }
    if (skill == 0x2b || skill == 0x2f)
        return {};
    if (skill == ThrustSkill)
    {
        // 0x55ccb1..0x55cccc: the signed WORD CMP.
        const auto right = static_cast<std::int16_t>(world.Hero().hands[0].model);
        if (right < 0xc12 || right >= 0xe12)
            return {};
    }
    // 0x55ccd2..0x55cd26: the target without checking the −1. With −1 the original reads the position of actor[−1],
    // before the pool (undefined behavior); MuRet does not read outside the pool and cuts (07, Q-C1239). The callers
    // never arrive with it: HeroAttack leaves first with −1 or less and the Wizard's branch (0x58eecc) asks for an
    // attackable chosen target.
    if (request.target < 0 || static_cast<std::size_t>(request.target) >= world.Actors().size())
        return Core::Unexpected(
            std::format("0x55ccd2: el blanco {} de la skill del arma está fuera del pool", request.target));
    Kernel::ActorState& target = world.Actors()[static_cast<std::size_t>(request.target)];
    user.attackPosition = target.position;
    turned.angles.z = static_cast<float>(
        Math::CreateAngle(turned.position.x, turned.position.y, target.position.x, target.position.y));
    const Combat::ActorCombatContext combat = CombatContextOf(context);
    switch (CaseOf(skill))
    {
    case ReachedCase::Strike:
    {
        if (auto struck = StrikeTheTarget(skill, target, context, "0x55cdc8"); !struck.HasValue())
            return struck;
        if (auto hit = SetTheHitAction(user, combat); !hit.HasValue())
            return hit;
        return TurnThePublishedHero(context, "0x55d76a");
    }
    case ReachedCase::Thrust:
    {
        if (auto struck = StrikeTheTarget(skill, target, context, "0x55da5b"); !struck.HasValue())
            return struck;
        if (auto acted = SetTheActorAction(turned, ThrustAction, combat); !acted.HasValue())
            return acted;
        Combat::RefreshCombatActionSpeeds(world, context.random);
        return TurnThePublishedHero(context, "0x55e40e");
    }
    case ReachedCase::Duration:
    {
        // 0x55e67e..0x55e699: the key through 0x558eb0(p1, target), no rand(). With the target at −1 the original
        // leaves 0xffff, but that −1 already cut at 0x55ccd2 (Q-C1239): here the target is always in the pool.
        const auto chosen = Combat::ChosenKeyOf(world, SlotOf(world, user), request.target, context.controlDown);
        if (!chosen.HasValue())
            return Core::Unexpected(chosen.Error());
        const auto key = static_cast<std::uint16_t>(chosen.Value());
        if (!HasTheSilentName(world))
        {
            runtime.lastShotSkill = skill;
            // 0x55e881..0x55ea8e: the cell of p1's +0x3a8/+0x3ac and p2's heading, each the low byte of the ftol.
            const auto x = LowByteOf(static_cast<double>(user.attackPosition.x) * static_cast<double>(CellReciprocal));
            const auto y = LowByteOf(static_cast<double>(user.attackPosition.y) * static_cast<double>(CellReciprocal));
            const auto direction =
                LowByteOf(static_cast<double>(turned.angles.z) * static_cast<double>(DirectionScale));
            if (!x || !y || !direction)
                return Core::Unexpected(std::string("0x55e890: la celda o el rumbo del 1E fuera del perfil ftol"));
            SendTheSkillPacket(context, Protocol::Skills::WriteDurationMagic(
                                            {.skill = skill, .x = *x, .y = *y, .direction = *direction, .key = key}));
        }
        if (auto hit = SetTheHitAction(user, combat); !hit.HasValue())
            return hit;
        return TurnThePublishedHero(context, "0x55f7dd");
    }
    case ReachedCase::Nothing:
        break;
    }
    return {};
}
} // namespace Client::Skills
