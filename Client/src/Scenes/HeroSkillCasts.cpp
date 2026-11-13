#include "Scenes/HeroSkillCasts.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/SkillTable.h"
#include "GameLogic/PathFinder.h"
#include "Network/Delivery.h"
#include "Protocol/Combat.h"
#include "Protocol/Movement.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skill.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorCombat.h"
#include "Scenes/ActorState.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/GameParticles.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/HudItems.h"
#include "Scenes/InventoryMoves.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/OwnInventory.h"
#include "Scenes/SkillDelays.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/ItemState.h"
#include "World/OriginalFtol.h"
#include "World/ParticleOwner.h"
#include "World/Particles.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
constexpr std::size_t ManaOffset = 0x22;
constexpr std::size_t StaminaOffset = 0x24;
constexpr std::size_t RangeOffset = 0x26;
constexpr std::uint16_t ThirdMount = 0x1c16;
constexpr std::uint16_t DarkHorse = 0x1c37;
constexpr std::int32_t FirstBloodCastleMap = 11;
constexpr std::int32_t LastBloodCastleMap = 17;
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a});    // [0x6b86c0], 0,01
constexpr float ChosenRangeFactor = std::bit_cast<float>(std::uint32_t{0x3f99999a}); // [0x6b8bb0], 1,2
constexpr float BloodCastleRange = std::bit_cast<float>(std::uint32_t{0x3fe66666});  // 0x55969e, 1,8
constexpr float RumboFactor = std::bit_cast<float>(std::uint32_t{0x3cb60b61});       // [0x6b87d8], 1/45
constexpr std::string_view NoAttackName = "webzen";                                  // 0x6c8fa0 y 0x6c8fa8
constexpr std::uint32_t HiddenFlag20 = 0x20;
constexpr std::uint16_t WallAttribute = 4;
constexpr float CellSize = std::bit_cast<float>(std::uint32_t{0x42c80000});   // [0x6b86b4], 100
constexpr float HalfCell = std::bit_cast<float>(std::uint32_t{0x42480000});   // [0x6b86b0], 50
constexpr float HalfTurn = std::bit_cast<float>(std::uint32_t{0x43340000});   // [0x6b86bc], 180
constexpr float OneOver360 = std::bit_cast<float>(std::uint32_t{0x3b360b61}); // [0x6ba444], 1/360
constexpr float ByteScale = std::bit_cast<float>(std::uint32_t{0x437f0000});  // [0x6b9f98], 255
constexpr float TurnToByte = std::bit_cast<float>(std::uint32_t{0x3f360b61}); // [0x6ba440], 256/360
constexpr std::uint16_t Byte38eCloseRandoms = 1;
constexpr std::size_t OwnSkillCount = 0x40;
constexpr std::uint16_t BlockedAttribute = 8;

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

bool InBloodCastle(std::int32_t map)
{
    return InRange(map, FirstBloodCastleMap, LastBloodCastleMap);
}

Render::Models::LogicalAccess OwnAccess(const HeroSkillContext& context)
{
    return Render::Models::LogicalAccess(context.world.Runtime().ownRecordDepth, context.random,
                                         Render::Models::OwnRecordCloseRandoms);
}

Render::Models::LogicalAccess EntryAccess(const HeroSkillContext& context, std::uint8_t skill)
{
    return Render::Models::LogicalAccess(context.world.Runtime().movement.skillDepths[skill], context.random,
                                         static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
}

// Una celda lógica (+0x41c o +0x420) con 0x40b570/0x412810 y 0x40b800: 4 rand() al cerrarla en 0.
std::uint8_t ReadCell(std::uint8_t& depth, std::uint8_t cell, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    return cell;
}

// El nombre (+0x1d9) hasta su '\0', como lo recorre la búsqueda en línea de "webzen".
bool NamedWebzen(const ActorState& actor)
{
    std::string name;
    for (const std::byte letter : actor.name)
    {
        if (letter == std::byte{0})
            break;
        name.push_back(static_cast<char>(letter));
    }
    return name.find(NoAttackName) != std::string::npos;
}

Core::Expected<ActorState*, std::string> ActorAt(WorldState& world, std::int32_t index, std::string_view where)
{
    if (index < 0 || static_cast<std::size_t>(index) >= world.Actors().size())
        return Core::Unexpected(std::format("{}: el personaje {} está fuera de la tabla", where, index));
    return &world.Actors()[static_cast<std::size_t>(index)];
}

// El lugar de `actor` en el pool, para 0x558eb0, que compara el puntero con [0x743a170]. Fuera del pool, ninguno.
std::size_t SlotOf(const WorldState& world, const ActorState& actor)
{
    const auto actors = world.Actors();
    for (std::size_t slot = 0; slot < actors.size(); ++slot)
    {
        if (&actors[slot] == &actor)
            return slot;
    }
    return actors.size();
}

// El byte bajo de ftol, o el corte con `where`.
Core::Expected<std::uint8_t, std::string> FtolByte(double value, std::string_view where)
{
    const auto whole = World::OriginalFtolLow(value);
    if (!whole)
        return Core::Unexpected(std::format("{}: el valor fuera del perfil ftol", where));
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(*whole) & 0xffu);
}

