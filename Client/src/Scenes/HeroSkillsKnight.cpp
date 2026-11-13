#include "Scenes/HeroSkillsKnight.h"

#include "Core/Expected.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "GameLogic/PathFinder.h"
#include "Protocol/Outgoing.h"
#include "Protocol/Skill.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Render/Models/OriginalAngles.h"
#include "Scenes/ActorName.h"
#include "Scenes/ActorState.h"
#include "Scenes/CombatActionSpeeds.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/HeroSkills.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/Skill4e9920.h"
#include "Scenes/Skill4e9c50.h"
#include "Scenes/Skill551c70.h"
#include "Scenes/Skill551f40.h"
#include "Scenes/Skill57cf90.h"
#include "Scenes/SkillDelays.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "World/OriginalFtol.h"
#include "World/WorldTerrain.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace Client::Scenes
{
namespace
{
constexpr std::uint16_t PlayerType = 0x20a;
constexpr std::uint8_t LordClass = 4;
constexpr std::int16_t NoWeapon = -1;
constexpr std::int16_t FirstShield = 0xc12;
constexpr std::int16_t ShieldsEnd = 0xe12;
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DarkHorseModel = 0x1c16;
constexpr std::uint16_t FenrirModel = 0x1c37;
constexpr std::size_t DarkHorseSlot = 8; // [0x7681eb4] + 0xb6c + 8 · 0x48 (+0x1a, la durabilidad: +0xdc6)
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
constexpr std::uint16_t NoKey = 0xffff;
constexpr std::string_view GameMasterName = "webzen"; // 0x6c915c..0x6c91a4, uno por skill
constexpr std::int32_t MinimumOffset = -8;
constexpr std::int32_t MaximumOffset = 7;
constexpr std::uint8_t OffsetBias = 8;
constexpr unsigned HighNibble = 4;
constexpr std::uint8_t LowNibbleMask = 0xf;
constexpr double CellSize = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x42c80000}));       // [0x6b86b4]
constexpr double HalfCell = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x42480000}));       // [0x6b86b0]
constexpr double CellReciprocal = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3c23d70a})); // [0x6b86c0]
constexpr double TurnsOf256 = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3f360b61}));     // [0x6ba440]
constexpr double TurnFraction = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3b360b61}));   // [0x6ba444]
constexpr double ByteScale = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x437f0000}));      // [0x6b9f98]
constexpr double HalfTurn = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x43340000}));       // [0x6b86bc]
constexpr double WalkReach = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3f99999a}));      // [0x6b8bb0]

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

Render::Models::LogicalAccess OwnAccess(const HeroSkillContext& context)
{
    return Render::Models::LogicalAccess(context.world.Runtime().ownRecordDepth, context.random,
                                         Render::Models::OwnRecordCloseRandoms);
}

SkillDelayContext DelaysOf(const HeroSkillContext& context)
{
    return SkillDelayContext{.world = context.world, .random = context.random, .skills = context.skills};
}

const ActorState& PublishedHero(const HeroSkillContext& context)
{
    return context.world.Actors()[context.world.Runtime().hero];
}

// 0x558eb0 compara el puntero con [0x743a170]: el lugar de `hero` en la tabla, o uno de más si no está.
std::size_t SlotOf(const WorldState& world, const ActorState& hero)
{
    const auto actors = world.Actors();
    const std::less<const ActorState*> before;
    if (before(&hero, actors.data()) || !before(&hero, actors.data() + actors.size()))
        return actors.size();
    return static_cast<std::size_t>(&hero - actors.data());
}

