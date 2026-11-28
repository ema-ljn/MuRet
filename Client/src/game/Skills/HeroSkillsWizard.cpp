#include "Skills/HeroSkillsWizard.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillId.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Kernel/CharacterClass.h"
#include "Actors/WalkPath.h"
#include "Maps/MapId.h"
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
#include "Skills/HeroSkillsTail.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Party/PartyActors.h"
#include "Skills/Skill4e9920.h"
#include "Skills/Skill4e9c50.h"
#include "Skills/Skill4f5290.h"
#include "Skills/Skill551f40.h"
#include "Skills/Skill55cbc0.h"
#include "Skills/Skill57cf90.h"
#include "Skills/Skill57ce80.h"
#include "Skills/SkillDelays.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Math/Ftol.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainDefines.h"

#include <bit>
#include <cmath>
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
constexpr std::uint8_t ChargedSkill = Tables::NovaSkill;
constexpr std::uint8_t ReleasedChargedSkill = 0x3a;
constexpr std::uint8_t PlayerObject = 1;
constexpr std::uint32_t FrozenHeroFlag = 0x1000;
using Maps::CastleSiegeMap;
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0.01
constexpr float DirectionScale = std::bit_cast<float>(std::uint32_t{0x3f360b61}); // [0x6ba440], 256/360
using Terrain::CellSize;                                                          // [0x6b86b4]
constexpr float HalfCell = 50.0f;                                                 // [0x6b86b0]
constexpr float WalkReach = std::bit_cast<float>(std::uint32_t{0x3f99999a});      // [0x6b8bb0], 1.2
constexpr float VisibleAlpha = std::bit_cast<float>(std::uint32_t{0x3f333333});   // [0x6b8a5c], 0.7
constexpr std::uint32_t TeleportWaitMs = 3000;                                    // 0x593146
constexpr std::int32_t MaximumTries = 10;

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

bool IsPlayer(const Kernel::ActorState& actor)
{
    return actor.type == Tables::PlayerSkeletonModel;
}

Core::Expected<void, std::string> Act(Kernel::ActorState& actor, std::uint8_t action, const HeroSkillContext& context)
{
    return SetTheActorAction(actor, action, CombatContextOf(context));
}

std::optional<std::uint8_t> LowByteOf(double value)
{
    const auto truncated = Math::OriginalFtolLow(value);
    if (!truncated)
        return std::nullopt;
    return static_cast<std::uint8_t>(*truncated & 0xff);
}

// The 1E's heading: ftol(+0x24 · 0.7111) (0x58c610 and the others), the low byte.
Core::Expected<std::uint8_t, std::string> DirectionOf(const Kernel::ActorState& hero, std::string_view where)
{
    const auto direction = LowByteOf(static_cast<double>(hero.angles.z) * static_cast<double>(DirectionScale));
    if (!direction)
        return Core::Unexpected(std::format("{}: el rumbo del 1E fuera del perfil ftol", where));
    return *direction;
}

struct SkillCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// The hero's logical cell for the 1E and the path: Y (+0x420) before X (+0x41c), 4 + 4 rand().
SkillCell ReadTheHeroCell(Kernel::ActorState& hero, const HeroSkillContext& context)
{
    const std::uint8_t y = ReadTheSkillCell(hero.cellYAccesses, hero.cellY, context.random);
    const std::uint8_t x = ReadTheSkillCell(hero.cellXAccesses, hero.cellX, context.random);
    return {x, y};
}

// The cell of +0x3a8/+0x3ac (ftol · 0.01, X first), no rand(): that of 5, 0x26 and 0x27.
Core::Expected<SkillCell, std::string> AttackCellOf(const Kernel::ActorState& hero, std::string_view where)
{
    const auto x = LowByteOf(static_cast<double>(hero.attackPosition.x) * static_cast<double>(CellReciprocal));
    const auto y = LowByteOf(static_cast<double>(hero.attackPosition.y) * static_cast<double>(CellReciprocal));
    if (!x || !y)
        return Core::Unexpected(std::format("{}: la celda del blanco del 1E fuera del perfil ftol", where));
    return SkillCell{*x, *y};
}

