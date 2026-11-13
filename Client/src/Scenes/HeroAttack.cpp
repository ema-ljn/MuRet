#include "Scenes/HeroAttack.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "GameLogic/PathCell.h"
#include "GameLogic/PathFinder.h"
#include "Protocol/Combat.h"
#include "Protocol/Movement.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorState.h"
#include "Scenes/AttackRules.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldState.h"
#include "World/OriginalFtol.h"
#include "World/WorldTerrain.h"

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

namespace Client::Scenes
{
namespace
{
constexpr std::uint8_t PlayerObject = 1;
constexpr std::uint8_t MonsterObject = 2;
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
constexpr std::int32_t BrakeSkill = 0x28;
constexpr std::int32_t BrakeMilliseconds = 300;
constexpr std::uint32_t SiegeShield = 0x4000;
constexpr std::uint8_t MinimumOutlawPk = 6;
constexpr std::uint16_t NoGuild = 0xffff;
constexpr std::uint8_t ArrivalAttack = 3;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t ClassMask = 7;
// 0x6c8ff0: el nombre con el que el héroe no manda el D7.
constexpr std::string_view NoAttackName = "webzen";

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

// El nombre desde +0x1d9 como lo ve strcmp: los diez bytes, +0x1e3 y +0x1e4, que MuRet no tiene y toma en 0.
std::string CNameOf(const ActorState& actor)
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

std::string CNameOf(const std::array<std::byte, DuelNameBytes>& bytes)
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

// 0x558c2a..0x558d6d: con el asedio, los puestos +0x360 enfrentados y las marcas de +0x78 deciden por el escudo
// 0x4000 del elegido; si no decide, sigue como sin asedio.
std::optional<bool> SiegeDecides(const ActorState& hero, const ActorState& target)
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
    case 5:
        return "0x56601f: el caso 5 de la llegada (+0x379 en 5) es de otro hito de la CL9";
    default:
        return {};
    }
}