// 0x581912..0x58197a: sin arma derecha, o con un escudo (0xc12 a 0xe11, MOVSX) en una de las manos del héroe publicado,
// solo siguen las skills de la lista.
bool TheHandsAllow(const ActorState& published, std::uint8_t skill)
{
    const auto right = static_cast<std::int16_t>(published.hands[0].model);
    const auto left = static_cast<std::int16_t>(published.hands[1].model);
    const auto shield = [](std::int16_t model) { return model >= FirstShield && model < ShieldsEnd; };
    if (right != NoWeapon && !shield(right) && !shield(left))
        return true;
    switch (skill)
    {
    case 0x30:
    case 0x40:
    case 0x3f:
    case 0x41:
    case 0x3e:
    case 0x4b:
    case 0x4c:
        return true;
    default:
        return false;
    }
}

// 0x5819bc..0x581a6b y 0x581abb..0x581b6a: la durabilidad del caballo (+0xdc6), con el registro ya abierto.
std::uint8_t HorseDurability(const HeroSkillContext& context)
{
    const auto own = OwnAccess(context);
    return context.world.OwnCharacter().equipment[DarkHorseSlot].durability;
}

// 0x58197f..0x581b6c: [ebp−0x3d], si se puede usar, antes del maná.
bool UsableHere(std::uint8_t skill, const ActorState& published, const HeroSkillContext& context)
{
    const WorldRuntime& runtime = context.world.Runtime();
    bool usable = true;
    if (skill == 0x3f)
        usable = runtime.hud.partyCount > 0;
    else if (skill == 0x4c)
        usable = published.pet.model == FenrirModel;
    else if (skill == 0x3e && HorseDurability(context) == 0)
        usable = false;
    // 0x581a71..0x581ab1: en Chaos Castle, las de la lista no; afuera, la 0x3e mira el caballo otra vez.
    if (InRange(runtime.map, FirstChaosCastleMap, LastChaosCastleMap))
    {
        if (skill == 0x3e || skill == 0x31 || InRange(skill, 0x78, 0x7b) || skill == 0x4c)
            usable = false;
    }
    else if (skill == 0x3e && HorseDurability(context) == 0)
    {
        usable = false;
    }
    return usable;
}

// 0x581b70..0x58236f: el maná, la stamina, la espera, la energía y, el Dark Lord, el liderazgo. Vacío: termina sin
// mirar si se podía (0x582364).
Core::Expected<std::optional<bool>, std::string> PaysForTheSkill(std::uint8_t heroClass, std::uint8_t skill,
                                                                 const SkillCosts& costs, bool usable,
                                                                 const HeroSkillContext& context)
{
    const SkillDelayContext delays = DelaysOf(context);
    const OwnCharacterRecord& own = context.world.OwnCharacter();
    if (costs.mana > static_cast<std::int32_t>(own.mana))
        return Core::Unexpected(std::format("0x581b88: la skill {:#x} sin maná busca la poción con 0x558110(3), que es "
                                            "de la parte compartida de la fase 2c",
                                            skill));
    if (usable)
    {
        if (costs.stamina > static_cast<std::int32_t>(own.bp))
        {
            usable = false;
        }
        else
        {
            const auto started = TryStartSkillDelay(PublishedHero(context).skillIndex, delays);
            if (!started.HasValue())
                return Core::Unexpected(started.Error());
            usable = started.Value();
        }
    }
    const auto energy = SkillEnergyRequirement(skill, delays);
    if (!energy.HasValue())
        return Core::Unexpected(energy.Error());
    if (energy.Value() > static_cast<std::int32_t>(own.word30) + static_cast<std::int32_t>(own.energy))
        usable = false;
    if (heroClass == LordClass)
    {
        const auto leadership = SkillDelayRequirement(skill, delays);
        if (!leadership.HasValue())
            return Core::Unexpected(leadership.Error());
        if (leadership.Value() > static_cast<std::int32_t>(own.word36) + static_cast<std::int32_t>(own.leadership))
            return std::optional<bool>();
    }
    return std::optional<bool>(usable);
}

struct Cells
{
    std::uint8_t x = 0;
    std::uint8_t y = 0;
};