// 0x58ecaa..0x58ecf9 (and 0x58b0cd.., 0x58dcec..): the distance to the center of the target's cell (0x77e1ba8,
// 0x77e1bac) against the reach · 100, on the x87. Less, equal or unordered (C0 or C3) is in reach.
bool InReach(const Kernel::ActorState& hero, const Movement::GameMovementState& state)
{
    const auto targetX = std::bit_cast<std::int32_t>(state.targetX);
    const auto targetY = std::bit_cast<std::int32_t>(state.targetY);
    const double dx = static_cast<double>(hero.position.x) -
                      (static_cast<double>(targetX) * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double dy = static_cast<double>(hero.position.y) -
                      (static_cast<double>(targetY) * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double limit = static_cast<double>(state.selectedSkillRange) * static_cast<double>(CellSize);
    return !(distance > limit);
}

// 0x4eac00(X, Y, x, y, +0x3e8, distance) from the hero's cell: Y and X opened and closed first (8 rand()). With a
// path, +0x378 at 1; `withType` also sets +0x379 to 5 (the arrival's 0x55cbc0, 0x5650c0). Without a path, nothing.
// With the goal outside 0..255, which PathFinding2 takes as a linear index (the next row, or past the 65536 WORDs),
// MuRet drops the path after the reads of the hero's cell, as with no path (08, P-C256).
Core::Expected<void, std::string> WalkToward(Kernel::ActorState& hero, std::int32_t goalX, std::int32_t goalY,
                                             float distance, bool withType, const HeroSkillContext& context,
                                             std::string_view where)
{
    const SkillCell start = ReadTheHeroCell(hero, context);
    constexpr std::int32_t LastCell = 0xff;
    if (!InRange(goalX, 0, LastCell) || !InRange(goalY, 0, LastCell))
        return {};
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::format("{}: falta TerrainWall para el camino de la skill", where));
    // 0x4eac0a..0x4eac41: the Crywolf altar flag, which does not count with a nonzero distance (0x4eadd4).
    const auto path =
        Actors::FindThePath(context.world, terrain.attributes->cells, {start.x, start.y},
                            {static_cast<std::uint8_t>(goalX), static_cast<std::uint8_t>(goalY)}, distance);
    if (!path)
        return {};
    hero.path = *path;
    hero.movement = 1;
    if (withType)
        hero.movementType = 5;
    return {};
}

// 0x4f5370: 0x4ea030 with 0x93 on a player and 5 on another.
Core::Expected<void, std::string> ActTheTeleport(Kernel::ActorState& actor, const HeroSkillContext& context)
{
    return Act(actor, IsPlayer(actor) ? std::uint8_t{0x93} : std::uint8_t{5}, context);
}

// The action with which Teleport and Teleport Ally do not run (0x593071..0x5930b8, 0x59267b..0x592799): the player in
// 0x93 or the other in 5, +0x80 at 1 or 2, or the alpha (+0x174) below 0.7 or unordered.
bool CannotTeleport(const Kernel::ActorState& actor)
{
    const std::uint8_t action = actor.animation.action;
    if (IsPlayer(actor) ? action == 0x93 : action == 5)
        return true;
    if (actor.byte80 == 1 || actor.byte80 == 2)
        return true;
    return !(actor.alpha >= VisibleAlpha);
}

class WizardBranch
{
public:
    WizardBranch(Kernel::ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
        : m_hero(hero), m_skill(skill), m_context(context), m_world(context.world), m_runtime(context.world.Runtime()),
          m_state(context.world.Runtime().movement)
    {
    }

    Core::Expected<void, std::string> Use();

private:
    Core::Expected<bool, std::string> PassesTheHead();
    Core::Expected<bool, std::string> Attackable() const
    {
        return Combat::IsAttackable(m_world, m_context.controlDown);
    }
    Core::Expected<Kernel::ActorState*, std::string> ChosenActor(std::string_view where) const;
    Core::Expected<void, std::string> SendTheMagicAttack(std::uint16_t key, std::string_view brake);
    Core::Expected<void, std::string> ReleaseTheCharge();
    Core::Expected<void, std::string> ShootTheCharge();
    Core::Expected<void, std::string> RaiseTheSoulBarrier();
    Core::Expected<void, std::string> CastAround(std::uint8_t action, std::string_view where);
    Core::Expected<void, std::string> CastTheSkill4c();
    Core::Expected<void, std::string> UseByReach();
    Core::Expected<void, std::string> CastAtTheCell(std::string_view where);
    Core::Expected<void, std::string> CastTheMagicKey();
    Core::Expected<void, std::string> CastTheAquaBeam();
    Core::Expected<void, std::string> CastTheFlame();
    Core::Expected<void, std::string> CastAtTheWeaponTarget();
    Core::Expected<void, std::string> UseTheWizardOnly();
    Core::Expected<void, std::string> Teleport();
    Core::Expected<void, std::string> TeleportTheAlly();

    Kernel::ActorState& m_hero;
    std::uint8_t m_skill;
    const HeroSkillContext& m_context;
    Kernel::WorldState& m_world;
    Kernel::WorldRuntime& m_runtime;
    Movement::GameMovementState& m_state;
    Kernel::CharacterClass m_class = Kernel::CharacterClass::DarkWizard;
    bool m_aimed = false;
};

Core::Expected<Kernel::ActorState*, std::string> WizardBranch::ChosenActor(std::string_view where) const
{
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= m_world.Actors().size())
        return Core::Unexpected(std::format("{}: el elegido {} está fuera del pool", where, chosen));
    return &m_world.Actors()[static_cast<std::size_t>(chosen)];
}

// The inline C1 06 19 (0x58a1e5.., 0x58a954.., 0x58b285.., 0x58ba70..): without "webzen" in the published hero and
// with the brake (which with 0x28 does not brake and records anyway).
Core::Expected<void, std::string> WizardBranch::SendTheMagicAttack(std::uint16_t key, std::string_view brake)
{
    if (HasTheSilentName(m_world))
        return {};
    const auto passes = PassesTheSkillBrake(m_skill, m_context, brake);
    if (!passes.HasValue())
        return Core::Unexpected(passes.Error());
    if (passes.Value())
        SendTheSkillPacket(m_context, Protocol::Skills::WriteMagicAttack(m_skill, key));
    return {};
}

// 0x5897b7..0x58a117: true if it continues to 0x57ce80, with the own record already closed.
Core::Expected<bool, std::string> WizardBranch::PassesTheHead()
{
    const Models::LogicalAccess own(m_runtime.ownRecordDepth, m_context.random, Models::OwnRecordCloseRandoms);
    // 0x58982d..0x58987d: 0x3a and 0x28 cost what 0x28 costs, and 0x28 without stamina.
    const bool charged = m_skill == ReleasedChargedSkill || m_skill == ChargedSkill;
    const auto cost = SkillCostOf(charged ? ChargedSkill : m_skill, m_context, charged ? "0x589866" : "0x58984c");
    if (!cost.HasValue())
        return Core::Unexpected(cost.Error());
    const auto mana = static_cast<std::int32_t>(cost.Value().mana);
    const auto stamina = m_skill == ChargedSkill ? 0 : static_cast<std::int32_t>(cost.Value().stamina);
    const auto energy = SkillEnergyRequirement(
        m_skill, SkillDelayContext{.world = m_world, .random = m_context.random, .skills = m_context.skills});
    if (!energy.HasValue())
        return Core::Unexpected(energy.Error());
    const Kernel::OwnCharacterRecord& record = m_world.OwnCharacter();
    // 0x58988a..0x5898a5: signed JLE against +0x1e + +0x30.
    if (energy.Value() > static_cast<std::int32_t>(record.word30) + static_cast<std::int32_t>(record.energy))
        return false;
    // 0x58990f..0x58991a.
    if (mana > static_cast<std::int32_t>(record.mana))
    {
        if (auto potion = UseAManaPotion(m_context, "0x589936"); !potion.HasValue())
            return Core::Unexpected(potion.Error());
        return false;
    }
    // 0x58a036..0x58a06f.
    if (stamina > static_cast<std::int32_t>(record.bp))
    {
        if (charged)
        {
            Ui::GameUiState& ui = m_runtime.ui;
            ui.rightReleased = false;
            ui.rightClick = false;
            ui.rightDown = false;
            ui.rightHoldFrames = 0;
        }
        return false;
    }
    // 0x58a071..0x58a08a: the delay of the published hero's slot skill, with the record open.
    const auto ready =
        TryStartSkillDelay(m_world.Hero().skillIndex,
                           SkillDelayContext{.world = m_world, .random = m_context.random, .skills = m_context.skills});
    if (!ready.HasValue())
        return Core::Unexpected(ready.Error());
    return ready.Value();
}

Core::Expected<void, std::string> WizardBranch::Use()
{
    m_class = Kernel::BaseClassOf(m_hero.characterClass);
    // 0x5897a7..0x5897b1.
    if (m_class != Kernel::CharacterClass::DarkWizard && m_class != Kernel::CharacterClass::MagicGladiator)
        return UseTheSharedTail(m_hero, m_skill, m_context);
    const auto head = PassesTheHead();
    if (!head.HasValue())
        return Core::Unexpected(head.Error());
    if (!head.Value())
        return {};
    // 0x58a117..0x58a120: the result stays in [ebp−0x3d].
    const auto aimed = AimTheHeroSkill(m_hero, m_context);
    if (!aimed.HasValue())
        return Core::Unexpected(aimed.Error());
    m_aimed = aimed.Value();
    // 0x58a126..0x58a13a: the skill − 0xa through tables 0x599e54 and 0x599e34.
    switch (m_skill)
    {
    case 0xa:
        return CastAround(0x96, "0x58c1b6");
    case 0xe:
        return CastAround(0x95, "0x58cf37");
    case 0x10:
        return RaiseTheSoulBarrier();
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x2f:
        return {};
    case ChargedSkill:
        return ShootTheCharge();
    case ReleasedChargedSkill:
        return ReleaseTheCharge();
    case 0x4c:
        return CastTheSkill4c();
    default:
        return UseByReach();
    }
}

// 0x58a141..0x58a899: the released 0x3a. Without a player, nothing; otherwise the 19 with the player key (0x57678bc),
// the speeds (0x4f3f80), action 0x44 and +0x378 at 0.
Core::Expected<void, std::string> WizardBranch::ReleaseTheCharge()
{
    if (!IsPlayer(m_hero))
        return {};
    if (auto sent = SendTheMagicAttack(m_context.playerKey, "0x58a1be"); !sent.HasValue())
        return sent;
    Combat::RefreshCombatActionSpeeds(m_world, m_context.random);
    if (auto acted = Act(m_hero, 0x44, m_context); !acted.HasValue())
        return acted;
    m_hero.movement = 0;
    return {};
}

// 0x58a89a..0x58b025: 0x28 uncharged (the auto attack or the locked left click). The chosen target's key through
// 0x558eb0, or the player's; the brake does not brake; action 0x45.
Core::Expected<void, std::string> WizardBranch::ShootTheCharge()
{
    if (!IsPlayer(m_hero))
        return {};
    const auto chosenKey =
        Combat::ChosenKeyOf(m_world, SlotOf(m_world, m_hero), m_runtime.selectedTargets[2], m_context.controlDown);
    if (!chosenKey.HasValue())
        return Core::Unexpected(chosenKey.Error());
    const std::uint16_t key =
        chosenKey.Value() == -1 ? m_context.playerKey : static_cast<std::uint16_t>(chosenKey.Value());
    if (auto sent = SendTheMagicAttack(key, "0x58a938"); !sent.HasValue())
        return sent;
    Combat::RefreshCombatActionSpeeds(m_world, m_context.random);
    if (auto acted = Act(m_hero, 0x45, m_context); !acted.HasValue())
        return acted;
    m_hero.movement = 0;
    return {};
}

// 0x58b026..0x58c15b: Soul Barrier. With no chosen target, the 19 with the player key; with a chosen non-player,
// 0x6c8e38 at −1 and nothing; with a party member (0x59e680), +0x3a4, 0x7758580..88 and, out of reach, the path
// (+0x379 at 5) and nothing more; in reach, the 19 with the target's key. After the 19 (or not sending it), 0x4f5290
// and it continues like the others (0x58c15e).
Core::Expected<void, std::string> WizardBranch::RaiseTheSoulBarrier()
{
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    if (chosen == -1)
    {
        if (auto sent = SendTheMagicAttack(m_context.playerKey, "0x58ba54"); !sent.HasValue())
            return sent;
    }
    else
    {
        const auto target = ChosenActor("0x58b034");
        if (!target.HasValue())
            return Core::Unexpected(target.Error());
        if (target.Value()->byte88 != PlayerObject)
        {
            m_runtime.selectedTargets[4] = -1;
            return {};
        }
        // 0x58b06e: 0x59e680, the chosen target's name among the party's (0x77f3cd0, in steps of 0x24); outside it or
        // with no party, nothing (0x599d5d).
        Kernel::ActorState& member = *target.Value();
        if (!Party::IsInTheParty(m_world, member))
            return {};
        // 0x58b07e..0x58b0c7: +0x3a4 with the chosen target (WORD) and 0x7758580..88 with 1, the published hero's skill
        // slot
        // (+0x425, without the own record) and the chosen target.
        m_hero.attackTarget = static_cast<std::uint16_t>(chosen);
        m_state.weaponSkill.active = 1;
        m_state.weaponSkill.slot = m_world.Hero().skillIndex;
        m_state.weaponSkill.target = chosen;
        if (!InReach(m_hero, m_state))
        {
            // 0x58b11f..0x58b1ff: the path up to the reach, with +0x378 at 1 and +0x379 at 5, and nothing more.
            return WalkToward(m_hero, std::bit_cast<std::int32_t>(m_state.targetX),
                              std::bit_cast<std::int32_t>(m_state.targetY), m_state.selectedSkillRange, true, m_context,
                              "0x58b1d1");
        }
        // 0x58b200..0x58b50d: the 19 with the key of 0x7758588's character.
        if (auto sent = SendTheMagicAttack(member.key, "0x58b269"); !sent.HasValue())
            return sent;
    }
    // 0x58c152..0x58c15b.
    if (auto hit = SetTheHitAction(m_hero, CombatContextOf(m_context)); !hit.HasValue())
        return hit;
    return UseByReach();
}

// 0x58c1b6..0x58cf36 (Hellfire, 0xa) and 0x58cf37..0x58dcc3 (Inferno, 0xe): only a player. Without "webzen",
// 0x5767948 and the 1E with the hero's cell (Y and X), the heading and key 0xffff; then the speeds, `action` and
// +0x378 at 0.
Core::Expected<void, std::string> WizardBranch::CastAround(std::uint8_t action, std::string_view where)
{
    if (!IsPlayer(m_hero))
        return {};
    if (!HasTheSilentName(m_world))
    {
        m_context.world.Runtime().lastShotSkill = m_skill;
        const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
        const auto direction = DirectionOf(m_hero, where);
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheSkillPacket(m_context,
                           Protocol::Skills::WriteDurationMagic({.skill = m_skill,
                                                                 .x = cell.x,
                                                                 .y = cell.y,
                                                                 .direction = direction.Value(),
                                                                 .key = Protocol::Skills::NoSkillTargetKey}));
    }
    Combat::RefreshCombatActionSpeeds(m_world, m_context.random);
    if (auto acted = Act(m_hero, action, m_context); !acted.HasValue())
        return acted;
    m_hero.movement = 0;
    return {};
}

// 0x58dcc4..0x58eca3: skill 0x4c. The cell of +0x3a8 (ftol · 0.01) is the path's goal; in reach and with an
// attackable chosen target, the relative position (0x4e9920), the angle, the delay (0x552990), the key (0x558eb0),
// +0x434 and the 1E with the magic key; then +0x378 at 0 and, on a player, 0x4e9c50. Otherwise, with no chosen target
// 0x6c8e38 at −1; with one, the path up to the reach · 1.2 (without +0x379).
Core::Expected<void, std::string> WizardBranch::CastTheSkill4c()
{
    const auto aimX =
        Math::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.x) * static_cast<double>(CellReciprocal));
    const auto aimY =
        Math::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.y) * static_cast<double>(CellReciprocal));
    if (!aimX || !aimY)
        return Core::Unexpected(std::string("0x58dcd3: la celda del blanco de la 0x4c fuera del perfil ftol"));
    bool casts = InReach(m_hero, m_state);
    if (casts)
    {
        const auto attackable = Attackable();
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        casts = attackable.Value();
    }
    if (!casts)
    {
        // 0x58ebaa..0x58eca3.
        if (m_runtime.selectedTargets[2] == -1)
        {
            m_runtime.selectedTargets[4] = -1;
            return {};
        }
        // 0x58ec44..0x58ec5a: FMUL and FSTP to a float on the stack.
        const auto reach =
            static_cast<float>(static_cast<double>(m_state.selectedSkillRange) * static_cast<double>(WalkReach));
        return WalkToward(m_hero, *aimX, *aimY, reach, false, m_context, "0x58ec65");
    }
    // 0x58dd50..0x58dd70: the ftol of +0x3ac and +0x3a8, unscaled.
    const auto wholeY = Math::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.y));
    const auto wholeX = Math::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.x));
    if (!wholeX || !wholeY)
        return Core::Unexpected(std::string("0x58dd56: la posición del blanco de la 0x4c fuera del perfil ftol"));
    const auto relative = RelativeTargetCell(m_hero.position.x, m_hero.position.y, *wholeX, *wholeY);
    if (!relative)
        return Core::Unexpected(std::string("0x58dd70: un ftol de la celda relativa de la 0x4c fuera de su perfil"));
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    if (chosen == -1)
    {
        m_hero.skillTarget4c = -1;
        return {};
    }
    m_hero.angles.z = static_cast<float>(
        Math::CreateAngle(m_hero.position.x, m_hero.position.y, m_hero.attackPosition.x, m_hero.attackPosition.y));
    if (auto delayed = StartSkillDelay(
            m_world.Hero().skillIndex,
            SkillDelayContext{.world = m_world, .random = m_context.random, .skills = m_context.skills});
        !delayed.HasValue())
        return delayed;
    const auto key = Combat::ChosenKeyOf(m_world, SlotOf(m_world, m_hero), chosen, m_context.controlDown);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    m_hero.skillTarget4c = chosen;
    if (!HasTheSilentName(m_world))
    {
        m_context.world.Runtime().lastShotSkill = m_skill;
        const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
        const auto direction = DirectionOf(m_hero, "0x58e232");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        // 0x58e6d9..0x58e6fe: 0x57cf90 inline, with the hero's +0x1bd.
        const std::uint8_t magicKey = NextMagicKey(m_state, m_hero.magicKey);
        SendTheSkillPacket(m_context,
                           Protocol::Skills::WriteDurationMagic({.skill = m_skill,
                                                                 .x = cell.x,
                                                                 .y = cell.y,
                                                                 .direction = direction.Value(),
                                                                 .back = *relative,
                                                                 .key = static_cast<std::uint16_t>(key.Value()),
                                                                 .magicKey = magicKey}));
    }
    // 0x58eb62..0x58eb7a.
    m_hero.movement = 0;
    if (!IsPlayer(m_hero))
        return {};
    return SetTheActorAction(m_hero, ActionByTheHands(m_hero), CombatContextOf(m_context));
}