// 0x562934..0x56298f y 0x563e0b..0x563e66: la distancia de c al centro de la celda de 0x77e1ba8/0x77e1bac, en la x87,
// contra el alcance (0x5526f0, en float) · 100. Fuera (sin C0 ni C3), no; desordenado, sí.
bool TheChosenCellIsInReach(const ActorState& caster, const GameMovementState& state, float range)
{
    const auto cellX = static_cast<double>(std::bit_cast<std::int32_t>(state.targetX));
    const auto cellY = static_cast<double>(std::bit_cast<std::int32_t>(state.targetY));
    const double dx = static_cast<double>(caster.position.x) -
                      (cellX * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double dy = static_cast<double>(caster.position.y) -
                      (cellY * static_cast<double>(CellSize) + static_cast<double>(HalfCell));
    const double distance = std::sqrt(dx * dx + dy * dy);
    return !(distance > static_cast<double>(range) * static_cast<double>(CellSize));
}

// 0x562a01..0x562a5b: la distancia de la celda lógica de c a la del blanco, cada eje entre −8 y 7, en un byte.
std::uint8_t TowardsTheChosen(const GameMovementState& state, std::uint8_t x, std::uint8_t y)
{
    const std::int32_t dx = std::clamp(std::bit_cast<std::int32_t>(state.targetX) - std::int32_t{x}, -8, 7);
    const std::int32_t dy = std::clamp(std::bit_cast<std::int32_t>(state.targetY) - std::int32_t{y}, -8, 7);
    return static_cast<std::uint8_t>(((static_cast<std::uint32_t>(dx + 8) << 4u) & 0xf0u) |
                                     (static_cast<std::uint32_t>(dy - 8) & 0x0fu));
}

// 0x4ea030(personaje, acción) con las acciones de Player.bmd.
Core::Expected<void, std::string> SetPlayerAction(ActorState& actor, std::uint8_t action,
                                                  const HeroSkillContext& context, std::string_view where)
{
    if (context.playerSkeleton == nullptr)
        return Core::Unexpected(std::format("{}: 0x4ea030 sin las acciones de Player.bmd", where));
    Render::Models::SetAction(actor.animation, *context.playerSkeleton, action);
    return {};
}

// 0x4f4a10 con el contexto de las skills: sin la biblioteca de modelos, como el ataque al llegar.
Core::Expected<void, std::string> AttackWith(ActorState& actor, const HeroSkillContext& context)
{
    return SetActorAttack(actor, ActorCombatContext{.world = context.world,
                                                    .random = context.random,
                                                    .playerSkeleton = context.playerSkeleton,
                                                    .models = nullptr,
                                                    .items = context.items});
}

// 0x563b8f..0x564f37: 0x4f4a10 sobre c; un jugador (0x20a en +2) o, en la 0x18, uno con 1 en +0x88, nada más. Si
// no, +0x38e (un rand() al cerrarlo), su lugar en las skills del registro propio (abierto por quien llama) y la
// flecha de 0x53e2b0(c, c, 0, lugar, `count`, 0), sin portar (P-C257 d).
Core::Expected<void, std::string> ShootTheArrow(ActorState& caster, std::uint8_t skill, std::uint8_t count,
                                                const HeroSkillContext& context)
{
    if (auto attacked = AttackWith(caster, context); !attacked.HasValue())
        return attacked;
    if (caster.type == Data::Models::PlayerSkeletonModel || (skill == 0x18 && caster.byte88 == 1))
        return {};
    std::uint8_t carried = 0;
    {
        const Render::Models::LogicalAccess access(caster.byte38eAccesses, context.random, Byte38eCloseRandoms);
        carried = caster.byte38e;
    }
    std::size_t slot = 0;
    {
        const auto own = OwnAccess(context);
        for (std::size_t index = 0; index < OwnSkillCount; ++index)
        {
            if (OwnSkillByte(context.world.OwnCharacter(), index) == carried)
            {
                slot = index;
                break;
            }
        }
    }
    return Core::Unexpected(std::format("0x564f2f: la flecha de 0x53e2b0 (lugar {}, {}) de la skill {:#x} de un "
                                        "personaje que no es un jugador está sin portar (P-C257 d)",
                                        slot, count, skill));
}

// 0x55bd12..0x55bdd8: la acción de un jugador por la skill (la tabla 0x55cb78 por skill − 0x2b, a 0x55cb60).
std::optional<std::uint8_t> PlayerActionOf(std::uint8_t skill, const ActorState& caster, const WorldRuntime& runtime)
{
    switch (skill)
    {
    case 0x2b:
        return std::uint8_t{0x43};
    case 0x2c:
        return std::uint8_t{0x85};
    case 0x2f:
        return caster.pet.model == DarkHorse ? std::uint8_t{0x5f} : std::uint8_t{0x42};
    case 0x31:
        return runtime.map == 8 || runtime.map == 10 ? std::uint8_t{0x41} : std::uint8_t{0x40};
    case 0x3c:
    case 0x3d:
    case 0x4a:
        return std::nullopt;
    default:
        // 0x55bda6..0x55bdd8: con el caballo fuera de la zona segura, la 0x5d; si no, 0x7758584 + 0x25 (el lugar o la
        // skill que quedó ahí, no la skill leída).
        if (caster.pet.model == DarkHorse && caster.safeZone == 0)
            return std::uint8_t{0x5d};
        return static_cast<std::uint8_t>(runtime.movement.weaponSkill.slot + 0x25u);
    }
}

// 0x55c680..0x55c72a: la celda del blanco corrida un paso por el rumbo de `hero` (ftol(+0x24 · 1/45), en valor
// absoluto, de 0 a 7; con más, quieta): la tabla 0x55cb98.
void StepTheCellByTheRumbo(std::uint8_t& x, std::uint8_t& y, std::int32_t rumbo)
{
    switch (rumbo)
    {
    case 0:
        ++y;
        break;
    case 1:
        --x;
        ++y;
        break;
    case 2:
        --x;
        break;
    case 3:
        --x;
        --y;
        break;
    case 4:
        --y;
        break;
    case 5:
        ++x;
        --y;
        break;
    case 6:
        ++x;
        break;
    case 7:
        ++x;
        ++y;
        break;
    default:
        break;
    }
}

// 0x55be0d..0x55be49: 0x53fe70(0x5fe, +0x10, +0x1c, (1; 1; 1), 0, 0, `hero`), con el dueño en el pool, y el sonido
// 0x41ed10(rand() % 2 + 0x3c, 0, 0), que saca su rand() también con el pool lleno (CL10: queda en el último sonido).
Core::Expected<void, std::string> ShineOnTheHero(const ActorState& hero, const HeroSkillContext& context)
{
    constexpr std::int32_t FirstShineSound = 0x3c;
    WorldState& world = context.world;
    const std::size_t slot = SlotOf(world, hero);
    if (slot >= world.Actors().size())
        return Core::Unexpected(std::string("0x55be23: el dueño del brillo 0x5fe no está en el pool de personajes"));
    const auto created = world.Particles().CreateFollowingChecked(
        World::SkillShineParticle, hero.position, hero.angles, glm::vec3(1.0f), 0, 0.0f,
        World::ParticleOwner{.kind = World::ParticleOwnerKind::Actor, .identity = slot}, ParticleOwnerBonesOf(world));
    if (!created.HasValue())
        return Core::Unexpected(std::format("0x55be23: el brillo 0x5fe (0x53fe70) falla con el error {}",
                                            static_cast<unsigned>(created.Error())));
    world.Runtime().lastSoundId = static_cast<std::int32_t>(context.random.Next() % 2) + FirstShineSound;
    return {};
}

std::uint16_t WordAt(std::span<const std::byte, Data::Tables::SkillRecordSize> record, std::size_t offset)
{
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(record[offset]) |
                                      (std::to_integer<std::uint16_t>(record[offset + 1]) << 8));
}
} // namespace