// 0x40b570 y 0x40b800 sobre +0x420 y después sobre +0x41c: la Y primero.
Cells ReadTheCells(ActorState& hero, Render::Models::CrtRandom& random)
{
    Cells cells;
    {
        const Render::Models::LogicalAccess access(hero.cellYAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        cells.y = hero.cellY;
    }
    {
        const Render::Models::LogicalAccess access(hero.cellXAccesses, random,
                                                   Render::Models::SmallLogicalCloseRandoms);
        cells.x = hero.cellX;
    }
    return cells;
}

// 0x4e93f0(+0x10, +0x14, +0x3a8, +0x3ac) en +0x24.
void FaceTheTarget(ActorState& hero)
{
    hero.angles.z = static_cast<float>(
        Render::Models::CreateAngle(hero.position.x, hero.position.y, hero.attackPosition.x, hero.attackPosition.y));
}

// 0x583f12..0x583f63 (y 0x586436, 0x587aab y 0x588899): la distancia del héroe al centro de la celda del blanco
// (0x77e1ba8 y 0x77e1bac) contra el alcance (el local de 0x5526f0) · 100; con NaN, en alcance (TEST AH, 0x41).
bool OutOfRange(const ActorState& hero, const GameMovementState& state)
{
    const double dx = static_cast<double>(hero.position.x) -
                      (static_cast<double>(std::bit_cast<std::int32_t>(state.targetX)) * CellSize + HalfCell);
    const double dy = static_cast<double>(hero.position.y) -
                      (static_cast<double>(std::bit_cast<std::int32_t>(state.targetY)) * CellSize + HalfCell);
    const double distance = std::sqrt(dx * dx + dy * dy);
    return distance > static_cast<double>(state.selectedSkillRange) * CellSize;
}

std::int32_t Minus(std::int32_t left, std::int32_t right)
{
    return std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(left) - static_cast<std::uint32_t>(right));
}

// 0x58314f..0x58319c: la celda del blanco menos la del héroe, cada una entre −8 y 7: arriba la X + 8 y abajo la Y − 8,
// con el AND.
std::uint8_t TargetPosition(std::int32_t dx, std::int32_t dy)
{
    const auto x = static_cast<std::uint8_t>(std::clamp(dx, MinimumOffset, MaximumOffset));
    const auto y = static_cast<std::uint8_t>(std::clamp(dy, MinimumOffset, MaximumOffset));
    const auto high = static_cast<std::uint8_t>(static_cast<std::uint8_t>(x + OffsetBias) << HighNibble);
    const auto low = static_cast<std::uint8_t>(static_cast<std::uint8_t>(y - OffsetBias) & LowNibbleMask);
    return static_cast<std::uint8_t>(high | low);
}

Core::Expected<std::int32_t, std::string> Ftol(double value, std::string_view where)
{
    const auto truncated = World::OriginalFtolLow(value);
    if (!truncated)
        return Core::Unexpected(std::format("{}: fuera del perfil de ftol", where));
    return *truncated;
}

Core::Expected<std::uint8_t, std::string> LowByte(double value, std::string_view where)
{
    const auto truncated = Ftol(value, where);
    if (!truncated.HasValue())
        return Core::Unexpected(truncated.Error());
    return static_cast<std::uint8_t>(static_cast<std::uint32_t>(truncated.Value()));
}

// El rumbo del 1E: +0x24 · 256/360.
Core::Expected<std::uint8_t, std::string> DirectionOf(const ActorState& hero, std::string_view where)
{
    return LowByte(static_cast<double>(hero.angles.z) * TurnsOf256, where);
}

// 0x41: la celda del blanco (+0x3a8 y +0x3ac · 0,01).
Core::Expected<std::int32_t, std::string> CellOf(float coordinate, std::string_view where)
{
    return Ftol(static_cast<double>(coordinate) * CellReciprocal, where);
}

