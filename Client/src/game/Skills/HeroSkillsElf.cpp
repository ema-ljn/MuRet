#include "Skills/HeroSkillsElf.h"

#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Tables/SkillId.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Actors/WalkPath.h"
#include "Maps/MapId.h"
#include "Protocol/Skills/Skill.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Combat/ActorCombat.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovementState.h"
#include "Combat/Attackable.h"
#include "Skills/HeroSkillCasts.h"
#include "Skills/HeroSkillSupport.h"
#include "Skills/HeroSkills.h"
#include "Skills/HeroSkillsTail.h"
#include "Items/InventoryMoves.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill4e9920.h"
#include "Skills/Skill4e9c50.h"
#include "Skills/Skill4f5290.h"
#include "Skills/Skill551f40.h"
#include "Skills/Skill55faa0.h"
#include "Skills/Skill57ce80.h"
#include "Skills/Skill57cf90.h"
#include "Skills/SkillDelays.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
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
constexpr std::uint8_t ShotSkill = Tables::PenetrationSkill;
constexpr std::uint8_t Skill4c = 0x4c;
constexpr std::uint8_t PlayerObject = 1;
constexpr std::int32_t SummonForbiddenMap = 10;
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0.01
constexpr float DirectionScale = std::bit_cast<float>(std::uint32_t{0x3f360b61}); // [0x6ba440], 256/360
using Terrain::CellSize;                                                          // [0x6b86b4], 100
constexpr float HalfCell = std::bit_cast<float>(std::uint32_t{0x42480000});       // [0x6b86b0], 50
constexpr float WalkReach = std::bit_cast<float>(std::uint32_t{0x3f99999a});      // [0x6b8bb0], 1.2

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

bool IsPlayer(const Kernel::ActorState& actor)
{
    return actor.type == Tables::PlayerSkeletonModel;
}

// 0x57e162..0x57e1b1 and 0x57f9d1..0x57fa1c: the published hero's ranged weapon (the signed WORD CMPs): the right
// (+0x2b8) from 0xa1a to 0xa20, the left (+0x2d8) from 0xa12 to 0xa18, 0xa23, 0xa26 or 0xa27, or the right from 0xa22
// to 0xc11.
bool HoldsTheElfWeapon(const Kernel::ActorState& published)
{
    const auto right = static_cast<std::int16_t>(published.hands[0].model);
    const auto left = static_cast<std::int16_t>(published.hands[1].model);
    if (InRange(right, 0xa1a, 0xa20))
        return true;
    if (InRange(left, 0xa12, 0xa18) || left == 0xa23 || left == 0xa26 || left == 0xa27)
        return true;
    return InRange(right, 0xa22, 0xc11);
}

