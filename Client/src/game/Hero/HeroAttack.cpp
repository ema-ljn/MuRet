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
constexpr float CastReach = std::bit_cast<float>(std::uint32_t{0x42f00001});      // [0x6ba448], 120,0000076
constexpr float WalkReach = std::bit_cast<float>(std::uint32_t{0x3f99999a});      // [0x6b8bb0], 1,2
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a}); // [0x6b86c0], 0,01
// 0x6c8ff0: el nombre con el que el héroe no manda el D7.
constexpr std::string_view NoAttackName = "webzen";

// 0x5650ef..0x565100: la tabla 0x56ab20 por +0x379 − 1.
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

// 0x565107..0x56526a: el alcance, en celdas, por las armas (los tipos del registro propio, +0xb6c la derecha y
// +0xbb4 la izquierda) y +0x376.
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

// 0x5653bb..0x56601e: en alcance.
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
    // 0x56544c..0x56548b: la celda del héroe publicado, X con 0x40b570/0x40b800 e Y con 0x4e7e40.
    Kernel::ActorState& published = world.Hero();
    const std::uint8_t x = ReadTheCell(published.cellXAccesses, published.cellX, random);
    const std::uint8_t y = ReadTheCell(published.cellYAccesses, published.cellY, random);
    // 0x56548b..0x565954: C1 06 D3 X Y (rumbo << 4), el giro de un paso, por el socket.
    if (const auto turn = DirectionOf(published.angles.z, "0x565700"); !turn.HasValue())
        return Core::Unexpected(turn.Error());
    if (context.sendWalk)
    {
        const std::array<Protocol::Movement::Cell, 1> cell{{{x, y}}};
        context.sendWalk(cell, cell[0], published.angles.z);
    }
    hero.movement = 0;
    // 0x565954..0x56599f: abre y cierra la X y la Y de la celda.
    static_cast<void>(ReadTheCell(hero.cellXAccesses, hero.cellX, random));
    static_cast<void>(ReadTheCell(hero.cellYAccesses, hero.cellY, random));
    hero.attackTarget = static_cast<std::uint16_t>(target);
    const auto direction = DirectionOf(published.angles.z, "0x5659cd");
    if (!direction.HasValue())
        return Core::Unexpected(direction.Error());
    // 0x5659e6..0x565a47: con "webzen" en el nombre del héroe publicado, no pega.
    if (Combat::CNameOf(published).find(NoAttackName) != std::string::npos)
        return {};
    if (!context.sendAttack)
        return {};
    context.sendAttack(world.Actors()[static_cast<std::size_t>(target)].key, Protocol::Combat::AttackAction,
                       direction.Value());
    return {};
}

// 0x56535b..0x566299: la Y (+0x420) y la X (+0x41c), cada una con 0x40b570 y 0x40b800, y PathFinding2(X, Y, blanco,
// +0x3e8, 0) con el alcance de distancia (0x4eac00, donde la bandera del altar no cuenta): las celdas a menos del
// alcance del blanco son la meta. Con camino, +0x378 en 1 y nada más: sin SendMove, el servidor no ve el camino hasta
// el D3 de girar al llegar (07). Sin camino, nada.
Core::Expected<void, std::string> WalkIntoRange(Kernel::ActorState& hero, std::int32_t targetX, std::int32_t targetY,
                                                float range, const HeroArrivalContext& context)
{
    auto& random = context.combat.random;
    const std::uint8_t y = ReadTheCell(hero.cellYAccesses, hero.cellY, random);
    const std::uint8_t x = ReadTheCell(hero.cellXAccesses, hero.cellX, random);
    // La celda del blanco fuera de la grilla, que el A* toma como índice lineal (la fila de al lado, o fuera de los
    // 65.536 WORD): MuRet descarta el acercamiento, con los dos rand() de las lecturas (07, Q-C1179).
    if (targetX < 0 || targetX > LastCell || targetY < 0 || targetY > LastCell)
        return {};
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.combat.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x566272: falta TerrainWall para el camino hasta el alcance"));
    const Pathfinding::PathCell target{static_cast<std::uint8_t>(targetX), static_cast<std::uint8_t>(targetY)};
    // 0x566272: PathFinding2 calcula primero la bandera del altar de Crywolf (0x4eac0a..0x4eac41), que con el alcance
    // distinto de 0 no cuenta (0x4eadd4).
    const auto path = Actors::FindThePath(context.combat.world, terrain.attributes->cells, {x, y}, target, range);
    if (!path)
        return {};
    hero.path = *path;
    hero.movement = 1;
    return {};
}