// 0x4e9920(+0x10, +0x14, _ftol(+0x3a8), _ftol(+0x3ac)): la celda relativa, con las del blanco sin el · 0,01.
Core::Expected<std::uint8_t, std::string> RelativeCellOf(const ActorState& hero, std::string_view where)
{
    const auto y = Ftol(static_cast<double>(hero.attackPosition.y), where);
    if (!y.HasValue())
        return Core::Unexpected(y.Error());
    const auto x = Ftol(static_cast<double>(hero.attackPosition.x), where);
    if (!x.HasValue())
        return Core::Unexpected(x.Error());
    const auto cell = RelativeTargetCell(hero.position.x, hero.position.y, x.Value(), y.Value());
    if (!cell)
        return Core::Unexpected(std::format("{}: 0x4e9920 fuera del perfil de ftol", where));
    return *cell;
}

// 0x558eb0(héroe, elegido): la clave, de la que el 1E manda los 16 bits de abajo.
Core::Expected<std::uint16_t, std::string> ChosenKey(const ActorState& hero, std::int32_t target,
                                                     const HeroSkillContext& context)
{
    const auto key = ChosenKeyOf(context.world, SlotOf(context.world, hero), target, context.controlDown);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(key.Value()));
}

// 0x5823b1..0x5823ce: con el elegido de la skill del arma (0x7758588) en −1, 0xffff sin llamar a 0x558eb0.
Core::Expected<std::uint16_t, std::string> WeaponTargetKey(const ActorState& hero, const HeroSkillContext& context)
{
    const std::int32_t target = context.world.Runtime().movement.weaponSkill.target;
    if (target == -1)
        return NoKey;
    return ChosenKey(hero, target, context);
}

// El strstr en línea con "webzen" sobre el nombre del héroe publicado (+0x1d9): con él, no manda.
bool SentByTheGameMaster(const HeroSkillContext& context)
{
    return ActorNameContains(PublishedHero(context), GameMasterName);
}

// El C1 armado con el XOR32 sale como C3: el relleno (0x69fd70) y el serial (0x40ae60 y 0x40b0a0) sacan dos rand()
// antes de mirar el socket (+0xc de 0x6c82a0); sin él, no sale.
void SendAsC3(Protocol::Outgoing packet, const HeroSkillContext& context)
{
    context.random.Discard(2);
    if (context.send)
        context.send(std::move(packet));
}

// El 1E (0x582446..0x582dd1 y los demás): 0x5767948 con la skill, las celdas lógicas del héroe (la Y antes que la X)
// y el C3. El rumbo y los bytes 7 a 11 no sacan rand(): cada rama los calcula antes.
struct DurationBytes
{
    std::uint8_t direction = 0;
    std::uint8_t byte7 = 0;
    std::uint8_t byte8 = 0;
    std::uint16_t key = 0;
    std::uint8_t byte11 = 0;
};

void SendTheDurationMagic(ActorState& hero, std::uint8_t skill, const DurationBytes& bytes,
                          const HeroSkillContext& context)
{
    context.world.Runtime().movement.sentSkill = skill;
    const Cells cells = ReadTheCells(hero, context.random);
    SendAsC3(Protocol::Skill::WriteDurationMagic({.skill = skill,
                                                  .x = cells.x,
                                                  .y = cells.y,
                                                  .direction = bytes.direction,
                                                  .byte7 = bytes.byte7,
                                                  .byte8 = bytes.byte8,
                                                  .key = bytes.key,
                                                  .byte11 = bytes.byte11}),
             context);
}

// El 19 (0x584dde..0x5854b4 y los demás): sin "webzen" y con el freno de 300 ms, la skill y la clave del jugador
// ([0x57678bc]).
Core::Expected<void, std::string> SendTheMagicAttack(std::uint8_t skill, const HeroSkillContext& context,
                                                     std::string_view where)
{
    if (SentByTheGameMaster(context))
        return {};
    if (!context.tickCount)
        return Core::Unexpected(std::format("{}: el freno de 300 ms sin GetTickCount", where));
    if (!PassesTheActionBrake(context.world.Runtime().movement.lastActionTick, skill, context.tickCount))
        return {};
    SendAsC3(Protocol::Skill::WriteMagicAttack(skill, context.playerKey), context);
    return {};
}