Core::Expected<SkillCost, std::string> SkillCostOf(std::uint8_t skill, const HeroSkillContext& context,
                                                   std::string_view where)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::format("{}: 0x551c70 sin Skill.bmd", where));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("{}: Skill.bmd no tiene la skill {}", where, skill));
    const Render::Models::LogicalAccess entry(context.world.Runtime().movement.skillDepths[skill], context.random,
                                              static_cast<std::uint16_t>(Data::Tables::SkillRecordSize));
    return SkillCost{.mana = WordAt(*record, ManaOffset), .stamina = WordAt(*record, StaminaOffset)};
}

Core::Expected<void, std::string> UseAManaPotion(const HeroSkillContext& context, std::string_view where)
{
    WorldRuntime& runtime = context.world.Runtime();
    const std::span<const World::ItemState, InventoryCellCount> cells(context.world.Inventory().cells);
    const auto found =
        FindQuickSlotItem(runtime.hud.quickSlots, cells, ManaPotionSlot, runtime.movement.gateRequestBlocked != 0);
    if (!found.HasValue())
        return Core::Unexpected(found.Error());
    if (!found.Value())
        return {};
    return Core::Unexpected(std::format("{}: la poción de maná de la casilla {} (0x5af520 y el C1 26) es del hito 5",
                                        where, *found.Value()));
}

