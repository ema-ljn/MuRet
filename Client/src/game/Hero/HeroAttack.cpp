#include "Hero/HeroAttack.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Tables/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Kernel/CharacterClass.h"
#include "Pathfinding/PathCell.h"
#include "Actors/WalkPath.h"
#include "Protocol/Combat/Combat.h"
#include "Protocol/Movement/Movement.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Combat/ActorCombat.h"
#include "Combat/Attackable.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovementState.h"
#include "Skills/HeroSkillCasts.h"
#include "Skills/HeroSkills.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill55cbc0.h"
#include "Skills/Skill55faa0.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainDefines.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace Client::Hero
{
namespace
{
constexpr std::uint8_t ArrivalAttack = 3;
constexpr std::uint8_t ArrivalSkill = 5;
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint32_t LastSkill = 0xff;
constexpr std::int32_t LastCell = 0xff;
using Terrain::CellSize;                                                          // [0x6b86b4], 100
constexpr float HalfCell = std::bit_cast<float>(std::uint32_t{0x42480000});       // [0x6b86b0], 50
constexpr float CastReach = std::bit_cast<float>(std::uint32_t{0x42f00001});      // [0x6ba448], 120.0000076
constexpr float WalkReach = std::bit_cast<float>(std::uint32_t{0x3f99999a});      // [0x6b8bb0], 1.2
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0.01
// 0x6c8ff0: with this name the hero does not send the D7.
constexpr std::string_view NoAttackName = "webzen";

// 0x5650ef..0x565100: table 0x56ab20 by +0x379 − 1.
std::string_view ArrivalCaseOf(std::int32_t type)
{
    switch (type)
    {
    case 1:
        return "0x566755: levantar el ítem del piso al llegar (+0x379 en 1) es del hito 5";
    case 2:
        return "0x5674c8: hablar con el NPC al llegar (+0x379 en 2) es del hito 2";
    case 4:
        return "0x5695b4: usar el objeto del mapa al llegar (+0x379 en 4) es de otro hito de la CL9";
    default:
        return {};
    }
}

// 0x565107..0x56526a: the range, in cells, by the weapons (the own record's types, +0xb6c the right one and +0xbb4 the
// left one) and +0x376.
float AttackRange(const Kernel::ActorState& hero, const HeroArrivalContext& context)
{
    Kernel::WorldState& world = context.combat.world;
    std::int32_t right = 0;
    std::int32_t left = 0;
    {
        const Models::LogicalAccess own(world.Runtime().ownRecordDepth, context.combat.random,
                                        Models::OwnRecordCloseRandoms);
        left = std::bit_cast<std::int16_t>(world.OwnCharacter().equipment[1].id);
        right = std::bit_cast<std::int16_t>(world.OwnCharacter().equipment[0].id);
    }
    float range = 1.8f;
    if (right >= 0x600 && right < 0x800)
        range = 2.2f;
    if ((right >= 0x808 && right < 0x80f) || right == 0x810 || (right >= 0x812 && right < 0xa00))
        range = 6.0f;
    if ((left >= 0x800 && left < 0x807) || left == 0x811 || left == 0x814 || left == 0x815)
        range = 6.0f;
    if (hero.word376 == 9)
        range = 7.0f;
    return range;
}

std::uint8_t ReadTheCell(std::uint8_t& depth, std::uint8_t cell, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    return cell;
}

Core::Expected<std::uint8_t, std::string> DirectionOf(float yaw, std::string_view where)
{
    const auto direction = Protocol::Movement::DirectionFromAngle(yaw);
    if (!direction.HasValue())
        return Core::Unexpected(std::format("{}: el rumbo del héroe fuera del perfil ftol", where));
    return direction.Value();
}

// 0x5653bb..0x56601e: in range.
Core::Expected<void, std::string> HitTheTarget(Kernel::ActorState& hero, std::int32_t target,
                                               const HeroArrivalContext& context)
{
    Kernel::WorldState& world = context.combat.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = context.combat.random;
    runtime.mouse.updateTime = runtime.mouse.updateTimeMax;
    if (auto attacked = Combat::SetActorAttack(hero, context.combat); !attacked.HasValue())
        return attacked;
    hero.byte381 = 1;
    hero.attackPosition = world.Actors()[static_cast<std::size_t>(target)].position;
    hero.angles.z = static_cast<float>(
        Math::CreateAngle(hero.position.x, hero.position.y, hero.attackPosition.x, hero.attackPosition.y));
    // 0x56544c..0x56548b: the published hero's cell, X with 0x40b570/0x40b800 and Y with 0x4e7e40.
    Kernel::ActorState& published = world.Hero();
    const std::uint8_t x = ReadTheCell(published.cellXAccesses, published.cellX, random);
    const std::uint8_t y = ReadTheCell(published.cellYAccesses, published.cellY, random);
    // 0x56548b..0x565954: C1 06 D3 X Y (heading << 4), the one-step turn, through the socket.
    if (const auto turn = DirectionOf(published.angles.z, "0x565700"); !turn.HasValue())
        return Core::Unexpected(turn.Error());
    if (context.sendWalk)
    {
        const std::array<Protocol::Movement::Cell, 1> cell{{{x, y}}};
        context.sendWalk(cell, cell[0], published.angles.z);
    }
    hero.movement = 0;
    // 0x565954..0x56599f: opens and closes the cell's X and Y.
    static_cast<void>(ReadTheCell(hero.cellXAccesses, hero.cellX, random));
    static_cast<void>(ReadTheCell(hero.cellYAccesses, hero.cellY, random));
    hero.attackTarget = static_cast<std::uint16_t>(target);
    const auto direction = DirectionOf(published.angles.z, "0x5659cd");
    if (!direction.HasValue())
        return Core::Unexpected(direction.Error());
    // 0x5659e6..0x565a47: with "webzen" in the published hero's name, it does not hit.
    if (Combat::CNameOf(published).find(NoAttackName) != std::string::npos)
        return {};
    if (!context.sendAttack)
        return {};
    context.sendAttack(world.Actors()[static_cast<std::size_t>(target)].key, Protocol::Combat::AttackAction,
                       direction.Value());
    return {};
}

// 0x56535b..0x566299: the Y (+0x420) and the X (+0x41c), each with 0x40b570 and 0x40b800, and PathFinding2(X, Y,
// target, +0x3e8, 0) with the distance range (0x4eac00, where the altar flag does not count): the goal is the cells
// within range of the target. With a path, +0x378 to 1 and nothing else: without SendMove, the server does not see the
// path until the D3 of turning on arrival (07). Without a path, nothing.
Core::Expected<void, std::string> WalkIntoRange(Kernel::ActorState& hero, std::int32_t targetX, std::int32_t targetY,
                                                float range, const HeroArrivalContext& context)
{
    auto& random = context.combat.random;
    const std::uint8_t y = ReadTheCell(hero.cellYAccesses, hero.cellY, random);
    const std::uint8_t x = ReadTheCell(hero.cellXAccesses, hero.cellX, random);
    // The target's cell off the grid, which the A* takes as a linear index (the next row, or outside the 65536 WORDs):
    // MuRet discards the approach, with the two rand() of the reads (07, Q-C1179).
    if (targetX < 0 || targetX > LastCell || targetY < 0 || targetY > LastCell)
        return {};
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.combat.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x566272: falta TerrainWall para el camino hasta el alcance"));
    const Pathfinding::PathCell target{static_cast<std::uint8_t>(targetX), static_cast<std::uint8_t>(targetY)};
    // 0x566272: PathFinding2 first computes the Crywolf altar flag (0x4eac0a..0x4eac41), which does not count with a
    // nonzero range (0x4eadd4).
    const auto path = Actors::FindThePath(context.combat.world, terrain.attributes->cells, {x, y}, target, range);
    if (!path)
        return {};
    hero.path = *path;
    hero.movement = 1;
    return {};
}

// 0x56610d..0x566127: the skill − 1 through tables 0x56ab4c and 0x56ab34.
enum class ArrivalSkillKind : std::uint8_t
{
    None,
    OnItsTarget,    // 0x56629a: 0x55cbc0
    AtTheTarget,    // 0x566143: 0x55b6f0
    ElfOnItsTarget, // 0x56642c and 0x5665c3: 0x55faa0
    OnUniria,       // 0x56612e: 0x2f, with the published hero on Uniria, as 0x566143
};

ArrivalSkillKind ArrivalSkillKindOf(std::uint32_t skill)
{
    switch (skill)
    {
    case 0x1:
    case 0x2:
    case 0x3:
    case 0x4:
    case 0x7:
    case 0xb:
    case 0x11:
    case 0x2d:
    case 0x49:
        return ArrivalSkillKind::OnItsTarget;
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x2b:
    case 0x2c:
    case 0x31:
    case 0x39:
    case 0x3c:
    case 0x3d:
    case 0x4a:
        return ArrivalSkillKind::AtTheTarget;
    case 0x1a:
    case 0x1b:
    case 0x1c:
    case 0x2e:
    case 0x33:
        return ArrivalSkillKind::ElfOnItsTarget;
    case 0x2f:
        return ArrivalSkillKind::OnUniria;
    default:
        return ArrivalSkillKind::None;
    }
}

Skills::HeroSkillContext SkillContextOf(const HeroArrivalContext& context)
{
    return Skills::HeroSkillContext{.world = context.combat.world,
                                    .random = context.combat.random,
                                    .skills = context.skills,
                                    .playerSkeleton = context.combat.playerSkeleton,
                                    .items = context.combat.items,
                                    .models = context.combat.models,
                                    .tickCount = context.tickCount,
                                    .inventory = context.inventory,
                                    .controlDown = context.controlDown,
                                    .send = context.sendSkill,
                                    .terrain = context.combat.terrain,
                                    .dynamicLighting = context.combat.dynamicLighting,
                                    .faunaModels = context.combat.faunaModels};
}

// 0x56601f..0x566754: case 5.
Core::Expected<void, std::string> UseTheSkillOnArrival(Kernel::ActorState& hero, const HeroArrivalContext& context)
{
    Kernel::WorldState& world = context.combat.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = context.combat.random;
    const Movement::WeaponSkillRequest request = runtime.movement.weaponSkill;
    // 0x56601f..0x566106: with the byte at 0x7758580, the 0x7758584 slot of the own record; otherwise 0x7758584 whole.
    // The record is opened and closed either way.
    std::uint32_t skill = 0;
    {
        const Models::LogicalAccess own(runtime.ownRecordDepth, random, Models::OwnRecordCloseRandoms);
        skill = (request.active & 0xffu) != 0 ? Kernel::OwnSkillByte(world.OwnCharacter(), request.slot) : request.slot;
    }
    if (skill > LastSkill)
        return Core::Unexpected(std::format("0x566108: la skill {:#x} de 0x7758584 está fuera de Skill.bmd", skill));
    const Skills::HeroSkillContext skills = SkillContextOf(context);
    const auto range = Skills::SkillRangeOf(static_cast<std::uint8_t>(skill), hero, skills);
    if (!range.HasValue())
        return Core::Unexpected(range.Error());
    ArrivalSkillKind kind = ArrivalSkillKindOf(skill);
    if (kind == ArrivalSkillKind::OnUniria)
    {
        if (world.Hero().pet.model != UniriaModel)
            return {};
        kind = ArrivalSkillKind::AtTheTarget;
    }
    if (kind == ArrivalSkillKind::None)
        return {};
    // 0x566143..0x56616d (and its pairs): the +0x389 of target 0x7758588 and the −1. The original reads +0x389 before
    // checking the −1 (Q-C1216), but with −1 or less it exits through either.
    const std::int32_t target = request.target;
    if (target <= -1)
        return {};
    if (static_cast<std::size_t>(target) >= world.Actors().size())
        return Core::Unexpected(
            std::format("0x56615b: el blanco {} de la skill pendiente está fuera del pool", target));
    const Kernel::ActorState& victim = world.Actors()[static_cast<std::size_t>(target)];
    if (victim.dying != 0)
        return {};
    // 0x566173..0x566194: the target's cell, ftol(· 0.01), at 0x77e1ba8 and 0x77e1bac.
    const auto cellX =
        Math::OriginalFtolLow(static_cast<double>(victim.position.x) * static_cast<double>(CellReciprocal));
    const auto cellY =
        Math::OriginalFtolLow(static_cast<double>(victim.position.y) * static_cast<double>(CellReciprocal));
    if (!cellX || !cellY)
        return Core::Unexpected(std::string("0x56617c: la celda del blanco de la skill fuera del perfil ftol"));
    runtime.movement.targetX = std::bit_cast<std::uint32_t>(*cellX);
    runtime.movement.targetY = std::bit_cast<std::uint32_t>(*cellY);
    // 0x566194..0x5661f0: the distance to the cell's center against the range · 120.0000076 (0x55b6f0's) or · 100, on
    // the x87. TEST AH, 1: with C0 (less or unordered) it is out; equal is in range (Q-C1216).
    const bool castsAtTheTarget = kind == ArrivalSkillKind::AtTheTarget;
    const double dx = static_cast<double>(hero.position.x) -
                      (static_cast<double>(*cellX) * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double dy = static_cast<double>(hero.position.y) -
                      (static_cast<double>(*cellY) * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double limit =
        static_cast<double>(range.Value()) * static_cast<double>(castsAtTheTarget ? CastReach : CellSize);
    if (distance <= limit && hero.safeZone == 0)
    {
        if (castsAtTheTarget)
            return Skills::CastTheSkillAtTheTarget(hero, hero, skills);
        // 0x56634c..0x5663a7: the line of sight from the hero's logical cell (the Y and the X).
        const std::uint8_t y = ReadTheCell(hero.cellYAccesses, hero.cellY, random);
        const std::uint8_t x = ReadTheCell(hero.cellXAccesses, hero.cellX, random);
        const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.combat.terrain);
        if (!terrain.attributes)
            return Core::Unexpected(
                std::string("0x558800: falta TerrainWall para ver el blanco de la skill pendiente"));
        // 0x56639d: MuRet takes a line that leaves TerrainWall (0x55886d reads outside the 65536 WORDs) as no line (08,
        // P-C256).
        const auto seen = Pathfinding::CanSeeTheCell(terrain.attributes->cells, x, y, *cellX, *cellY);
        if (!seen || !*seen)
            return {};
        if (kind == ArrivalSkillKind::OnItsTarget)
            return Skills::UseTheSkillOnItsTarget(hero, hero, skills);
        return Skills::UseTheElfSkillOnItsTarget(hero, hero, skills);
    }
    // 0x56620d..0x566299: the path to the range (· 1.2 in a float, with 0x55b6f0) from the hero's logical cell.
    const float reach = castsAtTheTarget
                            ? static_cast<float>(static_cast<double>(range.Value()) * static_cast<double>(WalkReach))
                            : range.Value();
    return WalkIntoRange(hero, *cellX, *cellY, reach, context);
}

Core::Expected<void, std::string> AttackOnArrival(Kernel::ActorState& hero, const HeroArrivalContext& context)
{
    Kernel::WorldState& world = context.combat.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    const float range = AttackRange(hero, context);
    const std::int32_t target = runtime.movement.attackTarget;
    if (target == -1)
        return {};
    // 0x565278..0x5652a6: the second argument (the hero) already hitting does not hit again; from 0x22 to 0x97, only
    // with 0x8d (Q-C1162).
    const std::uint8_t action = hero.animation.action;
    if (hero.type == Tables::PlayerSkeletonModel)
    {
        if (action >= 0x22 && action <= 0x97 && action == 0x8d)
            return {};
    }
    else if (action >= 3 && action <= 4)
    {
        return {};
    }
    if (target <= -1)
        return {};
    if (static_cast<std::size_t>(target) >= world.Actors().size())
        return Core::Unexpected(std::format("0x5652b5: el blanco {} está fuera del pool", target));
    const Kernel::ActorState& victim = world.Actors()[static_cast<std::size_t>(target)];
    // 0x5652b5..0x5652e8: the target's cell, ftol(x · 0.01f) as an integer, at 0x77e1ba8 and 0x77e1bac.
    constexpr double CellScale = static_cast<double>(0.01f);
    const auto cellX = Math::OriginalFtolLow(static_cast<double>(victim.position.x) * CellScale);
    const auto cellY = Math::OriginalFtolLow(static_cast<double>(victim.position.y) * CellScale);
    if (!cellX || !cellY)
        return Core::Unexpected(std::string("0x5652d0: la celda del blanco fuera del perfil ftol"));
    runtime.movement.targetX = std::bit_cast<std::uint32_t>(*cellX);
    runtime.movement.targetY = std::bit_cast<std::uint32_t>(*cellY);
    if (hero.safeZone != 0)
        return {};
    // 0x5652fb..0x565347: the distance to the cell's center, on the x87, against the range · 100. Less, equal or
    // unordered (C0 or C3) is in range.
    const double dx = static_cast<double>(hero.position.x) - (static_cast<double>(*cellX) * 100.0 + 50.0);
    const double dy = static_cast<double>(hero.position.y) - (static_cast<double>(*cellY) * 100.0 + 50.0);
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double limit = static_cast<double>(range) * 100.0;
    if (distance > limit)
    {
        // 0x565349..0x5653b6: out of range only the elf approaches, with the path to the range (0x566271).
        if (Kernel::BaseClassOf(hero.characterClass) != Kernel::CharacterClass::Elf)
            return {};
        return WalkIntoRange(hero, *cellX, *cellY, range, context);
    }
    return HitTheTarget(hero, target, context);
}

} // namespace

Core::Expected<void, std::string> InteractOnArrival(Kernel::ActorState& hero, const HeroArrivalContext& context)
{
    const std::int32_t type = static_cast<std::int8_t>(hero.movementType);
    if (type == ArrivalAttack)
        return AttackOnArrival(hero, context);
    if (type == ArrivalSkill)
        return UseTheSkillOnArrival(hero, context);
    const std::string_view other = ArrivalCaseOf(type);
    if (other.empty())
        return {};
    return Core::Unexpected(std::string(other));
}
} // namespace Client::Hero