// 0x4ea030 con el esqueleto del jugador: el héroe de la rama es un 0x20a (0x581912).
Core::Expected<void, std::string> PutTheAction(ActorState& hero, std::uint8_t action, const HeroSkillContext& context)
{
    if (context.playerSkeleton == nullptr)
        return Core::Unexpected(std::format("0x4ea030: falta el esqueleto del jugador para la acción {:#x}", action));
    Render::Models::SetAction(hero.animation, *context.playerSkeleton, action);
    return {};
}

// 0x58885c..0x58886f: la acción y +0x378 en 0.
Core::Expected<void, std::string> ActAndStop(ActorState& hero, std::uint8_t action, const HeroSkillContext& context)
{
    if (auto put = PutTheAction(hero, action, context); !put.HasValue())
        return put;
    hero.movement = 0;
    return {};
}

// 0x58977e: fuera de alcance, el otro elegido (0x6c8e38) en −1.
Core::Expected<void, std::string> ForgetTheOtherTarget(const HeroSkillContext& context)
{
    context.world.Runtime().selectedTargets[4] = -1;
    return {};
}

// 0x582393..0x5830dc: la 0x29. El rumbo hacia el blanco, la clave y el 1E con 0 en los bytes 7 y 8; con "webzen", sin
// mandar. Las velocidades (0x4f3f80) y la acción 0x3d.
Core::Expected<void, std::string> UseSkill29(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
{
    FaceTheTarget(hero);
    const auto key = WeaponTargetKey(hero, context);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    if (!SentByTheGameMaster(context))
    {
        const auto direction = DirectionOf(hero, "0x5827d8");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheDurationMagic(hero, skill, {.direction = direction.Value(), .key = key.Value()}, context);
    }
    RefreshCombatActionSpeeds(context.world, context.random);
    return ActAndStop(hero, 0x3d, context);
}

// 0x587a8d..0x58886f: la 0x2a. El rumbo; fuera de alcance, el otro elegido en −1 y nada más. Si no, la celda relativa
// de 0x4e9920 en el byte 8, la clave y el 1E; la acción 0x3e, sin 0x4f3f80.
Core::Expected<void, std::string> UseSkill2a(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
{
    FaceTheTarget(hero);
    if (OutOfRange(hero, context.world.Runtime().movement))
        return ForgetTheOtherTarget(context);
    const auto relative = RelativeCellOf(hero, "0x587b2c");
    if (!relative.HasValue())
        return Core::Unexpected(relative.Error());
    const auto key = WeaponTargetKey(hero, context);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    if (!SentByTheGameMaster(context))
    {
        const auto direction = DirectionOf(hero, "0x587f5c");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheDurationMagic(hero, skill,
                             {.direction = direction.Value(), .byte8 = relative.Value(), .key = key.Value()}, context);
    }
    return ActAndStop(hero, 0x3e, context);
}

// 0x5830e1..0x583ece: la 0x37. El rumbo, las celdas (la primera lectura) y la posición del blanco en el byte 7.
Core::Expected<void, std::string> UseSkill37(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
{
    const GameMovementState& state = context.world.Runtime().movement;
    FaceTheTarget(hero);
    const Cells cells = ReadTheCells(hero, context.random);
    const std::uint8_t position = TargetPosition(Minus(std::bit_cast<std::int32_t>(state.targetX), cells.x),
                                                 Minus(std::bit_cast<std::int32_t>(state.targetY), cells.y));
    const auto key = WeaponTargetKey(hero, context);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    if (!SentByTheGameMaster(context))
    {
        const auto direction = DirectionOf(hero, "0x5835cc");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheDurationMagic(hero, skill, {.direction = direction.Value(), .byte7 = position, .key = key.Value()},
                             context);
    }
    RefreshCombatActionSpeeds(context.world, context.random);
    return ActAndStop(hero, 0x3d, context);
}

// 0x583ed3..0x584d6e: la 0x38. Montado (0x1c14 a 0x1c16 o Fenrir), nada; fuera de alcance, nada. El rumbo va en la
// escala de 255 (· 1/360 · 255) y el byte 8 es el rumbo opuesto ((+0x24 + 180) · 1/360 · 255). La acción, 0x8d; la 0x5d
// de Fenrir fuera de la zona segura (EBX, 0x58187f) no llega, porque Fenrir ya salió.
Core::Expected<void, std::string> UseSkill38(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
{
    const auto pet = static_cast<std::int16_t>(hero.pet.model);
    if ((pet >= static_cast<std::int16_t>(UniriaModel) && pet <= static_cast<std::int16_t>(DarkHorseModel)) ||
        pet == static_cast<std::int16_t>(FenrirModel))
        return {};
    const GameMovementState& state = context.world.Runtime().movement;
    FaceTheTarget(hero);
    if (OutOfRange(hero, state))
        return {};
    const Cells cells = ReadTheCells(hero, context.random);
    const std::uint8_t position = TargetPosition(Minus(std::bit_cast<std::int32_t>(state.targetX), cells.x),
                                                 Minus(std::bit_cast<std::int32_t>(state.targetY), cells.y));
    const auto opposite =
        LowByte((static_cast<double>(hero.angles.z) + HalfTurn) * TurnFraction * ByteScale, "0x584017");
    if (!opposite.HasValue())
        return Core::Unexpected(opposite.Error());
    const auto key = WeaponTargetKey(hero, context);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    if (!SentByTheGameMaster(context))
    {
        const auto direction = LowByte(static_cast<double>(hero.angles.z) * TurnFraction * ByteScale, "0x58444d");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheDurationMagic(
            hero, skill,
            {.direction = direction.Value(), .byte7 = position, .byte8 = opposite.Value(), .key = key.Value()},
            context);
    }
    RefreshCombatActionSpeeds(context.world, context.random);
    const bool fenrir = hero.pet.model == FenrirModel && hero.safeZone == 0;
    return ActAndStop(hero, fenrir ? 0x5d : 0x8d, context);
}

// 0x586419..0x587346: la 0x3e (en el caballo oscuro y fuera de la zona segura) y la 0x41. Fuera de alcance, el otro
// elegido en −1. La celda del blanco (+0x3a8 y +0x3ac · 0,01) contra la del héroe en el byte 7, la de 0x4e9920 en el
// 8, la clave, y el rumbo después. La 0x41 hace 0x51 en el caballo, 0x5b en Fenrir (fuera de la zona segura) o 0x94;
// la 0x3e, 0x53 y el sonido 0x1ce.
Core::Expected<void, std::string> UseSkill3eOr41(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
{
    if (skill == 0x3e && (hero.pet.model != DarkHorseModel || hero.safeZone != 0))
        return {};
    if (OutOfRange(hero, context.world.Runtime().movement))
        return ForgetTheOtherTarget(context);
    const auto targetX = CellOf(hero.attackPosition.x, "0x586497");
    if (!targetX.HasValue())
        return Core::Unexpected(targetX.Error());
    const auto targetY = CellOf(hero.attackPosition.y, "0x5864aa");
    if (!targetY.HasValue())
        return Core::Unexpected(targetY.Error());
    const Cells cells = ReadTheCells(hero, context.random);
    const std::uint8_t position = TargetPosition(Minus(targetX.Value(), cells.x), Minus(targetY.Value(), cells.y));
    const auto relative = RelativeCellOf(hero, "0x58657c");
    if (!relative.HasValue())
        return Core::Unexpected(relative.Error());
    const auto key = WeaponTargetKey(hero, context);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    FaceTheTarget(hero);
    if (!SentByTheGameMaster(context))
    {
        const auto direction = DirectionOf(hero, "0x5869d0");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        SendTheDurationMagic(
            hero, skill,
            {.direction = direction.Value(), .byte7 = position, .byte8 = relative.Value(), .key = key.Value()},
            context);
    }
    RefreshCombatActionSpeeds(context.world, context.random);
    if (skill == 0x3e)
    {
        if (auto put = PutTheAction(hero, 0x53, context); !put.HasValue())
            return put;
        context.world.Runtime().lastSoundId = 0x1ce; // 0x41ed10(0x1ce, 0, 0) (CL10)
        hero.movement = 0;
        return {};
    }
    std::uint8_t action = 0x94;
    if (hero.pet.model == DarkHorseModel && hero.safeZone == 0)
        action = 0x51;
    else if (hero.pet.model == FenrirModel && hero.safeZone == 0)
        action = 0x5b;
    return ActAndStop(hero, action, context);
}

// 0x58734b..0x587a88: la 0x30. El 19 y, mande o no, la acción 0x3f.
Core::Expected<void, std::string> UseSkill30(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
{
    if (auto sent = SendTheMagicAttack(skill, context, "0x5873ba"); !sent.HasValue())
        return sent;
    return ActAndStop(hero, 0x3f, context);
}

// 0x584d73..0x5854b7, 0x5854fd.. y 0x585c8a..: las 0x3f, 0x40 y 0x4b. El 19 y +0x378 en 0, sin acción.
Core::Expected<void, std::string> UseSkillWithTheMagicAttack(ActorState& hero, std::uint8_t skill,
                                                             const HeroSkillContext& context, std::string_view where)
{
    if (auto sent = SendTheMagicAttack(skill, context, where); !sent.HasValue())
        return sent;
    hero.movement = 0;
    return {};
}

// 0x589744..0x589779: fuera de alcance con un elegido, el camino hasta las celdas a menos de alcance · 1,2 (en float)
// de la celda del blanco (0x4eac00, sin bClearGoal), con la Y del héroe leída antes que la X. Con camino, +0x378 en 1.
Core::Expected<void, std::string> WalkTowardsTheTarget(ActorState& hero, std::int32_t targetX, std::int32_t targetY,
                                                       const HeroSkillContext& context)
{
    const Cells cells = ReadTheCells(hero, context.random);
    const auto reach =
        static_cast<float>(static_cast<double>(context.world.Runtime().movement.selectedSkillRange) * WalkReach);
    if (!InRange(targetX, 0, 0xff) || !InRange(targetY, 0, 0xff))
        return Core::Unexpected(
            std::format("0x58975f: la celda del blanco ({}, {}) de la 0x4c fuera del mapa", targetX, targetY));
    World::WorldTerrain& terrain = context.world.Map().terrain;
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x58975f: falta TerrainWall para acercarse con la 0x4c"));
    const auto path = GameLogic::PathFinder(terrain.attributes->cells)
                          .Find({cells.x, cells.y},
                                {static_cast<std::uint8_t>(targetX), static_cast<std::uint8_t>(targetY)}, false, reach);
    if (!path)
        return {};
    hero.path = *path;
    hero.movement = 1;
    return {};
}

// 0x588874..0x5896e5: la 0x4c (Fenrir). La celda del blanco (· 0,01); fuera de alcance, sin elegido de la skill del
// arma, el otro elegido en −1, y con él, el camino. En alcance, la celda de 0x4e9920; sin elegido, +0x434 en −1 y
// nada más (ni +0x378). Si no: el rumbo, la espera (0x552990), la clave (0x558eb0 sin mirar el −1), +0x434 con el
// elegido y el 1E con la clave de la magia (0x57cf90 en línea, en +0x1bd) en el byte 11; +0x378 en 0 y la acción de
// las manos del héroe (0x4e9c50).
Core::Expected<void, std::string> UseSkill4c(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
{
    GameMovementState& state = context.world.Runtime().movement;
    const auto targetX = CellOf(hero.attackPosition.x, "0x588880");
    if (!targetX.HasValue())
        return Core::Unexpected(targetX.Error());
    const auto targetY = CellOf(hero.attackPosition.y, "0x588894");
    if (!targetY.HasValue())
        return Core::Unexpected(targetY.Error());
    const std::int32_t target = state.weaponSkill.target;
    if (OutOfRange(hero, state))
    {
        if (target == -1)
            return ForgetTheOtherTarget(context);
        return WalkTowardsTheTarget(hero, targetX.Value(), targetY.Value(), context);
    }
    const auto relative = RelativeCellOf(hero, "0x58891c");
    if (!relative.HasValue())
        return Core::Unexpected(relative.Error());
    if (target == -1)
    {
        hero.skillTarget4c = -1;
        return {};
    }
    FaceTheTarget(hero);
    if (auto started = StartSkillDelay(PublishedHero(context).skillIndex, DelaysOf(context)); !started.HasValue())
        return started;
    const auto key = ChosenKey(hero, target, context);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    hero.skillTarget4c = target;
    if (!SentByTheGameMaster(context))
    {
        const auto direction = DirectionOf(hero, "0x588d91");
        if (!direction.HasValue())
            return Core::Unexpected(direction.Error());
        const std::uint8_t magicKey = NextMagicKey(state, hero.magicKey);
        SendTheDurationMagic(
            hero, skill,
            {.direction = direction.Value(), .byte8 = relative.Value(), .key = key.Value(), .byte11 = magicKey},
            context);
    }
    hero.movement = 0;
    if (hero.type != PlayerType)
        return {};
    return PutTheAction(hero, ActionByTheHands(hero), context);
}

// 0x58238c: la tabla 0x599e10/0x599de0 por la skill − 0x29 (hasta 0x23); las demás van a 0x5854be.
Core::Expected<void, std::string> UseTheSkill(ActorState& hero, std::uint8_t skill, const HeroSkillContext& context)
{
    switch (skill)
    {
    case 0x29:
        return UseSkill29(hero, skill, context);
    case 0x2a:
        return UseSkill2a(hero, skill, context);
    case 0x30:
        return UseSkill30(hero, skill, context);
    case 0x37:
        return UseSkill37(hero, skill, context);
    case 0x38:
        return UseSkill38(hero, skill, context);
    case 0x3e:
    case 0x41:
        return UseSkill3eOr41(hero, skill, context);
    case 0x3f:
        return UseSkillWithTheMagicAttack(hero, skill, context, "0x584de2");
    case 0x40:
        return UseSkillWithTheMagicAttack(hero, skill, context, "0x58556b");
    case 0x4b:
        return UseSkillWithTheMagicAttack(hero, skill, context, "0x585cf8");
    case 0x4c:
        return UseSkill4c(hero, skill, context);
    default:
        return {};
    }
}
} // namespace

Core::Expected<void, std::string> UseTheKnightSkill(ActorState& hero, std::uint8_t heroClass, std::uint8_t skill,
                                                    const HeroSkillContext& context)
{
    // 0x581894: el registro propio queda abierto hasta 0x5854be..0x5897a2, donde se cierra (3.848 rand() en 0).
    const auto own = OwnAccess(context);
    const auto costs = SkillCostsOf(skill, DelaysOf(context));
    if (!costs.HasValue())
        return Core::Unexpected(costs.Error());
    if (hero.type != PlayerType)
        return {};
    const ActorState& published = PublishedHero(context);
    if (!TheHandsAllow(published, skill))
        return {};
    const bool usable = UsableHere(skill, published, context);
    const auto pays = PaysForTheSkill(heroClass, skill, costs.Value(), usable, context);
    if (!pays.HasValue())
        return Core::Unexpected(pays.Error());
    if (!pays.Value() || !*pays.Value())
        return {};
    return UseTheSkill(hero, skill, context);
}
} // namespace Client::Scenes