Core::Expected<float, std::string> SkillRangeOf(std::uint8_t skill, const ActorState& caster,
                                                const HeroSkillContext& context)
{
    auto range = SkillRangeOf(skill, context);
    if (range.HasValue() && caster.pet.model == ThirdMount)
        range.Value() += 2.0f;
    return range;
}

Core::Expected<float, std::string> SkillRangeOf(std::uint8_t skill, const HeroSkillContext& context)
{
    if (context.skills == nullptr)
        return Core::Unexpected(std::string("0x5526f0: falta Skill.bmd"));
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::string("0x5526f0: falta el registro de habilidad"));
    const auto entry = EntryAccess(context, skill);
    return static_cast<float>(std::to_integer<std::uint8_t>((*record)[RangeOffset]));
}

void SendTheSkillPacket(const HeroSkillContext& context, Protocol::Outgoing packet)
{
    if (packet.delivery == Network::Delivery::Encrypted)
        context.random.Discard(2);
    if (context.send)
        context.send(std::move(packet));
}

Core::Expected<bool, std::string> PassesTheSkillBrake(std::int32_t skill, const HeroSkillContext& context,
                                                      std::string_view where)
{
    if (!context.tickCount)
        return Core::Unexpected(std::format("{}: el freno de 300 ms (0x576791c) sin GetTickCount", where));
    return PassesTheActionBrake(context.world.Runtime().movement.lastActionTick, skill, context.tickCount);
}