// 0x58c15e..0x58ef17: the others by reach.
Core::Expected<void, std::string> WizardBranch::UseByReach()
{
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    if (chosen != -1)
    {
        // 0x58c16e..0x58c1b1.
        m_state.weaponSkill.active = 1;
        m_state.weaponSkill.slot = m_world.Hero().skillIndex;
        const auto attackable = Attackable();
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        m_state.weaponSkill.target = attackable.Value() ? chosen : -1;
    }
    if (!InReach(m_hero, m_state))
    {
        // 0x58ecff..0x58edff: with an attackable chosen target, the path up to the reach (+0x379 at 5).
        if (chosen != -1)
        {
            const auto attackable = Attackable();
            if (!attackable.HasValue())
                return Core::Unexpected(attackable.Error());
            if (attackable.Value())
            {
                if (auto walked = WalkToward(m_hero, std::bit_cast<std::int32_t>(m_state.targetX),
                                             std::bit_cast<std::int32_t>(m_state.targetY), m_state.selectedSkillRange,
                                             true, m_context, "0x58edc6");
                    !walked.HasValue())
                    return walked;
            }
        }
        if (!InRange(m_skill, 0x43, 0x47) && m_skill != 0x4c)
            return {};
    }
    // 0x58ee05..0x58eeb2: the line of sight from the hero's cell (Y and X). MuRet takes the line that leaves
    // TerrainWall (0x55886d reads past the 65536 WORDs) as no line (08, P-C256).
    const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(m_context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x558800: falta TerrainWall para ver el blanco de la skill"));
    const bool seen = Pathfinding::CanSeeTheCell(terrain.attributes->cells, cell.x, cell.y,
                                                 std::bit_cast<std::int32_t>(m_state.targetX),
                                                 std::bit_cast<std::int32_t>(m_state.targetY))
                          .value_or(false);
    if (!seen)
        return UseTheWizardOnly();
    // 0x58eeb8..0x58eecc.
    if (m_runtime.selectedTargets[2] != -1)
    {
        const auto attackable = Attackable();
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        if (attackable.Value())
        {
            if (auto reached = UseTheSkillOnItsTarget(m_hero, m_hero, m_context); !reached.HasValue())
                return reached;
        }
    }
    if (!m_aimed)
        return UseTheWizardOnly();
    // 0x58eedf..0x58ef17: the angle towards +0x3a8 and the skill − 5 through tables 0x599eac and 0x599e98.
    m_hero.angles.z = static_cast<float>(
        Math::CreateAngle(m_hero.position.x, m_hero.position.y, m_hero.attackPosition.x, m_hero.attackPosition.y));
    switch (m_skill)
    {
    case 5:
        return CastTheFlame();
    case 8:
    case 9:
        return CastTheMagicKey();
    case 0xc:
        return CastTheAquaBeam();
    case 0x26:
    case 0x27:
        return CastAtTheWeaponTarget();
    default:
        return UseTheWizardOnly();
    }
}

// 0x58ef1e..0x58fcec: 8 and 9, of any type. Without "webzen", 0x5767948 and the 1E with the hero's cell, the heading,
// key 0xffff and the magic key; then 0x4f5290.
Core::Expected<void, std::string> WizardBranch::CastTheMagicKey()
{
    if (!HasTheSilentName(m_world))
    {
        m_context.world.Runtime().lastShotSkill = m_skill;
        const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
        const auto direction = DirectionOf(m_hero, "0x58f371");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        // 0x58f822..0x58f849: 0x57cf90 inline, with the hero's +0x1bd.
        const std::uint8_t magicKey = NextMagicKey(m_state, m_hero.magicKey);
        SendTheSkillPacket(m_context, Protocol::Skills::WriteDurationMagic({.skill = m_skill,
                                                                            .x = cell.x,
                                                                            .y = cell.y,
                                                                            .direction = direction.Value(),
                                                                            .key = Protocol::Skills::NoSkillTargetKey,
                                                                            .magicKey = magicKey}));
    }
    return SetTheHitAction(m_hero, CombatContextOf(m_context));
}

// 0x58fced..0x590b17: Aqua Beam (0xc), only a player: like Hellfire, with action 0x94 and 0x77f3ba4 at 0. The +0x3e2
// it reads with Fenrir (0x590ae2) goes unused.
Core::Expected<void, std::string> WizardBranch::CastTheAquaBeam()
{
    if (!IsPlayer(m_hero))
        return {};
    if (auto cast = CastAround(0x94, "0x59018f"); !cast.HasValue())
        return cast;
    m_state.dword77f3ba4 = 0;
    return {};
}

// 0x590b18..0x591858: Flame (5), of any type. The 1E with the cell of +0x3a8 and key 0xffff; then 0x4f5290.
Core::Expected<void, std::string> WizardBranch::CastTheFlame()
{
    if (!HasTheSilentName(m_world))
    {
        m_context.world.Runtime().lastShotSkill = m_skill;
        const auto cell = AttackCellOf(m_hero, "0x590d01");
        if (!cell.HasValue())
            return Core::Unexpected(cell.Error());
        const auto direction = DirectionOf(m_hero, "0x590ef9");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheSkillPacket(m_context,
                           Protocol::Skills::WriteDurationMagic({.skill = m_skill,
                                                                 .x = cell.Value().x,
                                                                 .y = cell.Value().y,
                                                                 .direction = direction.Value(),
                                                                 .key = Protocol::Skills::NoSkillTargetKey}));
    }
    return SetTheHitAction(m_hero, CombatContextOf(m_context));
}

// 0x591859..0x5925ad: 0x26 and 0x27, only a player. The key of the weapon skill's target (0x7758588) through
// 0x558eb0, or 0xffff without it; the 1E with the cell of +0x3a8; then 0x4f5290 and +0x378 at 0.
Core::Expected<void, std::string> WizardBranch::CastAtTheWeaponTarget()
{
    if (!IsPlayer(m_hero))
        return {};
    std::uint16_t key = Protocol::Skills::NoSkillTargetKey;
    if (m_state.weaponSkill.target != -1)
    {
        const auto chosenKey =
            Combat::ChosenKeyOf(m_world, SlotOf(m_world, m_hero), m_state.weaponSkill.target, m_context.controlDown);
        if (!chosenKey.HasValue())
            return Core::Unexpected(chosenKey.Error());
        key = static_cast<std::uint16_t>(chosenKey.Value());
    }
    if (!HasTheSilentName(m_world))
    {
        m_context.world.Runtime().lastShotSkill = m_skill;
        const auto cell = AttackCellOf(m_hero, "0x591a6c");
        if (!cell.HasValue())
            return Core::Unexpected(cell.Error());
        const auto direction = DirectionOf(m_hero, "0x591c67");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheSkillPacket(m_context, Protocol::Skills::WriteDurationMagic({.skill = m_skill,
                                                                            .x = cell.Value().x,
                                                                            .y = cell.Value().y,
                                                                            .direction = direction.Value(),
                                                                            .key = key}));
    }
    if (auto hit = SetTheHitAction(m_hero, CombatContextOf(m_context)); !hit.HasValue())
        return hit;
    m_hero.movement = 0;
    return {};
}

// 0x5925ae..0x5925d3: the Magic Gladiator or no target, to 0x5808da; the Wizard, 6 and 0xf.
Core::Expected<void, std::string> WizardBranch::UseTheWizardOnly()
{
    if (m_class != Kernel::CharacterClass::DarkWizard || !m_aimed)
        return UseTheSharedTail(m_hero, m_skill, m_context);
    if (m_skill == 6)
        return Teleport();
    if (m_skill == 0xf)
        return TeleportTheAlly();
    return UseTheSharedTail(m_hero, m_skill, m_context);
}

// 0x593051..0x593873: Teleport (6) to the target's cell (0x77e1ba8, 0x77e1bac).
Core::Expected<void, std::string> WizardBranch::Teleport()
{
    if (m_runtime.cursorItemCount > 0 || (m_hero.flags78 & FrozenHeroFlag) == FrozenHeroFlag || CannotTeleport(m_hero))
        return {};
    // 0x5930be..0x5930f9: the TerrainWall of 0x5f82a0 (both with & 0xff) without 0x20 or 0x40.
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(m_context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x5930cc: falta TerrainWall para el Teleport"));
    const std::size_t index = ((m_state.targetY & 0xffu) << 8) + (m_state.targetX & 0xffu);
    std::uint32_t wall = terrain.attributes->cells[index];
    if ((wall & 0x20u) == 0x20u)
        wall -= 0x20u;
    if ((wall & 0x40u) == 0x40u)
        wall -= 0x40u;
    if (wall != 0)
        return {};
    m_hero.angles.z = static_cast<float>(
        Math::CreateAngle(m_hero.position.x, m_hero.position.y, m_hero.attackPosition.x, m_hero.attackPosition.y));
    // 0x59311d..0x59314b: byte 0x576794c, 0x77f3bb0 and 3000 ms (unsigned subtraction) since the last 1C.
    if (static_cast<std::uint8_t>(m_state.gateRequestBlocked) != 0 || m_state.warpPending)
        return {};
    if (!m_context.tickCount)
        return Core::Unexpected(std::string("0x59313a: la espera del Teleport sin GetTickCount (0x6b8144)"));
    if (m_context.tickCount() - m_state.lastTeleportMs < TeleportWaitMs)
        return {};
    m_state.gateRequestBlocked = 1;
    SendTheSkillPacket(m_context,
                       Protocol::Skills::WriteTeleportMagic(static_cast<std::uint8_t>(m_state.targetX & 0xffu),
                                                            static_cast<std::uint8_t>(m_state.targetY & 0xffu)));
    // 0x59383f..0x593857: with 0x1000 in +0x78, 0x4e6910; 0x593061 already left with it (*static*).
    return ActTheTeleport(m_hero, m_context);
}

// 0x5925d9..0x593050: Teleport Ally (0xf) of the chosen target to a free cell next to the hero.
Core::Expected<void, std::string> WizardBranch::TeleportTheAlly()
{
    if (m_runtime.selectedTargets[2] == -1)
        return {};
    const auto chosen = ChosenActor("0x59266d");
    if (!chosen.HasValue())
        return Core::Unexpected(chosen.Error());
    Kernel::ActorState& target = *chosen.Value();
    if (m_runtime.castleSiege.siegeActive)
    {
        // 0x5925f3..0x5926c8: the siege post (+0x360) of the published hero and of the hero, in the same pair.
        const std::uint8_t published = m_world.Hero().byte360;
        const std::uint8_t own = m_hero.byte360;
        const auto pairOf = [](std::uint8_t role) -> int
        {
            if (role == 9 || role == 6)
                return 1;
            if (role == 0xa || role == 7)
                return 2;
            if (role == 0xb || role == 8)
                return 3;
            if (role == 0xd || role == 0xc)
                return 4;
            return 0;
        };
        if (pairOf(published) != 0 && pairOf(published) != pairOf(own))
            return {};
    }
    else
    {
        // 0x5926ca..0x592766: the chosen target's name among the party's (0x77f3cd0, the strncmp of 0x59272c, like
        // 0x59e680 inline); outside it or with no party, nothing.
        if (!Party::IsInTheParty(m_world, target))
            return {};
    }
    // 0x592660..0x592799.
    if (m_runtime.cursorItemCount > 0 || CannotTeleport(target))
        return {};
    const auto heroX =
        Math::OriginalFtolLow(static_cast<double>(m_hero.position.x) * static_cast<double>(CellReciprocal));
    const auto heroY =
        Math::OriginalFtolLow(static_cast<double>(m_hero.position.y) * static_cast<double>(CellReciprocal));
    if (!heroX || !heroY)
        return Core::Unexpected(std::string("0x5927b2: la celda del héroe fuera del perfil ftol"));
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(m_context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x59280f: falta TerrainWall para el Teleport Ally"));
    // 0x5927cb..0x5928f1: one of the eight neighbouring cells (two rand() % 3, without the center) without TerrainWall
    // (except 0x20), or inside the castle in the siege; with ten blocked cells, nothing.
    std::int32_t tries = 0;
    std::int32_t cellX = 0;
    std::int32_t cellY = 0;
    for (;;)
    {
        const std::int32_t dx = m_context.random.Next() % 3;
        const std::int32_t dy = m_context.random.Next() % 3;
        if (dx == 1 && dy == 1)
        {
            if (tries > MaximumTries)
                return {};
            continue;
        }
        cellX = dx + *heroX - 1;
        cellY = dy + *heroY - 1;
        m_state.targetX = std::bit_cast<std::uint32_t>(cellX);
        m_state.targetY = std::bit_cast<std::uint32_t>(cellY);
        // 0x5f8290 without a mask: y · 256 + x.
        const std::int64_t index = static_cast<std::int64_t>(cellY) * 256 + cellX;
        if (index < 0 || index >= static_cast<std::int64_t>(terrain.attributes->cells.size()))
            return Core::Unexpected(
                std::format("0x592819: la celda ({}, {}) del Teleport Ally sale de TerrainWall", cellX, cellY));
        std::uint32_t wall = terrain.attributes->cells[static_cast<std::size_t>(index)];
        if ((wall & 0x20u) == 0x20u)
            wall -= 0x20u;
        if (m_runtime.map == CastleSiegeMap)
        {
            // 0x59283b..0x5928de: the published hero's X (0x412810/0x40b800) and Y (0x4e7e40).
            Kernel::ActorState& published = m_world.Hero();
            const std::uint8_t x = ReadTheSkillCell(published.cellXAccesses, published.cellX, m_context.random);
            const std::uint8_t y = ReadTheSkillCell(published.cellYAccesses, published.cellY, m_context.random);
            if (InRange(x, 0x96, 0xc8) && InRange(y, 0xb4, 0xe6))
                break;
        }
        if (wall == 0)
            break;
        ++tries;
        if (tries > MaximumTries)
            return {};
    }
    // 0x5928f6..0x592914: the chosen target's angle towards its own +0x3a8.
    target.angles.z = static_cast<float>(
        Math::CreateAngle(target.position.x, target.position.y, target.attackPosition.x, target.attackPosition.y));
    // 0x592917..0x592941: with 0x576794c, it lowers it and nothing.
    if (static_cast<std::uint8_t>(m_state.gateRequestBlocked) != 0)
    {
        m_state.gateRequestBlocked = 0;
        return {};
    }
    m_state.gateRequestBlocked = 1;
    SendTheSkillPacket(m_context,
                       Protocol::Skills::WriteTargetTeleport(target.key, static_cast<std::uint8_t>(cellX & 0xff),
                                                             static_cast<std::uint8_t>(cellY & 0xff)));
    return ActTheTeleport(target, m_context);
}
} // namespace

Core::Expected<void, std::string> UseTheWizardSkill(Kernel::ActorState& hero, std::uint8_t skill,
                                                    const HeroSkillContext& context)
{
    return WizardBranch(hero, skill, context).Use();
}
} // namespace Client::Skills
