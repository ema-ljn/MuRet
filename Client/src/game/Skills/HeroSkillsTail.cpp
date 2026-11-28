#include "Skills/HeroSkillsTail.h"

#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillId.h"
#include "Protocol/Skills/Skill.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Kernel/ActorState.h"
#include "Combat/CombatActionSpeeds.h"
#include "Movement/GameMovementState.h"
#include "Combat/Attackable.h"
#include "Skills/HeroSkillCasts.h"
#include "Skills/HeroSkillSupport.h"
#include "Skills/HeroSkills.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill4e9920.h"
#include "Skills/Skill4f5290.h"
#include "Skills/Skill551f40.h"
#include "Skills/SkillDelays.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"

#include <algorithm>
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
constexpr std::uint8_t FirstSharedSkill = Tables::FirstSiegeSkill;
constexpr std::uint8_t LastSharedSkill = Tables::LastSiegeSkill;
constexpr std::uint8_t SternSkill = Tables::StunSkill;
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DinorantModel = 0x1c15;
constexpr std::uint16_t DarkHorseModel = 0x1c16;
constexpr std::uint16_t FenrirModel = 0x1c37;
constexpr std::uint8_t DarkHorseAction = 0x52;
constexpr std::uint8_t UniriaAction = 0x40;
constexpr std::uint8_t DinorantAction = 0x41;
constexpr std::uint8_t FenrirAction = 0x5d; // EBX, the constant of the function's XOR32 (0x58187f, 0x599d37)
constexpr std::uint8_t StandingAction = 0x3f;
constexpr std::uint8_t CancelAction = 0x87;                                       // 0x598d5b, the one of 0x44 and 0x47
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0.01
constexpr float DirectionScale = std::bit_cast<float>(std::uint32_t{0x3f360b61}); // [0x6ba440], 256/360
constexpr std::int32_t MinimumOffset = -8;
constexpr std::int32_t MaximumOffset = 7;
constexpr std::uint8_t OffsetBias = 8;
constexpr unsigned HighNibble = 4;
constexpr std::uint8_t LowNibbleMask = 0xf;
// 0x57cf90(0): with the null pointer it gives 0 without touching the counter 0x77f3bad.
constexpr std::uint8_t NoMagicKey = 0;

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

std::int32_t Minus(std::int32_t left, std::int32_t right)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right));
}

// 0x5ae300(x, y, targetX, targetY): the target's cell minus (x, y), each axis between −8 and 7; X + 8 on top and Y − 8
// below with the AND. The caller keeps the low byte.
std::uint8_t TowardsTheTarget(std::int32_t x, std::int32_t y, std::int32_t targetX, std::int32_t targetY)
{
    const auto dx = static_cast<std::uint8_t>(std::clamp(Minus(targetX, x), MinimumOffset, MaximumOffset));
    const auto dy = static_cast<std::uint8_t>(std::clamp(Minus(targetY, y), MinimumOffset, MaximumOffset));
    const auto high = static_cast<std::uint8_t>(static_cast<std::uint8_t>(dx + OffsetBias) << HighNibble);
    const auto low = static_cast<std::uint8_t>(static_cast<std::uint8_t>(dy - OffsetBias) & LowNibbleMask);
    return static_cast<std::uint8_t>(high | low);
}

Core::Expected<std::int32_t, std::string> Ftol(double value, std::string_view where)
{
    const auto truncated = Math::OriginalFtolLow(value);
    if (!truncated)
        return Core::Unexpected(std::format("{}: fuera del perfil de ftol", where));
    return *truncated;
}

struct MagicAttackSite
{
    std::string_view brake;
    std::string_view chosenBrake;
    std::string_view chosenKey;
    std::uint8_t standing = StandingAction;
};

// 0x599ed0, the table by skill − 0x43 (from 0x44 on): the brake's GetTickCount without a chosen target and with one,
// the chosen target's key and the final action.
MagicAttackSite SiteOf(std::uint8_t skill)
{
    switch (skill)
    {
    case 0x44:
        return {"0x594ebe", "0x59567d", "0x595858", CancelAction};
    case 0x45:
        return {"0x595ea1", "0x596660", "0x59683b", StandingAction};
    case 0x46:
        return {"0x596e46", "0x597603", "0x5977de", StandingAction};
    case 0x47:
        return {"0x597dfa", "0x5985bf", "0x598796", CancelAction};
    default:
        return {"0x598de1", "0x5995a0", "0x59977b", StandingAction};
    }
}