Core::Expected<bool, std::string> UseTheSkillOnTheChosen(ActorState& caster, ActorState& hero, std::uint8_t skill,
                                                         const HeroSkillContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    GameMovementState& state = runtime.movement;
    const std::int32_t chosen = runtime.selectedTargets[2];
    if (chosen <= -1)
        return false;
    const auto target = ActorAt(world, chosen, "0x55952d");
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    // 0x55953e..0x55956c: la celda del elegido.
    const auto cellX =
        World::OriginalFtolLow(static_cast<double>(target.Value()->position.x) * static_cast<double>(CellReciprocal));
    const auto cellY =
        World::OriginalFtolLow(static_cast<double>(target.Value()->position.y) * static_cast<double>(CellReciprocal));
    if (!cellX || !cellY)
        return Core::Unexpected(std::string("0x55954b: la celda del elegido fuera del perfil ftol"));
    state.targetX = std::bit_cast<std::uint32_t>(*cellX);
    state.targetY = std::bit_cast<std::uint32_t>(*cellY);
    state.weaponSkill = WeaponSkillRequest{.active = 0, .slot = skill, .target = chosen};
    if (context.skills == nullptr || !context.skills->Record(skill))
        return Core::Unexpected(std::format("0x5595b5: Skill.bmd no tiene la skill {}", skill));
    // 0x5595b5..0x559720 y 0x55982d..0x55987e: la entrada de la skill, abierta hasta el final.
    const auto entry = EntryAccess(context, skill);
    const auto range = SkillRangeOf(skill, caster, context);
    if (!range.HasValue())
        return Core::Unexpected(range.Error());
    float reach = static_cast<float>(static_cast<double>(range.Value()) * static_cast<double>(ChosenRangeFactor));
    if (InBloodCastle(runtime.map) && InRange(skill, 0x13, 0x17))
        reach = BloodCastleRange;
    // 0x5596a5..0x5596f2: la distancia de `hero` al centro de la celda, en la x87; menor o igual que el alcance · 100
    // (sin C0) está en alcance.
    const double dx = static_cast<double>(hero.position.x) - (static_cast<double>(*cellX) * 100.0 + 50.0);
    const double dy = static_cast<double>(hero.position.y) - (static_cast<double>(*cellY) * 100.0 + 50.0);
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double limit = static_cast<double>(reach) * 100.0;
    if (!(limit < distance) && !std::isnan(distance))
    {
        if (auto cast = CastTheSkillAtTheTarget(caster, hero, context); !cast.HasValue())
            return Core::Unexpected(cast.Error());
        return true;
    }
    // 0x559725..0x559822: fuera de alcance, el camino desde las celdas lógicas de c.
    const std::uint8_t y = ReadCell(caster.cellYAccesses, caster.cellY, context.random);
    const std::uint8_t x = ReadCell(caster.cellXAccesses, caster.cellX, context.random);
    const World::WorldTerrain& terrain = world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x55980f: falta TerrainWall para el camino de la skill"));
    constexpr std::int32_t LastCell = 0xff;
    if (*cellX < 0 || *cellX > LastCell || *cellY < 0 || *cellY > LastCell)
        return Core::Unexpected(std::format("0x55980f: la celda del elegido ({}, {}) fuera de la grilla, que "
                                            "PathFinding2 toma como índice lineal",
                                            *cellX, *cellY));
    const auto path =
        GameLogic::PathFinder(terrain.attributes->cells)
            .Find({x, y}, {static_cast<std::uint8_t>(*cellX), static_cast<std::uint8_t>(*cellY)}, false, reach);
    if (!path)
        return false;
    caster.path = *path;
    caster.movement = 1;
    caster.movementType = 5;
    return true;
}