// 0x56610d..0x566127: la skill − 1 por las tablas 0x56ab4c y 0x56ab34.
enum class ArrivalSkillKind : std::uint8_t
{
    None,
    OnItsTarget,    // 0x56629a: 0x55cbc0
    AtTheTarget,    // 0x566143: 0x55b6f0
    ElfOnItsTarget, // 0x56642c y 0x5665c3: 0x55faa0
    OnUniria,       // 0x56612e: la 0x2f, con el héroe publicado en Uniria, como 0x566143
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

// 0x56601f..0x566754: el caso 5.
Core::Expected<void, std::string> UseTheSkillOnArrival(Kernel::ActorState& hero, const HeroArrivalContext& context)
{
    Kernel::WorldState& world = context.combat.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    auto& random = context.combat.random;
    const Movement::WeaponSkillRequest request = runtime.movement.weaponSkill;
    // 0x56601f..0x566106: con el byte de 0x7758580, el lugar 0x7758584 del registro propio; si no, 0x7758584 entero. El
    // registro se abre y se cierra igual.
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
    // 0x566143..0x56616d (y sus pares): +0x389 del blanco 0x7758588 y el −1. El original lee +0x389 antes de mirar el
    // −1 (Q-C1216), pero con −1 o menos sale por cualquiera de los dos.
    const std::int32_t target = request.target;
    if (target <= -1)
        return {};
    if (static_cast<std::size_t>(target) >= world.Actors().size())
        return Core::Unexpected(
            std::format("0x56615b: el blanco {} de la skill pendiente está fuera del pool", target));
    const Kernel::ActorState& victim = world.Actors()[static_cast<std::size_t>(target)];
    if (victim.dying != 0)
        return {};
    // 0x566173..0x566194: la celda del blanco, ftol(· 0,01), en 0x77e1ba8 y 0x77e1bac.
    const auto cellX =
        Math::OriginalFtolLow(static_cast<double>(victim.position.x) * static_cast<double>(CellReciprocal));
    const auto cellY =
        Math::OriginalFtolLow(static_cast<double>(victim.position.y) * static_cast<double>(CellReciprocal));
    if (!cellX || !cellY)
        return Core::Unexpected(std::string("0x56617c: la celda del blanco de la skill fuera del perfil ftol"));
    runtime.movement.targetX = std::bit_cast<std::uint32_t>(*cellX);
    runtime.movement.targetY = std::bit_cast<std::uint32_t>(*cellY);
    // 0x566194..0x5661f0: la distancia al centro de la celda contra el alcance · 120,0000076 (la 0x55b6f0) o · 100, en
    // la x87. TEST AH, 1: con C0 (menor o desordenado) está fuera; igual está en alcance (Q-C1216).
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
        // 0x56634c..0x5663a7: la línea de vista desde la celda lógica del héroe (la Y y la X).
        const std::uint8_t y = ReadTheCell(hero.cellYAccesses, hero.cellY, random);
        const std::uint8_t x = ReadTheCell(hero.cellXAccesses, hero.cellX, random);
        const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.combat.terrain);
        if (!terrain.attributes)
            return Core::Unexpected(
                std::string("0x558800: falta TerrainWall para ver el blanco de la skill pendiente"));
        // 0x56639d: la línea que sale de TerrainWall (0x55886d lee fuera de los 65.536 WORD) MuRet la toma como sin
        // línea (08, P-C256).
        const auto seen = Pathfinding::CanSeeTheCell(terrain.attributes->cells, x, y, *cellX, *cellY);
        if (!seen || !*seen)
            return {};
        if (kind == ArrivalSkillKind::OnItsTarget)
            return Skills::UseTheSkillOnItsTarget(hero, hero, skills);
        return Skills::UseTheElfSkillOnItsTarget(hero, hero, skills);
    }
    // 0x56620d..0x566299: el camino hasta el alcance (· 1,2 en un float, con 0x55b6f0) desde la celda lógica del héroe.
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
    // 0x565278..0x5652a6: el segundo argumento (el héroe) pegando ya no vuelve a pegar; de 0x22 a 0x97, sólo con la
    // 0x8d (Q-C1162).
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
    // 0x5652b5..0x5652e8: la celda del blanco, ftol(x · 0,01f) entero, en 0x77e1ba8 y 0x77e1bac.
    constexpr double CellScale = static_cast<double>(0.01f);
    const auto cellX = Math::OriginalFtolLow(static_cast<double>(victim.position.x) * CellScale);
    const auto cellY = Math::OriginalFtolLow(static_cast<double>(victim.position.y) * CellScale);
    if (!cellX || !cellY)
        return Core::Unexpected(std::string("0x5652d0: la celda del blanco fuera del perfil ftol"));
    runtime.movement.targetX = std::bit_cast<std::uint32_t>(*cellX);
    runtime.movement.targetY = std::bit_cast<std::uint32_t>(*cellY);
    if (hero.safeZone != 0)
        return {};
    // 0x5652fb..0x565347: la distancia al centro de la celda, en la x87, contra el alcance · 100. Menor, igual o
    // unordered (C0 o C3) está en alcance.
    const double dx = static_cast<double>(hero.position.x) - (static_cast<double>(*cellX) * 100.0 + 50.0);
    const double dy = static_cast<double>(hero.position.y) - (static_cast<double>(*cellY) * 100.0 + 50.0);
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double limit = static_cast<double>(range) * 100.0;
    if (distance > limit)
    {
        // 0x565349..0x5653b6: fuera de alcance sólo el elfo se acerca, con el camino hasta el alcance (0x566271).
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
