#include "Scenes/HeroSkills.h"

#include "Core/Expected.h"
#include "Data/Tables/SkillTable.h"
#include "Protocol/Pet/PetOrders.h"
#include "Render/Models/Cloth.h"
#include "Render/Models/LogicalAccess.h"
#include "Scenes/ActorState.h"
#include "Scenes/GameMovementState.h"
#include "Scenes/HeroAttack.h"
#include "Scenes/HeroSkillCasts.h"
#include "Scenes/HeroSkillsElf.h"
#include "Scenes/HeroSkillsKnight.h"
#include "Scenes/HeroSkillsWizard.h"
#include "Scenes/OwnCharacterRecord.h"
#include "Scenes/WorldMap.h"
#include "Scenes/WorldRuntime.h"
#include "Scenes/WorldState.h"
#include "UI/GameUiState.h"
#include "World/OriginalFtol.h"
#include "World/ItemState.h"
#include "World/TerrainWalls.h"
#include "World/WorldTerrain.h"

#include <glm/ext/vector_float3.hpp>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Scenes
{
namespace
{
constexpr std::uint32_t FrozenHeroFlag = 0x1000;
constexpr std::uint32_t HiddenHeroFlag = 0x4000;
constexpr std::uint32_t PartySkillFlag = 0x200;
constexpr std::uint16_t PlayerType = 0x20a;
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t WizardClass = 0;
constexpr std::uint8_t KnightClass = 1;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t GladiatorClass = 3;
constexpr std::uint8_t LordClass = 4;
constexpr float TeleportFade = 0.1f;
constexpr float CellReciprocal = std::bit_cast<float>(std::uint32_t{0x3c23d70a});        // [0x6b86c0], 0,01
constexpr float SecondsPerMillisecond = std::bit_cast<float>(std::uint32_t{0x3a83126f}); // [0x6ba128], 0,001
constexpr double MaximumHoldSeconds = 3600.0;                                            // [0x6ba43c]
constexpr std::int32_t SpectatorMap = 6;
constexpr std::int32_t FirstBloodCastleMap = 11;
constexpr std::int32_t LastBloodCastleMap = 17;
constexpr std::int32_t FirstChaosCastleMap = 18;
constexpr std::int32_t LastChaosCastleMap = 23;
constexpr std::int32_t CastleSiegeMap = 30;
constexpr std::uint8_t ChargedSkill = 0x28;
constexpr std::uint8_t ReleasedChargedSkill = 0x3a;
constexpr std::size_t PartyOffset = 0x31; // Skill.bmd: con 2, la skill pide 0x200 en +0x78
constexpr std::size_t WeaponHands = 2;    // 0x57d74c: +0xb6c y +0xbb4

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

bool InChaosCastle(std::int32_t map)
{
    return InRange(map, FirstChaosCastleMap, LastChaosCastleMap);
}

// 0x57d1bc..0x57d1d7 y 0x57d3ab..0x57d3c9: el ataque automático (0x6c8e3c), fuera del mapa 6 y de Chaos Castle.
bool AutoAttacksHere(const WorldRuntime& runtime)
{
    return runtime.ui.autoAttack && runtime.map != SpectatorMap && !InChaosCastle(runtime.map);
}

// 0x57d369..0x57d3a5: lo que se usa desde la zona segura de Blood Castle y Chaos Castle.
bool UsableInTheSafeZone(std::uint8_t skill)
{
    switch (skill)
    {
    case 0x1a:
    case 0x1b:
    case 0x1c:
    case 0x10:
    case 0x30:
    case 0x35:
    case 0x40:
    case 0x3f:
    case 0x4b:
        return true;
    default:
        return InRange(skill, 0x43, 0x48);
    }
}

// 0x57d3cf..0x57d458: con el ataque automático, el otro elegido (0x6c8e38) por la clase y la skill: −1 con las que no
// lo siguen y 2 con las demás.
std::int32_t AutoTargetOf(std::uint8_t heroClass, std::uint8_t skill)
{
    constexpr std::int32_t NoAutoTarget = -1;
    constexpr std::int32_t FollowsTheChosen = 2;
    if (heroClass == ElfClass)
    {
        if (skill != 0x18 && skill != 0x19 && skill != 0x34 && skill != 0x36 && skill != 0x4c)
            return NoAutoTarget;
    }
    else if (heroClass == KnightClass)
    {
        if (skill == 0x30)
            return NoAutoTarget;
    }
    else if (heroClass == LordClass)
    {
        if (skill == 0x40 || skill == 0x3f)
            return NoAutoTarget;
    }
    else if (heroClass == WizardClass)
    {
        if (skill == 0x3a || skill == 0x28)
            return NoAutoTarget;
    }
    // 0x57d42f..0x57d452: las 0x43..0x48 y, por el SBB, la 0x4b.
    if (InRange(skill, 0x43, 0x48) || skill == 0x4b)
        return NoAutoTarget;
    return FollowsTheChosen;
}

// 0x57d45d..0x57d4b0: la acción del héroe (+0x111). Un jugador, de 1 a 12, 0x8a, 0x44, 0x48, 0x4a o 0x76 a 0x79; si
// no, nada y, si es jugador, los frames del derecho (0x7d368a4) en 0. Otro tipo, la 0 o la 1.
enum class ActionGate : std::uint8_t
{
    Passes,
    Stops,
    StopsAndForgetsTheHold,
};

ActionGate GateOfTheAction(const ActorState& hero)
{
    const std::uint8_t action = hero.animation.action;
    if (hero.type != PlayerType)
        return action > 1 ? ActionGate::Stops : ActionGate::Passes;
    if (InRange(action, 1, 0xc) || action == 0x8a || action == 0x44 || action == 0x48 || action == 0x4a ||
        InRange(action, 0x76, 0x79))
        return ActionGate::Passes;
    return ActionGate::StopsAndForgetsTheHold;
}

bool Mounted(const ActorState& hero)
{
    const std::uint16_t model = hero.pet.model;
    return model == 0x1c14 || model == 0x1c15 || model == 0x1c16 || model == 0x1c37;
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

std::uint8_t ReadCell(std::uint8_t& depth, std::uint8_t cell, Render::Models::CrtRandom& random)
{
    const Render::Models::LogicalAccess access(depth, random, Render::Models::SmallLogicalCloseRandoms);
    return cell;
}

// 0x57d2da..0x57d307: el byte +0x31 de la entrada (0x5aea30 la abre y 0x4e7c40 la cierra).
Core::Expected<std::uint8_t, std::string> PartyByteOf(std::uint8_t skill, const HeroSkillContext& context)
{
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("0x57d2f1: Skill.bmd no tiene la skill {}", skill));
    const auto entry = EntryAccess(context, skill);
    return std::to_integer<std::uint8_t>((*record)[PartyOffset]);
}

// 0x5a5540(skill): las del asedio (0x2c, 0x2d, 0x2e, 0x39, 0x49 y 0x4a) solo en el mapa 30 con el asedio (0x4294b0).
bool UsableOnThisMap(std::uint8_t skill, const WorldRuntime& runtime)
{
    const bool siegeSkill =
        skill == 0x2c || skill == 0x2d || skill == 0x2e || skill == 0x39 || skill == 0x49 || skill == 0x4a;
    return !siegeSkill || (runtime.map == CastleSiegeMap && runtime.siegeActive);
}

// 0x420f70(héroe, lugar): las órdenes del cuervo. Sin cuervo (+0x430), sin el clic ni el botón derecho o con un lugar
// fuera de 0x78 a 0x7b, 0. Con el 0x7b (0x420fd6..0x42100f), si 0x558bb0 deja atacar al elegido y es un jugador (1) o
// un monstruo (2) en +0x88, el A7 con la orden 3 y su clave (+0x1f4); con los otros, el A7 con el lugar − 0x78 y la
// clave 0xffff (0x421599..0x4219cd). La mascota es el +0x444 del cuervo, siempre 0 (Protocol::Pet::RavenPet). Por
// send() en línea; sin el socket (+0xc de 0x6c82a0 en −1), no sale. Después, con el A7 o sin él, el soltado, el clic,
// el botón y los frames del derecho en 0 y 1 (0x421aff..0x421b20). Sin rand().
Core::Expected<bool, std::string> UseTheRavenOrder(const ActorState& hero, std::uint8_t slot,
                                                   const HeroSkillContext& context)
{
    constexpr std::uint8_t FirstOrderSlot = 0x78;
    constexpr std::uint8_t AttackOrderSlot = 0x7b;
    constexpr std::uint8_t AttackCommand = 3;
    constexpr std::uint8_t PlayerObject = 1;
    constexpr std::uint8_t MonsterObject = 2;
    WorldRuntime& runtime = context.world.Runtime();
    UI::GameUiState& ui = runtime.ui;
    if (!hero.darkSpirit || (!ui.rightClick && !ui.rightDown) || !InRange(slot, FirstOrderSlot, AttackOrderSlot))
        return false;
    std::optional<Protocol::Pet::PetCommand> command;
    if (slot == AttackOrderSlot)
    {
        const auto attackable = IsAttackable(context.world, context.controlDown);
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        // 0x558bb0 ya miró el −1 y el lugar del elegido.
        if (attackable.Value())
        {
            const ActorState& chosen = context.world.Actors()[static_cast<std::size_t>(runtime.selectedTargets[2])];
            if (chosen.byte88 == MonsterObject || chosen.byte88 == PlayerObject)
                command = Protocol::Pet::PetCommand{
                    .pet = Protocol::Pet::RavenPet, .command = AttackCommand, .key = chosen.key};
        }
    }
    else
    {
        command = Protocol::Pet::PetCommand{.pet = Protocol::Pet::RavenPet,
                                            .command = static_cast<std::uint8_t>(slot - FirstOrderSlot),
                                            .key = Protocol::Pet::NoTargetKey};
    }
    if (command && context.send)
        context.send(Protocol::Pet::WritePetCommand(*command));
    ui.rightReleased = false;
    ui.rightClick = false;
    ui.rightDown = false;
    ui.rightHoldFrames = 0;
    return true;
}

// 0x57ce80(héroe): con un elegido (0x6c8e30), su celda (ftol de la posición · 0,01) en 0x77e1ba8/0x77e1bac y su
// posición en +0x3a8..+0x3b0; sin él, el punto del terreno bajo el mouse y su celda, si lo hay. El resultado no lo
// mira nadie (0x57d4c7).
Core::Expected<void, std::string> AimTheSkill(ActorState& hero, const HeroSkillContext& context)
{
    WorldRuntime& runtime = context.world.Runtime();
    GameMovementState& state = runtime.movement;
    glm::vec3 point{};
    const std::int32_t chosen = runtime.selectedTargets[2];
    if (chosen != -1)
    {
        if (chosen < 0 || static_cast<std::size_t>(chosen) >= context.world.Actors().size())
            return Core::Unexpected(std::string("0x57ce8e: el elegido está fuera de la tabla de personajes"));
        point = context.world.Actors()[static_cast<std::size_t>(chosen)].position;
    }
    else
    {
        if (!context.pickTerrainPoint)
            return Core::Unexpected(std::string("0x57ceea: 0x5fc260(1) y 0x5fac90 sin el rayo del mouse"));
        const auto picked = context.pickTerrainPoint();
        if (!picked.HasValue())
            return Core::Unexpected(picked.Error());
        if (!picked.Value())
            return {};
        point = *picked.Value();
    }
    const auto x = World::OriginalFtolLow(static_cast<double>(point.x) * static_cast<double>(CellReciprocal));
    const auto y = World::OriginalFtolLow(static_cast<double>(point.y) * static_cast<double>(CellReciprocal));
    if (!x || !y)
        return Core::Unexpected(std::string("0x57ceaa: la celda del blanco fuera del perfil ftol"));
    state.targetX = std::bit_cast<std::uint32_t>(*x);
    state.targetY = std::bit_cast<std::uint32_t>(*y);
    hero.attackPosition = point;
    return {};
}

// 0x559890(héroe, ítem): la skill del arma de las clases 1, 3 y 4. Con lo que la frena, 0; las de la lista van a
// 0x559afc (UseTheListedWeaponSkill) y las demás a las opciones del arma (UseTheWeaponOptions, HeroSkillCasts).
Core::Expected<bool, std::string> UseTheWeaponSkill(ActorState& hero, const World::ItemState& item,
                                                    const HeroSkillContext& context)
{
    const std::uint8_t action = hero.animation.action;
    if (hero.type == PlayerType)
    {
        if (action == 0x98 || InRange(action, 0x38, 0x3b) || action == 0x8d)
            return false;
    }
    else if (InRange(action, 3, 4))
    {
        return false;
    }
    WorldRuntime& runtime = context.world.Runtime();
    std::uint8_t skill = 0;
    {
        const auto own = OwnAccess(context);
        skill = OwnSkillByte(context.world.OwnCharacter(), runtime.movement.weaponSkill.slot);
    }
    // 0x5599db..0x559afa: las skills de la lista y los tipos del arma derecha del héroe publicado (+0x2b8, MOVSX).
    const ActorState& published = context.world.Actors()[runtime.hero];
    const auto right = static_cast<std::int32_t>(static_cast<std::int16_t>(published.hands[0].model));
    bool special = false;
    switch (skill)
    {
    case 0x31:
    case 0x3c:
    case 0x3d:
    case 0x2c:
    case 0x4a:
        special = true;
        break;
    case 0x2b:
        if (hero.type != PlayerType || right == -1 || InRange(right, 0xc12, 0xe11))
            return false;
        special = true;
        break;
    case 0x2f:
        if (Mounted(published))
        {
            if (hero.type != PlayerType || right == -1 || right < 0x812 || right >= 0xa12)
                return false;
            special = true;
        }
        break;
    case 0x39:
        if (right < 0x212 || right >= 0x412)
            return false;
        special = true;
        break;
    default:
        break;
    }
    if (special)
        return UseTheListedWeaponSkill(hero, item, skill, context);
    return UseTheWeaponOptions(hero, item, context);
}

// 0x57d532..0x57d758: por cada mano (+0xb6c y +0xbb4), la skill de su arma. La tabla 0x599d70 (por la skill − 0x2b)
// deja montado solo las 0x2b, 0x2f, 0x31, 0x3c a 0x3e, 0x41, 0x4a y 0x4c. True: la usó (0x57df46, el registro cerrado
// y nada más).
Core::Expected<bool, std::string> UseTheWeaponSkills(ActorState& hero, std::uint8_t heroClass, std::uint8_t skill,
                                                     const HeroSkillContext& context)
{
    static constexpr std::array<std::uint8_t, 0x22> MountedSkills{0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                                                  0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0};
    WorldRuntime& runtime = context.world.Runtime();
    for (std::size_t hand = 0; hand < WeaponHands; ++hand)
    {
        if (heroClass == KnightClass || heroClass == GladiatorClass || heroClass == LordClass)
        {
            const std::uint32_t index = static_cast<std::uint32_t>(skill) - 0x2bu;
            const bool skipped = Mounted(hero) && (index >= MountedSkills.size() || MountedSkills[index] != 0);
            if (!skipped)
            {
                // 0x57d582..0x57d61b: el registro abierto, 0x7758580 en 1, el lugar y el elegido si se ataca.
                const auto own = OwnAccess(context);
                WeaponSkillRequest& request = runtime.movement.weaponSkill;
                request.active = 1;
                request.slot = context.world.Actors()[runtime.hero].skillIndex;
                const auto attackable = IsAttackable(context.world, context.controlDown);
                if (!attackable.HasValue())
                    return Core::Unexpected(attackable.Error());
                request.target = attackable.Value() ? runtime.selectedTargets[2] : -1;
                const auto used = UseTheWeaponSkill(hero, context.world.OwnCharacter().equipment[hand], context);
                if (!used.HasValue() || used.Value())
                    return used;
            }
        }
        if (heroClass == ElfClass)
        {
            const auto own = OwnAccess(context);
            const auto used = UseTheElfWeaponOptions(hero, context.world.OwnCharacter().equipment[hand], context);
            if (!used.HasValue() || used.Value())
                return used;
        }
    }
    return false;
}

// 0x57d787..0x58188e y 0x5897aa..0x5897b1: la rama de la clase. Las 0x43 a 0x48 (0x5808da) son de todas.
Core::Expected<void, std::string> UseTheClassSkill(ActorState& hero, std::uint8_t heroClass, std::uint8_t skill,
                                                   const HeroSkillContext& context)
{
    if (heroClass == ElfClass)
        return UseTheElfSkill(hero, skill, context);
    if (heroClass == KnightClass || heroClass == GladiatorClass || heroClass == LordClass)
    {
        if (auto used = UseTheKnightSkill(hero, heroClass, skill, context); !used.HasValue())
            return used;
        // 0x5897a7..0x5897b1: el Magic Gladiator sigue en la rama del Wizard, que repite la prueba de la clase.
        if (heroClass == GladiatorClass)
            return UseTheWizardSkill(hero, skill, context);
    }
    if (heroClass == WizardClass)
        return UseTheWizardSkill(hero, skill, context);
    // 0x5808da..0x5808e9: fuera de 0x43 a 0x48, nada.
    if (!InRange(skill, 0x43, 0x48))
        return {};
    return Core::Unexpected(std::format("0x5808ef: la skill {:#x} (0x43 a 0x48) es de la fase 2c", skill));
}

// 0x57d12c..0x57d1b8: el derecho. Con los frames de la carga (0x7d368a4), el soltado la usa y si no, cuentan; si no,
// el clic o el botón abajo la usan y la 0x28 empieza a cargarse como 0x3a. Devuelve si la usa y deja la skill.
enum class RightButton : std::uint8_t
{
    Idle,
    Uses,
    Leaves,
};

RightButton StepTheRightButton(ActorState& hero, std::uint8_t& skill, WorldState& world)
{
    UI::GameUiState& ui = world.Runtime().ui;
    if (ui.rightHoldFrames != 0)
    {
        if (!ui.rightReleased)
        {
            ++ui.rightHoldFrames;
            return RightButton::Idle;
        }
        ui.rightReleased = false;
        ui.rightClick = false;
        ui.rightDown = false;
        ui.rightHoldFrames = 0;
        return RightButton::Uses;
    }
    if (!ui.rightClick && !ui.rightDown)
        return RightButton::Idle;
    if (skill == ChargedSkill)
    {
        if (hero.byte80 != 3 && hero.byte80 != 0)
            return RightButton::Leaves;
        skill = ReleasedChargedSkill;
        ui.rightHoldFrames = 1;
        world.Actors()[world.Runtime().hero].byte1bc = 0;
    }
    ui.rightClick = false;
    return RightButton::Uses;
}
} // namespace

Core::Expected<void, std::string> UseTheHeroSkill(ActorState& hero, const HeroSkillContext& context)
{
    WorldState& world = context.world;
    WorldRuntime& runtime = world.Runtime();
    GameMovementState& state = runtime.movement;
    UI::GameUiState& ui = runtime.ui;
    // 0x57cfe2..0x57d00f.
    if (state.inputBlocked || ui.mouseOnUi != 0 || (hero.flags78 & FrozenHeroFlag) == FrozenHeroFlag)
        return {};
    const auto heroClass = static_cast<std::uint8_t>(hero.characterClass & ClassMask);
    if (hero.byte80 == 1)
    {
        // 0x57d02b..0x57d04a: fst conserva el resultado extendido para la comparación.
        const double faded = static_cast<double>(hero.alpha) - static_cast<double>(TeleportFade);
        hero.alpha = static_cast<float>(faded);
        if (faded < static_cast<double>(TeleportFade))
            hero.byte80 = 2;
    }
    // 0x57d051..0x57d10d: la skill del lugar +0x425 del héroe publicado, con el registro abierto.
    std::uint8_t skill = 0;
    {
        const auto own = OwnAccess(context);
        skill = OwnSkillByte(world.OwnCharacter(), world.Actors()[runtime.hero].skillIndex);
    }
    // 0x57d112: el alcance queda en un local que leen las ramas de las clases.
    const auto range = SkillRangeOf(skill, hero, context);
    if (!range.HasValue())
        return Core::Unexpected(range.Error());
    state.selectedSkillRange = range.Value();
    bool uses = false;
    if (state.movementBlocked)
    {
        // 0x57d261..0x57d282: trabado, solo el clic izquierdo (el flanco se gasta) o el botón abajo, sin el reloj.
        if (!state.pendingClick && !state.leftHeld)
            return {};
        state.pendingClick = false;
        uses = true;
    }
    else
    {
        const RightButton right = StepTheRightButton(hero, skill, world);
        if (right == RightButton::Leaves)
            return {};
        uses = right == RightButton::Uses;
        // 0x57d1bc..0x57d1ef: el ataque automático con el otro elegido (0x6c8e38) en 2 y un elegido.
        if (AutoAttacksHere(runtime) && runtime.selectedTargets[4] == 2 && runtime.selectedTargets[2] != -1)
            uses = true;
        if (!uses)
        {
            // 0x57d23c..0x57d260.
            state.actionInputTime = context.worldTime;
            state.actionInputElapsed = 0.0f;
            return {};
        }
        // 0x57d1f8..0x57d22d: desde la última vez sin uso, en s; a las 3.600 suelta el derecho.
        const double elapsed = (static_cast<double>(context.worldTime) - static_cast<double>(state.actionInputTime)) *
                               static_cast<double>(SecondsPerMillisecond);
        state.actionInputElapsed = static_cast<float>(elapsed);
        if (!(elapsed < MaximumHoldSeconds))
        {
            ui.rightClick = false;
            ui.rightDown = false;
            return {};
        }
    }
    // 0x57d28f..0x57d2c3.
    state.followTarget = -1;
    const ActorState& published = world.Actors()[runtime.hero];
    if ((published.flags78 & HiddenHeroFlag) != HiddenHeroFlag)
    {
        const auto ordered = UseTheRavenOrder(hero, published.skillIndex, context);
        if (!ordered.HasValue())
            return Core::Unexpected(ordered.Error());
        if (ordered.Value())
            return {};
    }
    if (!UsableOnThisMap(skill, runtime))
        return {};
    const auto party = PartyByteOf(skill, context);
    if (!party.HasValue())
        return Core::Unexpected(party.Error());
    if (party.Value() == 2 && (hero.flags78 & PartySkillFlag) != PartySkillFlag)
        return {};
    if (!uses || hero.dying != 0)
        return {};
    // 0x57d33d..0x57d3a5: en la zona segura, solo en Blood Castle o Chaos Castle y con las skills de la lista.
    if (hero.safeZone != 0)
    {
        if (!InRange(runtime.map, FirstBloodCastleMap, LastBloodCastleMap) && !InChaosCastle(runtime.map))
            return {};
        if (!UsableInTheSafeZone(skill))
            return {};
    }
    if (AutoAttacksHere(runtime))
        runtime.selectedTargets[4] = AutoTargetOf(heroClass, skill);
    switch (GateOfTheAction(hero))
    {
    case ActionGate::Passes:
        break;
    case ActionGate::StopsAndForgetsTheHold:
        ui.rightHoldFrames = 0;
        return {};
    case ActionGate::Stops:
        return {};
    }
    if (heroClass != WizardClass)
    {
        // 0x57d4be..0x57d51f: el blanco y, desde la celda lógica (la Y antes que la X), si se lo ve.
        if (auto aimed = AimTheSkill(hero, context); !aimed.HasValue())
            return aimed;
        const auto y = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
        const auto x = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
        const World::WorldTerrain& terrain = world.Map().terrain;
        if (!terrain.attributes)
            return Core::Unexpected(std::string("0x558800: falta TerrainWall para ver el blanco de la skill"));
        const auto seen =
            World::CanSeeTheCell(terrain.attributes->cells, x, y, std::bit_cast<std::int32_t>(state.targetX),
                                 std::bit_cast<std::int32_t>(state.targetY));
        if (!seen)
            return Core::Unexpected(std::string("0x55886d: la línea hasta el blanco de la skill sale de TerrainWall"));
        if (*seen)
        {
            const auto used = UseTheWeaponSkills(hero, heroClass, skill, context);
            if (!used.HasValue())
                return Core::Unexpected(used.Error());
            if (used.Value())
                return {};
        }
        else
        {
            // 0x57d760..0x57d774: los tres en 0 y el elegido en −1.
            state.weaponSkill = WeaponSkillRequest{.active = 0, .slot = 0, .target = -1};
        }
    }
    return UseTheClassSkill(hero, heroClass, skill, context);
}
} // namespace Client::Scenes