Core::Expected<void, std::string> CastTheSkillAtTheTarget(ActorState& caster, ActorState& hero,
                                                          const HeroSkillContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    const WeaponSkillRequest request = runtime.movement.weaponSkill;
    // 0x55b714..0x55b7fe: con 0x7758580, la skill del lugar 0x7758584 en el registro propio; si no, 0x7758584 es la
    // skill.
    std::uint8_t skill = 0;
    {
        const auto own = OwnAccess(context);
        skill = request.active != 0 ? OwnSkillByte(world.OwnCharacter(), request.slot)
                                    : static_cast<std::uint8_t>(request.slot & 0xffu);
    }
    // 0x55b803..0x55bc0c: C1 06 D3 X Y (rumbo << 4) con la celda lógica del héroe publicado, X antes que Y, por el
    // socket.
    ActorState& published = world.Actors()[runtime.hero];
    const std::uint8_t x = ReadCell(published.cellXAccesses, published.cellX, context.random);
    const std::uint8_t y = ReadCell(published.cellYAccesses, published.cellY, context.random);
    const std::array<Protocol::Movement::Cell, 1> cell{{{x, y}}};
    auto turn = Protocol::Movement::WriteWalk(cell, cell[0], published.angles.z);
    if (!turn.HasValue())
        return Core::Unexpected(std::string("0x55baac: el rumbo del héroe fuera del perfil ftol"));
    SendTheSkillPacket(context, std::move(turn.Value()));
    // 0x55bcf2..0x55bde3.
    caster.movement = 0;
    if (hero.type == Data::Models::PlayerSkeletonModel)
    {
        RefreshCombatActionSpeeds(world, context.random);
        if (const auto action = PlayerActionOf(skill, caster, runtime))
        {
            if (auto set = SetPlayerAction(hero, *action, context, "0x55bdd3"); !set.HasValue())
                return set;
        }
    }
    else if (auto attacked = AttackWith(caster, context); !attacked.HasValue())
    {
        return attacked;
    }
    // 0x55bde6..0x55be49: salvo las 0x3c, 0x3d y 0x4a, el brillo 0x5fe sobre `hero` y el sonido 0x3c o 0x3d.
    if (skill != 0x3c && skill != 0x3d && skill != 0x4a)
    {
        if (auto shone = ShineOnTheHero(hero, context); !shone.HasValue())
            return shone;
    }
    // 0x55be4c..0x55beb5: la posición del blanco en +0x3a8 de c y el rumbo de `hero` hacia él.
    const auto target = ActorAt(world, request.target, "0x55be4c");
    if (!target.HasValue())
        return Core::Unexpected(target.Error());
    caster.attackPosition = target.Value()->position;
    hero.angles.z = static_cast<float>(Render::Models::CreateAngle(hero.position.x, hero.position.y,
                                                                   caster.attackPosition.x, caster.attackPosition.y));
    // 0x55be9a..0x55c622: con "webzen" en el nombre del héroe publicado o sin pasar el freno, no sale el 19.
    if (!NamedWebzen(published))
    {
        const auto passes = PassesTheSkillBrake(skill, context, "0x55bf17");
        if (!passes.HasValue())
            return Core::Unexpected(passes.Error());
        if (passes.Value())
            SendTheSkillPacket(context, Protocol::Skill::WriteMagicAttack(skill, target.Value()->key));
    }
    // 0x55c62c..0x55c662.
    caster.byte381 = 1;
    if ((hero.flags78 & HiddenFlag20) == HiddenFlag20 || caster.pet.model == ThirdMount)
        return {};
    const auto cellX =
        World::OriginalFtolLow(static_cast<double>(caster.attackPosition.x) * static_cast<double>(CellReciprocal));
    const auto cellY =
        World::OriginalFtolLow(static_cast<double>(caster.attackPosition.y) * static_cast<double>(CellReciprocal));
    if (!cellX || !cellY)
        return Core::Unexpected(std::string("0x55c662: la celda del blanco fuera del perfil ftol"));
    auto dfX = static_cast<std::uint8_t>(*cellX & 0xff);
    auto dfY = static_cast<std::uint8_t>(*cellY & 0xff);
    const std::uint16_t monster = target.Value()->word376;
    if (InBloodCastle(runtime.map) || skill == 0x3c || skill == 0x39 || skill == 0x2c || monster == 0x115 ||
        monster == 0x11b || monster == 0x116 || monster == 0x120)
    {
        const auto rumbo =
            World::OriginalFtolLow(static_cast<double>(hero.angles.z) * static_cast<double>(RumboFactor));
        if (!rumbo)
            return Core::Unexpected(std::string("0x55c6e0: el rumbo del héroe fuera del perfil ftol"));
        StepTheCellByTheRumbo(dfX, dfY, std::abs(*rumbo));
    }
    // 0x55c72d..0x55c79b: 0x5f8290 (y · 256 + x) en los atributos de TerrainWall.
    const World::WorldTerrain& terrain = world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x55c745: falta TerrainWall para la celda del DF"));
    const std::uint16_t attribute = terrain.attributes->cells[std::size_t{dfY} * 256u + dfX];
    if ((attribute & WallAttribute) == WallAttribute || (attribute & BlockedAttribute) == BlockedAttribute)
        return {};
    if (skill == 0x2f || skill == 0x2b || skill == 0x31 || skill == 0x3c || skill == 0x3d || skill == 0x4a)
        return {};
    // 0x55c7a1..0x55cb48: C1 05 DF X Y, por el socket.
    SendTheSkillPacket(context, Protocol::Combat::WriteElfCell(dfX, dfY));
    return {};
}

Core::Expected<bool, std::string> UseTheListedWeaponSkill(ActorState& hero, const World::ItemState& item,
                                                          std::uint8_t skill, const HeroSkillContext& context)
{
    WorldState& world = context.world;
    const auto cost = SkillCostOf(skill, context, "0x559b0b");
    if (!cost.HasValue())
        return Core::Unexpected(cost.Error());
    const OwnCharacterRecord& own = world.OwnCharacter();
    if (own.mana < cost.Value().mana)
    {
        if (auto potion = UseAManaPotion(context, "0x559b41"); !potion.HasValue())
            return Core::Unexpected(potion.Error());
        return false;
    }
    // 0x55a292..0x55a2b3.
    if (cost.Value().stamina > own.bp)
        return false;
    const auto ready =
        TryStartSkillDelay(static_cast<std::uint8_t>(world.Runtime().movement.weaponSkill.slot & 0xffu),
                           SkillDelayContext{.world = world, .random = context.random, .skills = context.skills});
    if (!ready.HasValue())
        return Core::Unexpected(ready.Error());
    if (!ready.Value())
        return false;
    const auto attackable = IsAttackable(world, context.controlDown);
    if (!attackable.HasValue())
        return Core::Unexpected(attackable.Error());
    if (!attackable.Value())
        return UseTheWeaponOptions(hero, item, context);
    return UseTheSkillOnTheChosen(hero, hero, skill, context);
}