// 0x565107..0x56526a: el alcance, en celdas, por las armas (los tipos del registro propio, +0xb6c la derecha y
// +0xbb4 la izquierda) y +0x376.
float AttackRange(const ActorState& hero, const HeroArrivalContext& context)
{
    WorldState& world = context.combat.world;
    std::int32_t right = 0;
    std::int32_t left = 0;
    {
        const Render::Models::LogicalAccess own(world.Runtime().ownRecordDepth, context.combat.random,
                                                Render::Models::OwnRecordCloseRandoms);
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

std::uint8_t ReadTheCell(std::uint8_t& depth, std::uint8_t cell, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
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
Core::Expected<void, std::string> HitTheTarget(ActorState& hero, std::int32_t target, const HeroArrivalContext& context)
{
    WorldState& world = context.combat.world;
    WorldRuntime& runtime = world.Runtime();
    auto& random = context.combat.random;
    runtime.movement.mouseUpdateTime = runtime.movement.mouseUpdateTimeMax;
    if (auto attacked = SetActorAttack(hero, context.combat); !attacked.HasValue())
        return attacked;
    hero.byte381 = 1;
    hero.attackPosition = world.Actors()[static_cast<std::size_t>(target)].position;
    hero.angles.z = static_cast<float>(
        Render::Models::CreateAngle(hero.position.x, hero.position.y, hero.attackPosition.x, hero.attackPosition.y));
    // 0x56544c..0x56548b: la celda del héroe publicado, X con 0x40b570/0x40b800 e Y con 0x4e7e40.
    ActorState& published = world.Actors()[runtime.hero];
    const std::uint8_t x = ReadTheCell(published.cellXAccesses, published.cellX, random);
    const std::uint8_t y = ReadTheCell(published.cellYAccesses, published.cellY, random);
    // 0x56548b..0x565954: C1 06 D3 X Y (rumbo << 4), el giro de un paso, por el socket.
    if (const auto turn = DirectionOf(published.angles.z, "0x565700"); !turn.HasValue())
        return Core::Unexpected(turn.Error());
    if (context.sendWalk)
    {
        const std::array<Protocol::Movement::Cell, 1> cell{{{x, y}}};
        if (auto sent = context.sendWalk(cell, cell[0], published.angles.z); !sent.HasValue())
            return sent;
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
    if (CNameOf(published).find(NoAttackName) != std::string::npos)
        return {};
    if (!context.sendAttack)
        return {};
    return context.sendAttack(world.Actors()[static_cast<std::size_t>(target)].key, Protocol::Combat::AttackAction,
                              direction.Value());
}

// 0x56535b..0x566299: la Y (+0x420) y la X (+0x41c), cada una con 0x40b570 y 0x40b800, y PathFinding2(X, Y, blanco,
// +0x3e8, 0) con el alcance de distancia (0x4eac00, sin bClearGoal): las celdas a menos del alcance del blanco son la
// meta. Con camino, +0x378 en 1 y nada más: sin SendMove, el servidor no ve el camino hasta el D3 de girar al llegar
// (07). Sin camino, nada.
Core::Expected<void, std::string> WalkIntoRange(ActorState& hero, std::int32_t targetX, std::int32_t targetY,
                                                float range, const HeroArrivalContext& context)
{
    auto& random = context.combat.random;
    const std::uint8_t y = ReadTheCell(hero.cellYAccesses, hero.cellY, random);
    const std::uint8_t x = ReadTheCell(hero.cellXAccesses, hero.cellX, random);
    // 0x566272: PathFinding2 mira primero el altar de Crywolf.
    if (auto altar = CheckTheCrywolfAltar(context.combat.world, "0x566272"); !altar.HasValue())
        return altar;
    // La celda del blanco fuera de la grilla, que el A* toma como índice lineal (la fila de al lado, o fuera de los
    // 65.536 WORD): MuRet descarta el acercamiento, con los dos rand() de las lecturas (07, Q-C1179).
    constexpr std::int32_t LastCell = 0xff;
    if (targetX < 0 || targetX > LastCell || targetY < 0 || targetY > LastCell)
        return {};
    World::WorldTerrain& terrain = context.combat.world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x566272: falta TerrainWall para que el elfo se acerque"));
    const GameLogic::PathCell target{static_cast<std::uint8_t>(targetX), static_cast<std::uint8_t>(targetY)};
    const auto path = GameLogic::PathFinder(terrain.attributes->cells).Find({x, y}, target, false, range);
    if (!path)
        return {};
    hero.path = *path;
    hero.movement = 1;
    return {};
}

Core::Expected<void, std::string> AttackOnArrival(ActorState& hero, const HeroArrivalContext& context)
{
    WorldState& world = context.combat.world;
    WorldRuntime& runtime = world.Runtime();
    const float range = AttackRange(hero, context);
    const std::int32_t target = runtime.movement.attackTarget;
    if (target == -1)
        return {};
    // 0x565278..0x5652a6: el segundo argumento (el héroe) pegando ya no vuelve a pegar; de 0x22 a 0x97, sólo con la
    // 0x8d (Q-C1162).
    const std::uint8_t action = hero.animation.action;
    if (hero.type == Data::Models::PlayerSkeletonModel)
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
    const ActorState& victim = world.Actors()[static_cast<std::size_t>(target)];
    // 0x5652b5..0x5652e8: la celda del blanco, ftol(x · 0,01f) entero, en 0x77e1ba8 y 0x77e1bac.
    constexpr double CellScale = static_cast<double>(0.01f);
    const auto cellX = World::OriginalFtolLow(static_cast<double>(victim.position.x) * CellScale);
    const auto cellY = World::OriginalFtolLow(static_cast<double>(victim.position.y) * CellScale);
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
        if ((hero.characterClass & ClassMask) != ElfClass)
            return {};
        return WalkIntoRange(hero, *cellX, *cellY, range, context);
    }
    return HitTheTarget(hero, target, context);
}
} // namespace

Core::Expected<bool, std::string> IsAttackable(const WorldState& world, bool controlDown)
{
    const WorldRuntime& runtime = world.Runtime();
    if (runtime.cursorItemCount > 0)
        return false;
    const std::int32_t chosen = runtime.selectedTargets[2];
    if (chosen == -1)
        return false;
    const auto actors = world.Actors();
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= actors.size())
        return Core::Unexpected(std::format("0x558bce: el elegido {} está fuera del pool", chosen));
    const ActorState& target = actors[static_cast<std::size_t>(chosen)];
    const ActorState& hero = actors[runtime.hero];
    const bool isHero = &target == &hero;
    const AttackRules& rules = runtime.attackRules;
    if (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap && !isHero)
        return true;
    if (target.byte88 == MonsterObject)
        return rules.byte576796c == 0 || rules.byte576796d != 0;
    if (target.byte88 != PlayerObject)
        return false;
    if (runtime.siegeActive)
    {
        if (const auto decided = SiegeDecides(hero, target))
            return *decided;
    }
    // 0x558d6e..0x558e9b.
    if (target.byte370 == 2 || target.byte370 == 8)
        return true;
    // 0x558da4..0x558df3: en la guerra de gremios, el nombre del gremio del elegido (0x787aa55 + 0x58 · +0x1f2) contra
    // el del héroe: iguales, falso; distintos, sigue. Sin los nombres hasta el hito 8, falso (08, P-C250 f).
    if (rules.byte576796c != 0 && target.pk >= MinimumOutlawPk && target.word1f2 != NoGuild)
        return false;
    if (rules.duelActive)
    {
        const bool rival = static_cast<std::int32_t>(static_cast<std::int16_t>(target.key)) == rules.duelKey;
        return rival && CNameOf(target) == CNameOf(rules.duelName);
    }
    if (rules.byte576796c != 0)
        return target.byte374 == 2 && !isHero;
    if (target.pk >= MinimumOutlawPk)
        return true;
    return controlDown && !isHero;
}

Core::Expected<std::int32_t, std::string> ChosenKeyOf(const WorldState& world, std::size_t attacker, bool controlDown)
{
    constexpr std::int32_t NoKey = -1;
    const WorldRuntime& runtime = world.Runtime();
    // 0x558eb3..0x558ed8: el cursor (JG, con signo), el elegido y el héroe publicado.
    const std::int32_t chosen = runtime.selectedTargets[2];
    if (runtime.cursorItemCount > 0 || chosen == -1 || attacker != runtime.hero)
        return NoKey;
    const auto actors = world.Actors();
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= actors.size())
        return Core::Unexpected(std::format("0x558ede: el elegido {} está fuera del pool", chosen));
    const ActorState& target = actors[static_cast<std::size_t>(chosen)];
    const bool isHero = static_cast<std::size_t>(chosen) == runtime.hero;
    const auto key = static_cast<std::int32_t>(static_cast<std::int16_t>(target.key));
    // 0x558ef0..0x558f08.
    if (runtime.map >= FirstChaosCastleMap && runtime.map <= LastChaosCastleMap)
        return key;
    const AttackRules& rules = runtime.attackRules;
    // 0x558f09..0x558f7c.
    // 0x558f29..0x558f7c: los nombres de gremio, como en 0x558da4; iguales, −1. Sin los nombres hasta el hito 8, −1
    // (08, P-C250 f; Q-C1170).
    if (rules.byte576796c != 0 && target.pk >= MinimumOutlawPk && target.word1f2 != NoGuild)
        return NoKey;
    // 0x558f84..0x558fed.
    if (rules.duelActive)
        return key == rules.duelKey ? key : NoKey;
    if (target.byte370 == 2 || target.byte370 == 8)
        return key;
    if (rules.byte576796c != 0)
        return target.byte374 == 2 && !isHero ? key : NoKey;
    if (target.pk >= MinimumOutlawPk)
        return key;
    return controlDown && !isHero ? key : NoKey;
}