class SharedTail
{
public:
    SharedTail(Kernel::ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
        : m_hero(hero), m_skill(skill), m_context(context), m_world(context.world), m_runtime(context.world.Runtime()),
          m_state(context.world.Runtime().movement)
    {
    }

    Core::Expected<void, std::string> Use();

private:
    SkillDelayContext Delays() const
    {
        return SkillDelayContext{.world = m_world, .random = m_context.random, .skills = m_context.skills};
    }
    Core::Expected<void, std::string> PayForTheSkill(const SkillCost& cost);
    Core::Expected<void, std::string> CastTheStern();
    Core::Expected<void, std::string> SendTheMagicAttack(const MagicAttackSite& site);
    Core::Expected<void, std::string> ActByTheMount(std::uint8_t standing);

    Kernel::ActorState& m_hero;
    std::uint8_t m_skill;
    const HeroSkillContext& m_context;
    Kernel::WorldState& m_world;
    Kernel::WorldRuntime& m_runtime;
    Movement::GameMovementState& m_state;
};

Core::Expected<void, std::string> SharedTail::Use()
{
    // 0x5808ef..0x5808fa: the own record (0x4e79a0), open until 0x599d4c..0x599d58 (0x40f370, the 3848 rand() if it
    // returns to 0).
    const Models::LogicalAccess own(m_runtime.ownRecordDepth, m_context.random, Models::OwnRecordCloseRandoms);
    // 0x5808ff..0x580911: 0x551c70(skill, 1, 0, &mana, 0, &stamina).
    const auto cost = SkillCostOf(m_skill, m_context, "0x580911");
    if (!cost.HasValue())
        return Core::Unexpected(cost.Error());
    // 0x580916..0x580922: 0x57cfc0's hero has to be a player; otherwise, 0x599d4c (only the close).
    if (m_hero.type != Tables::PlayerSkeletonModel)
        return {};
    if (auto paid = PayForTheSkill(cost.Value()); !paid.HasValue())
        return paid;
    // 0x593fe0..0x594002: the energy (0x551f40), which nobody reads ([ebp−0x84]), and table 0x599ed0 by skill − 0x43.
    if (const auto energy = SkillEnergyRequirement(m_skill, Delays()); !energy.HasValue())
        return Core::Unexpected(energy.Error());
    if (m_skill == SternSkill)
    {
        if (auto cast = CastTheStern(); !cast.HasValue())
            return cast;
        return ActByTheMount(StandingAction);
    }
    const MagicAttackSite site = SiteOf(m_skill);
    if (auto sent = SendTheMagicAttack(site); !sent.HasValue())
        return sent;
    return ActByTheMount(site.standing);
}

// 0x580928..0x580975 and 0x593fbd..0x593fdd: every path continues at 0x593fe0, without checking whether it was enough
// (Q-C1226).
Core::Expected<void, std::string> SharedTail::PayForTheSkill(const SkillCost& cost)
{
    const Kernel::OwnCharacterRecord& record = m_world.OwnCharacter();
    // 0x580928..0x580938: signed JLE against +0x24 of [0x7681eb0].
    if (static_cast<std::int32_t>(cost.mana) > static_cast<std::int32_t>(record.mana))
    {
        // 0x58093e..0x580954: 0x558110(3); with no potion, 0x593fe0. With one, 0x5af520, the C1 26 (0x593874) or the
        // notice of 0x555190 (0x580961): milestone 5.
        return UseAManaPotion(m_context, "0x580954");
    }
    // 0x593fbd..0x593fc8: signed JG against +0x38.
    if (static_cast<std::int32_t>(cost.stamina) > static_cast<std::int32_t>(record.bp))
        return {};
    // 0x593fca..0x593fdd: 0x552c50 with the published hero's +0x425; nobody checks the result.
    const auto started = TryStartSkillDelay(m_world.Hero().skillIndex, Delays());
    if (!started.HasValue())
        return Core::Unexpected(started.Error());
    return {};
}

// 0x594009..0x594de9: skill 0x43.
Core::Expected<void, std::string> SharedTail::CastTheStern()
{
    // 0x594009..0x594027: 0x4e93f0(+0x10, +0x14, +0x3a8, +0x3ac) into +0x24.
    m_hero.angles.z = static_cast<float>(
        Math::CreateAngle(m_hero.position.x, m_hero.position.y, m_hero.attackPosition.x, m_hero.attackPosition.y));
    // 0x59402a..0x59404c: the target's cell, ftol of +0x3a8 · 0.01 and then of +0x3ac · 0.01.
    const auto targetX =
        Ftol(static_cast<double>(m_hero.attackPosition.x) * static_cast<double>(CellReciprocal), "0x594039");
    if (!targetX.HasValue())
        return Core::Unexpected(targetX.Error());
    const auto targetY =
        Ftol(static_cast<double>(m_hero.attackPosition.y) * static_cast<double>(CellReciprocal), "0x59404c");
    if (!targetY.HasValue())
        return Core::Unexpected(targetY.Error());
    // 0x594051..0x594082: 0x4e7e40 on +0x420 and then on +0x41c, and 0x5ae300.
    const std::uint8_t cellY = ReadTheSkillCell(m_hero.cellYAccesses, m_hero.cellY, m_context.random);
    const std::uint8_t cellX = ReadTheSkillCell(m_hero.cellXAccesses, m_hero.cellX, m_context.random);
    const std::uint8_t towards = TowardsTheTarget(cellX, cellY, targetX.Value(), targetY.Value());
    // 0x59407c..0x5940a4: 0x4e9920 with the ftol of +0x3ac and then that of +0x3a8, unscaled.
    const auto wholeY = Ftol(static_cast<double>(m_hero.attackPosition.y), "0x594085");
    if (!wholeY.HasValue())
        return Core::Unexpected(wholeY.Error());
    const auto wholeX = Ftol(static_cast<double>(m_hero.attackPosition.x), "0x594091");
    if (!wholeX.HasValue())
        return Core::Unexpected(wholeX.Error());
    const auto back = RelativeTargetCell(m_hero.position.x, m_hero.position.y, wholeX.Value(), wholeY.Value());
    if (!back)
        return Core::Unexpected(std::string("0x59409f: 0x4e9920 fuera del perfil de ftol"));
    // 0x5940a7..0x5940c5: the key of 0x558eb0 with 0x7758588, or 0xffff with −1.
    std::uint16_t key = Protocol::Skills::NoSkillTargetKey;
    if (m_state.weaponSkill.target != -1)
    {
        const auto chosen =
            Combat::ChosenKeyOf(m_world, SlotOf(m_world, m_hero), m_state.weaponSkill.target, m_context.controlDown);
        if (!chosen.HasValue())
            return Core::Unexpected(chosen.Error());
        key = static_cast<std::uint16_t>(static_cast<std::uint32_t>(chosen.Value()) & 0xffffu);
    }
    // 0x5940c8..0x594135: with "webzen" in the published hero's name, to 0x594de9 without sending (nor the brake: it
    // has none).
    if (!HasTheSilentName(m_world))
    {
        // 0x594137..0x594de1: 0x5767948 and the 1E. 0x4e7e40 reads the logical cell while building it: X (0x5942c9)
        // before Y (0x5943c8).
        m_context.world.Runtime().lastShotSkill = m_skill;
        const std::uint8_t x = ReadTheSkillCell(m_hero.cellXAccesses, m_hero.cellX, m_context.random);
        const std::uint8_t y = ReadTheSkillCell(m_hero.cellYAccesses, m_hero.cellY, m_context.random);
        const auto direction =
            Ftol(static_cast<double>(m_hero.angles.z) * static_cast<double>(DirectionScale), "0x5944d0");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheSkillPacket(
            m_context, Protocol::Skills::WriteDurationMagic({.skill = m_skill,
                                                             .x = x,
                                                             .y = y,
                                                             .direction = static_cast<std::uint8_t>(
                                                                 static_cast<std::uint32_t>(direction.Value()) & 0xffu),
                                                             .towards = towards,
                                                             .back = *back,
                                                             .key = key,
                                                             .magicKey = NoMagicKey}));
    }
    // 0x594de9: the speeds, with the own record still open.
    Combat::RefreshCombatActionSpeeds(m_world, m_context.random);
    return {};
}

// 0x594e42..0x595db3 (and their pairs from 0x45 to 0x48): the 19.
Core::Expected<void, std::string> SharedTail::SendTheMagicAttack(const MagicAttackSite& site)
{
    // The "webzen" name (0x6c920c..0x6c9254, one per path) is checked before the brake, which is checked before
    // building it.
    if (HasTheSilentName(m_world))
        return {};
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    const auto passes = PassesTheSkillBrake(m_skill, m_context, chosen == -1 ? site.brake : site.chosenBrake);
    if (!passes.HasValue())
        return Core::Unexpected(passes.Error());
    if (!passes.Value())
        return {};
    std::uint16_t key = m_context.playerKey;
    if (chosen != -1)
    {
        // The chosen target's WORD +0x1f4, without checking whether it is in the pool.
        if (chosen < 0 || static_cast<std::size_t>(chosen) >= m_world.Actors().size())
            return Core::Unexpected(std::format("{}: el elegido {} está fuera del pool", site.chosenKey, chosen));
        key = m_world.Actors()[static_cast<std::size_t>(chosen)].key;
    }
    SendTheSkillPacket(m_context, Protocol::Skills::WriteMagicAttack(m_skill, key));
    return {};
}

// 0x594dee..0x594e3d, 0x595db8..0x595e20 and the others, up to 0x599d45: the action by mount, each outside the safe
// zone (+0x3e2), and +0x378 at 0.
Core::Expected<void, std::string> SharedTail::ActByTheMount(std::uint8_t standing)
{
    const std::uint16_t mount = m_hero.pet.model;
    const bool outside = m_hero.safeZone == 0;
    std::uint8_t action = standing;
    if (mount == DarkHorseModel && outside)
        action = DarkHorseAction;
    else if (mount == UniriaModel && outside)
        action = UniriaAction;
    else if (mount == DinorantModel && outside)
        action = DinorantAction;
    else if (mount == FenrirModel && outside)
        action = FenrirAction;
    if (auto acted = SetTheActorAction(m_hero, action, CombatContextOf(m_context)); !acted.HasValue())
        return acted;
    m_hero.movement = 0;
    return {};
}
} // namespace

Core::Expected<void, std::string> UseTheSharedTail(Kernel::ActorState& hero, std::uint8_t skill,
                                                   const HeroSkillContext& context)
{
    // 0x5808da..0x5808e9.
    if (!InRange(skill, FirstSharedSkill, LastSharedSkill))
        return {};
    return SharedTail(hero, skill, context).Use();
}
} // namespace Client::Skills