Core::Expected<bool, std::string> UseTheWeaponOptions(ActorState& hero, const World::ItemState& item,
                                                      const HeroSkillContext& context)
{
    WorldState& world = context.world;
    const auto own = OwnAccess(context);
    bool used = false;
    for (std::uint8_t option = 0; option < item.optionCount; ++option)
    {
        const std::uint8_t skill = item.OptionTag(option);
        const ActorState& published = world.Actors()[world.Runtime().hero];
        if (OwnSkillByte(world.OwnCharacter(), published.skillIndex) != skill)
            continue;
        // 0x55a368..0x55a3b0: el costo y, sin maná, la poción o la opción que sigue.
        const auto cost = SkillCostOf(skill, context, "0x55a37a");
        if (!cost.HasValue())
            return Core::Unexpected(cost.Error());
        if (world.OwnCharacter().mana < cost.Value().mana)
        {
            if (auto potion = UseAManaPotion(context, "0x55a3b0"); !potion.HasValue())
                return Core::Unexpected(potion.Error());
            continue;
        }
        // 0x55abb5..0x55abf3: la espera del lugar del héroe publicado y la tabla 0x55b6c8 por la opción − 0x12.
        const auto ready =
            TryStartSkillDelay(published.skillIndex,
                               SkillDelayContext{.world = world, .random = context.random, .skills = context.skills});
        if (!ready.HasValue())
            return Core::Unexpected(ready.Error());
        if (!ready.Value())
            continue;
        if (skill == 0x12)
        {
            // 0x55abfa..0x55b45a: la acción, y el 19 sobre el héroe publicado.
            hero.movement = 0;
            if (hero.type == Data::Models::PlayerSkeletonModel)
            {
                if (auto set = SetPlayerAction(hero, 0x98, context, "0x55ac12"); !set.HasValue())
                    return Core::Unexpected(set.Error());
            }
            else if (auto attacked = AttackWith(hero, context); !attacked.HasValue())
            {
                return Core::Unexpected(attacked.Error());
            }
            const ActorState& self = world.Actors()[world.Runtime().hero];
            if (!NamedWebzen(self))
            {
                const auto passes = PassesTheSkillBrake(skill, context, "0x55ac98");
                if (!passes.HasValue())
                    return Core::Unexpected(passes.Error());
                if (passes.Value())
                    SendTheSkillPacket(context, Protocol::Skill::WriteMagicAttack(skill, self.key));
            }
            used = true;
        }
        else if (InRange(skill, 0x13, 0x17) || skill == 0x31)
        {
            // 0x55b460..0x55b484.
            const auto attackable = IsAttackable(world, context.controlDown);
            if (!attackable.HasValue())
                return Core::Unexpected(attackable.Error());
            if (!attackable.Value())
                continue;
            const auto chosen = UseTheSkillOnTheChosen(hero, hero, skill, context);
            if (!chosen.HasValue())
                return chosen;
            used = chosen.Value();
        }
    }
    return used;
}