// 0x57e1c4..0x57e21e (the limit on the x87), 0x57f044..0x57f092 and 0x5809a2..0x5809e7 (the limit stored in a float,
// [ebp−0x34]): the distance from the hero to the center of cell 0x77e1ba8/0x77e1bac against the reach · 100. Less,
// equal or unordered (C0 or C3) is in reach.
bool InReach(const Kernel::ActorState& hero, const Movement::GameMovementState& state, bool storedLimit)
{
    const auto targetX = std::bit_cast<std::int32_t>(state.targetX);
    const auto targetY = std::bit_cast<std::int32_t>(state.targetY);
    const double dx = static_cast<double>(hero.position.x) -
                      (static_cast<double>(targetX) * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double dy = static_cast<double>(hero.position.y) -
                      (static_cast<double>(targetY) * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double distance = std::sqrt(dx * dx + dy * dy);
    double limit = static_cast<double>(state.selectedSkillRange) * static_cast<double>(CellSize);
    if (storedLimit)
        limit = static_cast<double>(static_cast<float>(limit));
    return !(distance > limit);
}

std::optional<std::uint8_t> LowByteOf(double value)
{
    const auto truncated = Math::OriginalFtolLow(value);
    if (!truncated)
        return std::nullopt;
    return static_cast<std::uint8_t>(*truncated & 0xff);
}

struct SkillCell
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// The hero's logical cell: Y (+0x420) before X (+0x41c), 4 + 4 rand().
SkillCell ReadTheHeroCell(Kernel::ActorState& hero, const HeroSkillContext& context)
{
    const std::uint8_t y = ReadTheSkillCell(hero.cellYAccesses, hero.cellY, context.random);
    const std::uint8_t x = ReadTheSkillCell(hero.cellXAccesses, hero.cellX, context.random);
    return {x, y};
}

class ElfBranch
{
public:
    ElfBranch(Kernel::ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
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
    Kernel::ActorState& Published() const
    {
        return m_world.Hero();
    }
    Core::Expected<Kernel::ActorState*, std::string> ActorAt(std::int32_t index, std::string_view where) const;
    Core::Expected<void, std::string> SendTheMagicAttack(std::string_view brake);
    Core::Expected<void, std::string> Summon();
    Core::Expected<void, std::string> Shoot();
    Core::Expected<void, std::string> UseByReach();
    Core::Expected<void, std::string> WalkToward(std::int32_t goalX, std::int32_t goalY, float distance, bool withType,
                                                 std::string_view where);
    Core::Expected<void, std::string> CastTheSupport();
    Core::Expected<void, std::string> CastThe35();
    Core::Expected<void, std::string> CastTheSkill4c();

    Kernel::ActorState& m_hero;
    std::uint8_t m_skill;
    const HeroSkillContext& m_context;
    Kernel::WorldState& m_world;
    Kernel::WorldRuntime& m_runtime;
    Movement::GameMovementState& m_state;
};

Core::Expected<Kernel::ActorState*, std::string> ElfBranch::ActorAt(std::int32_t index, std::string_view where) const
{
    if (index < 0 || static_cast<std::size_t>(index) >= m_world.Actors().size())
        return Core::Unexpected(std::format("{}: el personaje {} está fuera del pool", where, index));
    return &m_world.Actors()[static_cast<std::size_t>(index)];
}

// The inline C1 06 19 with the player key (0x57f2fe.., 0x57fadb.. and 0x58021d..): without "webzen" in the published
// hero and with the brake (which with 0x28 does not brake and records anyway).
Core::Expected<void, std::string> ElfBranch::SendTheMagicAttack(std::string_view brake)
{
    if (HasTheSilentName(m_world))
        return {};
    const auto passes = PassesTheSkillBrake(m_skill, m_context, brake);
    if (!passes.HasValue())
        return Core::Unexpected(passes.Error());
    if (passes.Value())
        SendTheSkillPacket(m_context, Protocol::Skills::WriteMagicAttack(m_skill, m_context.playerKey));
    return {};
}

// 0x57d790..0x57e12a: true if it continues to 0x57ce80, with the own record already closed.
Core::Expected<bool, std::string> ElfBranch::PassesTheHead()
{
    const Models::LogicalAccess own(m_runtime.ownRecordDepth, m_context.random, Models::OwnRecordCloseRandoms);
    const auto cost = SkillCostOf(m_skill, m_context, "0x57d803");
    if (!cost.HasValue())
        return Core::Unexpected(cost.Error());
    const Kernel::OwnCharacterRecord& record = m_world.OwnCharacter();
    // 0x57d808..0x57d854: signed JLE against +0x24; without the mana, the potion and nothing.
    if (static_cast<std::int32_t>(cost.Value().mana) > static_cast<std::int32_t>(record.mana))
    {
        if (auto potion = UseAManaPotion(m_context, "0x57d837"); !potion.HasValue())
            return Core::Unexpected(potion.Error());
        return false;
    }
    // 0x57df9c..0x57dfa7.
    if (static_cast<std::int32_t>(cost.Value().stamina) > static_cast<std::int32_t>(record.bp))
        return false;
    // 0x57dfff..0x57e018: the delay of the published hero's slot skill.
    const SkillDelayContext delays{.world = m_world, .random = m_context.random, .skills = m_context.skills};
    const auto ready = TryStartSkillDelay(Published().skillIndex, delays);
    if (!ready.HasValue())
        return Core::Unexpected(ready.Error());
    if (!ready.Value())
        return false;
    // 0x57e070..0x57e095: signed JLE against +0x30 + +0x1e.
    const auto energy = SkillEnergyRequirement(m_skill, delays);
    if (!energy.HasValue())
        return Core::Unexpected(energy.Error());
    return energy.Value() <= static_cast<std::int32_t>(record.word30) + static_cast<std::int32_t>(record.energy);
}

Core::Expected<void, std::string> ElfBranch::Use()
{
    const auto head = PassesTheHead();
    if (!head.HasValue())
        return Core::Unexpected(head.Error());
    if (!head.Value())
        return {};
    // 0x57e12a..0x57e130: nobody checks the result.
    if (auto aimed = AimTheHeroSkill(m_hero, m_context); !aimed.HasValue())
        return Core::Unexpected(aimed.Error());
    // 0x57e130..0x57e150.
    if (InRange(m_skill, 0x1e, 0x24))
        return Summon();
    if (m_skill == ShotSkill)
    {
        if (auto shot = Shoot(); !shot.HasValue())
            return shot;
    }
    return UseByReach();
}

// 0x57f252..0x57f9af: the summons. On map 10 or 18 to 23 (0x6c9dd0), nothing.
Core::Expected<void, std::string> ElfBranch::Summon()
{
    const std::int32_t map = m_runtime.map;
    if (map == SummonForbiddenMap || InRange(map, FirstChaosCastleMap, LastChaosCastleMap))
        return {};
    if (auto sent = SendTheMagicAttack("0x57f2df"); !sent.HasValue())
        return sent;
    // 0x57f993..0x57f997.
    return SetTheHitAction(m_hero, CombatContextOf(m_context));
}

// 0x57e14d..0x57f005: skill 0x34. Any "no" continues at 0x57f008, without 0x4f4a10.
Core::Expected<void, std::string> ElfBranch::Shoot()
{
    if (!IsPlayer(m_hero) || !HoldsTheElfWeapon(Published()))
        return {};
    static const Items::InventoryServices NoServices{};
    const auto arrows = Items::CheckTheArrows(
        Items::InventoryMoveContext{.world = m_world,
                                    .random = m_context.random,
                                    .services = m_context.inventory != nullptr ? *m_context.inventory : NoServices});
    if (!arrows.HasValue())
        return Core::Unexpected(arrows.Error());
    if (!arrows.Value() || !InReach(m_hero, m_state, false))
        return {};
    // 0x57e224..0x57e25d: the heading towards +0x3a8 and the key of 0x558eb0 with 0x7758588, or 0xffff without it.
    m_hero.angles.z = static_cast<float>(
        Math::CreateAngle(m_hero.position.x, m_hero.position.y, m_hero.attackPosition.x, m_hero.attackPosition.y));
    std::uint16_t key = Protocol::Skills::NoSkillTargetKey;
    if (m_state.weaponSkill.target != -1)
    {
        const auto chosen =
            Combat::ChosenKeyOf(m_world, SlotOf(m_world, m_hero), m_state.weaponSkill.target, m_context.controlDown);
        if (!chosen.HasValue())
            return Core::Unexpected(chosen.Error());
        key = static_cast<std::uint16_t>(static_cast<std::uint32_t>(chosen.Value()) & 0xffffu);
    }
    if (!HasTheSilentName(m_world))
    {
        // 0x57e2c8..0x57ef4e: 0x5767948 and the 1E with the logical cell (Y before X).
        m_context.world.Runtime().lastShotSkill = ShotSkill;
        const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
        const auto direction = LowByteOf(static_cast<double>(m_hero.angles.z) * static_cast<double>(DirectionScale));
        if (!direction)
            return Core::Unexpected(std::string("0x57e668: el rumbo del 1E fuera del perfil ftol"));
        SendTheSkillPacket(m_context,
                           Protocol::Skills::WriteDurationMagic(
                               {.skill = ShotSkill, .x = cell.x, .y = cell.y, .direction = *direction, .key = key}));
    }
    // 0x57ef5f..0x57ef71: 0x4f4a10. The arrow of 0x53e2b0 (0x57ef77..0x57f002) belongs to a non-player, which 0x57e156
    // already left out.
    return Combat::SetActorAttack(m_hero, CombatContextOf(m_context));
}

// 0x4eac00(X, Y, x, y, +0x3e8, distance) from the hero's logical cell (Y and X, 8 rand()). With a path, +0x378 at 1
// and, with `withType`, +0x379 at 5. Without a path, nothing. With the goal outside 0..255, which PathFinding2 takes as
// a linear index, MuRet drops the path after the reads, as with no path (08, P-C256).
Core::Expected<void, std::string> ElfBranch::WalkToward(std::int32_t goalX, std::int32_t goalY, float distance,
                                                        bool withType, std::string_view where)
{
    const SkillCell start = ReadTheHeroCell(m_hero, m_context);
    constexpr std::int32_t LastCell = 0xff;
    if (!InRange(goalX, 0, LastCell) || !InRange(goalY, 0, LastCell))
        return {};
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(m_context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::format("{}: falta TerrainWall para el camino de la skill", where));
    // 0x4eac0a..0x4eac41: the Crywolf altar flag, which does not count with a nonzero distance (0x4eadd4).
    const auto path =
        Actors::FindThePath(m_world, terrain.attributes->cells, {start.x, start.y},
                            {static_cast<std::uint8_t>(goalX), static_cast<std::uint8_t>(goalY)}, distance);
    if (!path)
        return {};
    m_hero.path = *path;
    m_hero.movement = 1;
    if (withType)
        m_hero.movementType = 5;
    return {};
}

// 0x57f008..0x57fa3f: the others by reach.
Core::Expected<void, std::string> ElfBranch::UseByReach()
{
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    // 0x57f008..0x57f03e.
    if (chosen != -1)
        m_state.weaponSkill =
            Movement::WeaponSkillRequest{.active = 1, .slot = Published().skillIndex, .target = chosen};
    if (!InReach(m_hero, m_state, true))
    {
        // 0x57f098..0x57f15e: the path to a chosen player (+0x88 at 1), with 0x1a to 0x1c or attackable.
        if (chosen != -1)
        {
            bool walks = InRange(m_skill, 0x1a, 0x1c);
            if (!walks)
            {
                const auto attackable = Attackable();
                if (!attackable.HasValue())
                    return Core::Unexpected(attackable.Error());
                walks = attackable.Value();
            }
            if (walks)
            {
                const auto target = ActorAt(chosen, "0x57f0d5");
                if (!target.HasValue())
                    return Core::Unexpected(target.Error());
                if (target.Value()->byte88 == PlayerObject)
                {
                    if (auto walked = WalkToward(std::bit_cast<std::int32_t>(m_state.targetX),
                                                 std::bit_cast<std::int32_t>(m_state.targetY),
                                                 m_state.selectedSkillRange, true, "0x57f148");
                        !walked.HasValue())
                        return walked;
                }
            }
        }
        // 0x57f165..0x57f184.
        if (!InRange(m_skill, 0x43, 0x47) && m_skill != Skill4c)
            return {};
    }
    // 0x57f18a..0x57f1ef: the line of sight from the hero's cell (Y and X). MuRet takes the line that leaves
    // TerrainWall (0x55886d reads past the 65536 WORDs) as no line (08, P-C256).
    const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(m_context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x558800: falta TerrainWall para ver el blanco de la skill"));
    const bool seen = Pathfinding::CanSeeTheCell(terrain.attributes->cells, cell.x, cell.y,
                                                 std::bit_cast<std::int32_t>(m_state.targetX),
                                                 std::bit_cast<std::int32_t>(m_state.targetY))
                          .value_or(false);
    if (seen && m_runtime.selectedTargets[2] != -1)
    {
        const auto target = ActorAt(m_runtime.selectedTargets[2], "0x57f211");
        if (!target.HasValue())
            return Core::Unexpected(target.Error());
        // 0x57f203..0x57f251: 0x1a to 0x1c on a player, 0x55faa0 and nothing more.
        if (target.Value()->byte88 == PlayerObject && InRange(m_skill, 0x1a, 0x1c))
            return UseTheElfSkillOnItsTarget(m_hero, m_hero, m_context);
        // 0x57f9b0..0x57fa25: a player's 0x33 and 0x2e with the weapon, on an attackable target, and it continues.
        const auto attackable = Attackable();
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        if (attackable.Value() && (m_skill == 0x33 || m_skill == 0x2e) && IsPlayer(m_hero) &&
            HoldsTheElfWeapon(Published()))
        {
            if (auto reached = UseTheElfSkillOnItsTarget(m_hero, m_hero, m_context); !reached.HasValue())
                return reached;
        }
    }
    // 0x57fa28..0x57fa3f: the skill − 0x1a through tables 0x599dac and 0x599d9c.
    switch (m_skill)
    {
    case 0x1a:
    case 0x1b:
    case 0x1c:
        return CastTheSupport();
    case 0x35:
        return CastThe35();
    case Skill4c:
        return CastTheSkill4c();
    default:
        return UseTheSharedTail(m_hero, m_skill, m_context);
    }
}

// 0x57fa46..0x580187: 0x1a to 0x1c, of any type: the 19 with the player key and 0x4f5290, and nothing more.
Core::Expected<void, std::string> ElfBranch::CastTheSupport()
{
    if (auto sent = SendTheMagicAttack("0x57fabc"); !sent.HasValue())
        return sent;
    return SetTheHitAction(m_hero, CombatContextOf(m_context));
}

// 0x580188..0x5808d3: skill 0x35, like 0x1a to 0x1c, with +0x378 at 0; continues at 0x5808da.
Core::Expected<void, std::string> ElfBranch::CastThe35()
{
    if (auto sent = SendTheMagicAttack("0x5801f6"); !sent.HasValue())
        return sent;
    if (auto hit = SetTheHitAction(m_hero, CombatContextOf(m_context)); !hit.HasValue())
        return hit;
    m_hero.movement = 0;
    return UseTheSharedTail(m_hero, m_skill, m_context);
}

// 0x58097a..0x581877: skill 0x4c. The cell of +0x3a8 (ftol · 0.01) is the path's goal; in reach (the float limit of
// 0x57f087) and with the chosen target attackable, the relative position (0x4e9920), with no chosen target +0x434 at
// −1; otherwise the angle, the delay (0x552990), the key (0x558eb0), +0x434 and the 1E with the magic key; then +0x378
// at 0 and, on a player, 0x4e9c50. Otherwise, with no chosen target 0x6c8e38 at −1; with one, the path up to the reach
// · 1.2 (without +0x379). Everything continues at 0x5808da.
Core::Expected<void, std::string> ElfBranch::CastTheSkill4c()
{
    const auto aimX =
        Math::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.x) * static_cast<double>(CellReciprocal));
    const auto aimY =
        Math::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.y) * static_cast<double>(CellReciprocal));
    if (!aimX || !aimY)
        return Core::Unexpected(std::string("0x580989: la celda del blanco de la 0x4c fuera del perfil ftol"));
    bool casts = InReach(m_hero, m_state, true);
    if (casts)
    {
        const auto attackable = Attackable();
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        casts = attackable.Value();
    }
    if (!casts)
    {
        // 0x5817e6..0x581877.
        if (m_runtime.selectedTargets[2] == -1)
        {
            m_runtime.selectedTargets[4] = -1;
            return UseTheSharedTail(m_hero, m_skill, m_context);
        }
        // 0x58182f..0x581848: FMUL and FSTP to a float on the stack.
        const auto reach =
            static_cast<float>(static_cast<double>(m_state.selectedSkillRange) * static_cast<double>(WalkReach));
        if (auto walked = WalkToward(*aimX, *aimY, reach, false, "0x581853"); !walked.HasValue())
            return walked;
        return UseTheSharedTail(m_hero, m_skill, m_context);
    }
    // 0x5809fa..0x580a1a: the ftol of +0x3ac and +0x3a8, unscaled.
    const auto wholeY = Math::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.y));
    const auto wholeX = Math::OriginalFtolLow(static_cast<double>(m_hero.attackPosition.x));
    if (!wholeX || !wholeY)
        return Core::Unexpected(std::string("0x580a00: la posición del blanco de la 0x4c fuera del perfil ftol"));
    const auto relative = RelativeTargetCell(m_hero.position.x, m_hero.position.y, *wholeX, *wholeY);
    if (!relative)
        return Core::Unexpected(std::string("0x580a1a: un ftol de la celda relativa de la 0x4c fuera de su perfil"));
    const std::int32_t chosen = m_runtime.selectedTargets[2];
    if (chosen == -1)
    {
        // 0x5817db.
        m_hero.skillTarget4c = -1;
        return UseTheSharedTail(m_hero, m_skill, m_context);
    }
    // 0x580a36..0x580a7d.
    m_hero.angles.z = static_cast<float>(
        Math::CreateAngle(m_hero.position.x, m_hero.position.y, m_hero.attackPosition.x, m_hero.attackPosition.y));
    if (auto delayed = StartSkillDelay(
            Published().skillIndex,
            SkillDelayContext{.world = m_world, .random = m_context.random, .skills = m_context.skills});
        !delayed.HasValue())
        return delayed;
    const auto key = Combat::ChosenKeyOf(m_world, SlotOf(m_world, m_hero), chosen, m_context.controlDown);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    m_hero.skillTarget4c = chosen;
    if (!HasTheSilentName(m_world))
    {
        // 0x580af3..0x5817a5: 0x5767948, the logical cell (Y and X), the heading, the relative cell in byte 8 and the
        // magic key (0x57cf90 inline, with +0x1bd).
        m_context.world.Runtime().lastShotSkill = m_skill;
        const SkillCell cell = ReadTheHeroCell(m_hero, m_context);
        const auto direction = LowByteOf(static_cast<double>(m_hero.angles.z) * static_cast<double>(DirectionScale));
        if (!direction)
            return Core::Unexpected(std::string("0x580e90: el rumbo del 1E fuera del perfil ftol"));
        const std::uint8_t magicKey = NextMagicKey(m_state, m_hero.magicKey);
        SendTheSkillPacket(m_context,
                           Protocol::Skills::WriteDurationMagic(
                               {.skill = m_skill,
                                .x = cell.x,
                                .y = cell.y,
                                .direction = *direction,
                                .back = *relative,
                                .key = static_cast<std::uint16_t>(static_cast<std::uint32_t>(key.Value()) & 0xffffu),
                                .magicKey = magicKey}));
    }
    // 0x5817b6..0x5817d6.
    m_hero.movement = 0;
    if (IsPlayer(m_hero))
    {
        if (auto acted = SetTheActorAction(m_hero, ActionByTheHands(m_hero), CombatContextOf(m_context));
            !acted.HasValue())
            return acted;
    }
    return UseTheSharedTail(m_hero, m_skill, m_context);
}
} // namespace

Core::Expected<void, std::string> UseTheElfSkill(Kernel::ActorState& hero, std::uint8_t skill,
                                                 const HeroSkillContext& context)
{
    return ElfBranch(hero, skill, context).Use();
}
} // namespace Client::Skills