Core::Expected<void, std::string> CheckTheCrywolfAltar(const WorldState& world, std::string_view caller)
{
    // 0x4eac0a..0x4eac1a: 0x435a00 (la etapa 3 o 4) y 0x4303c0 (el mapa 34).
    constexpr std::int32_t CrywolfMap = 34;
    constexpr std::uint8_t ThirdStage = 3;
    constexpr std::uint8_t FourthStage = 4;
    const WorldRuntime& runtime = world.Runtime();
    const bool crywolf =
        (runtime.crywolf.stage == ThirdStage || runtime.crywolf.stage == FourthStage) && runtime.map == CrywolfMap;
    if (!crywolf)
        return {};
    // 0x4eac1c..0x4eac3f: [0x743a16c] + 0x438 · [0x6c8e54] + 2, comparado en 16 bits con signo.
    const std::int32_t chosen = runtime.movement.chosenNpc;
    const auto actors = world.Actors();
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= actors.size())
        return {};
    constexpr std::int16_t FirstAltar = 0x204;
    constexpr std::int16_t LastAltar = 0x208;
    const auto model = static_cast<std::int16_t>(actors[static_cast<std::size_t>(chosen)].type);
    if (model < FirstAltar || model > LastAltar)
        return {};
    return Core::Unexpected(std::format("0x4eac41: el camino de {} con un altar de Crywolf elegido (la bandera del A* "
                                        "de 0x4ead70) es del hito 11",
                                        caller));
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

Core::Expected<void, std::string> InteractOnArrival(ActorState& hero, const HeroArrivalContext& context)
{
    const std::int32_t type = static_cast<std::int8_t>(hero.movementType);
    if (type == ArrivalAttack)
        return AttackOnArrival(hero, context);
    const std::string_view other = ArrivalCaseOf(type);
    if (other.empty())
        return {};
    return Core::Unexpected(std::string(other));
}
} // namespace Client::Scenes