Core::Expected<bool, std::string> UseTheElfWeaponOptions(ActorState& hero, const World::ItemState& item,
                                                         const HeroSkillContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    GameMovementState& state = runtime.movement;
    const auto own = OwnAccess(context);
    bool used = false;
    for (std::uint8_t option = 0; option < item.optionCount; ++option)
    {
        const std::uint8_t skill = item.OptionTag(option);
        const ActorState& published = world.Actors()[runtime.hero];
        if (OwnSkillByte(world.OwnCharacter(), published.skillIndex) != skill)
            continue;
        // 0x562024..0x562092: el costo y, sin maná, la poción o la opción que sigue.
        const auto cost = SkillCostOf(skill, context, "0x56203e");
        if (!cost.HasValue())
            return Core::Unexpected(cost.Error());
        if (world.OwnCharacter().mana < cost.Value().mana)
        {
            if (auto potion = UseAManaPotion(context, "0x562075"); !potion.HasValue())
                return Core::Unexpected(potion.Error());
            continue;
        }
        // 0x5628c3..0x5628ef y 0x56500b: con la stamina de más o sin la espera, 0 aunque otra opción la haya usado.
        if (cost.Value().stamina > world.OwnCharacter().bp)
            return false;
        const auto ready =
            TryStartSkillDelay(published.skillIndex,
                               SkillDelayContext{.world = world, .random = context.random, .skills = context.skills});
        if (!ready.HasValue())
            return Core::Unexpected(ready.Error());
        if (!ready.Value())
            return false;
        // 0x5628f5..0x562919: el alcance (0x5526f0) de cualquier opción; solo la 0x18 y la 0x36 siguen.
        const auto range = SkillRangeOf(skill, hero, context);
        if (!range.HasValue())
            return Core::Unexpected(range.Error());
        if (skill != 0x18 && skill != 0x36)
            continue;
        // 0x56291f..0x56292f y 0x563df6..0x563e06: sin flechas (0x561c50), se terminan las opciones.
        static const InventoryServices NoServices{};
        const auto arrows = CheckTheArrows(
            InventoryMoveContext{.world = world,
                                 .random = context.random,
                                 .services = context.inventory != nullptr ? *context.inventory : NoServices});
        if (!arrows.HasValue())
            return Core::Unexpected(arrows.Error());
        if (!arrows.Value())
            break;
        if (!TheChosenCellIsInReach(hero, state, range.Value()))
            continue;
        // 0x562995..0x5629b0 y 0x563e6c..0x563e92: el rumbo de c hacia +0x3a8.
        hero.angles.z = static_cast<float>(Render::Models::CreateAngle(hero.position.x, hero.position.y,
                                                                       hero.attackPosition.x, hero.attackPosition.y));
        Protocol::Skill::DurationMagicSend send{.skill = skill};
        if (skill == 0x36)
        {
            // 0x5629b6..0x562a69: la celda lógica de c (la Y antes que la X), hacia el blanco y la espalda.
            const std::uint8_t y = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
            const std::uint8_t x = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
            send.towards = TowardsTheChosen(state, x, y);
            const auto back = FtolByte((static_cast<double>(hero.angles.z) + static_cast<double>(HalfTurn)) *
                                           static_cast<double>(OneOver360) * static_cast<double>(ByteScale),
                                       "0x562a64");
            if (!back.HasValue())
                return Core::Unexpected(back.Error());
            send.back = back.Value();
        }
        // 0x562a6c..0x562a87 y 0x563e8a..0x563eab: la clave de 0x558eb0 con 0x7758588, o 0xffff sin él.
        std::int32_t key = 0xffff;
        if (state.weaponSkill.target != -1)
        {
            const auto chosen = ChosenKeyOf(world, SlotOf(world, hero), state.weaponSkill.target, context.controlDown);
            if (!chosen.HasValue())
                return Core::Unexpected(chosen.Error());
            key = chosen.Value();
        }
        send.key = static_cast<std::uint16_t>(static_cast<std::uint32_t>(key) & 0xffffu);
        // 0x562a8a..0x562af3 y 0x563eae..0x563f17: con "webzen" en el nombre del héroe publicado, no sale (sin freno).
        if (!NamedWebzen(world.Actors()[runtime.hero]))
        {
            // 0x562af5..0x563b78 y 0x563f19..0x564e84: 0x5767948, la celda lógica otra vez y el 1E.
            state.lastShotSkill = skill;
            send.y = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
            send.x = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
            const auto direction =
                skill == 0x36
                    ? FtolByte(static_cast<double>(hero.angles.z) * static_cast<double>(OneOver360) *
                                   static_cast<double>(ByteScale),
                               "0x563022")
                    : FtolByte(static_cast<double>(hero.angles.z) * static_cast<double>(TurnToByte), "0x5643f1");
            if (!direction.HasValue())
                return Core::Unexpected(direction.Error());
            send.direction = direction.Value();
            SendTheSkillPacket(context, Protocol::Skill::WriteDurationMagic(send));
        }
        if (auto shot =
                ShootTheArrow(hero, skill, skill == 0x36 ? std::uint8_t{0x36 - 0x34} : std::uint8_t{1}, context);
            !shot.HasValue())
            return Core::Unexpected(shot.Error());
        used = true;
    }
    return used;
}
} // namespace Client::Scenes
