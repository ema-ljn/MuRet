#include "Skills/HeroSkills.h"
#include "Skills/Skill57ce80.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/MouseState.h"
#include "Kernel/CharacterClass.h"
#include "Tables/SkillTable.h"
#include "Maps/MapId.h"
#include "Protocol/Pets/PetOrders.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Movement/GameMovementState.h"
#include "Combat/Attackable.h"
#include "Skills/HeroSkillCasts.h"
#include "Skills/HeroSkillsElf.h"
#include "Skills/HeroSkillsKnight.h"
#include "Skills/HeroSkillsTail.h"
#include "Skills/HeroSkillsWizard.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Ui/GameUiState.h"
#include "Kernel/ItemState.h"
#include "Pathfinding/TerrainWalls.h"
#include "Terrain/WorldTerrain.h"
#include "Tables/PlayerModels.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace Client::Skills
{
namespace
{
constexpr std::uint32_t FrozenHeroFlag = 0x1000;
constexpr std::uint32_t HiddenHeroFlag = 0x4000;
constexpr std::uint32_t PartySkillFlag = 0x200;
constexpr std::uint16_t PlayerType = Tables::PlayerSkeletonModel;
constexpr float TeleportFade = 0.1f;
constexpr float SecondsPerMillisecond = std::bit_cast<float>(std::uint32_t{0x3a83126f}); // [0x6ba128], 0.001
constexpr double MaximumHoldSeconds = 3600.0;                                            // [0x6ba43c]
constexpr std::int32_t SpectatorMap = 6;
using Kernel::BaseClassOf;
using Kernel::CharacterClass;
using Maps::CastleSiegeMap;
using Maps::FirstBloodCastleMap;
using Maps::IsChaosCastle;
using Maps::LastBloodCastleMap;
constexpr std::uint8_t ChargedSkill = 0x28;
constexpr std::uint8_t ReleasedChargedSkill = 0x3a;
constexpr std::size_t PartyOffset = 0x31; // Skill.bmd: with 2, the skill asks for 0x200 in +0x78
constexpr std::size_t WeaponHands = 2;    // 0x57d74c: +0xb6c and +0xbb4

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

// 0x57d1bc..0x57d1d7 and 0x57d3ab..0x57d3c9: auto attack (0x6c8e3c), outside map 6 and Chaos Castle.
bool AutoAttacksHere(const Kernel::WorldRuntime& runtime)
{
    return runtime.ui.autoAttack && runtime.map != SpectatorMap && !IsChaosCastle(runtime.map);
}

// 0x57d369..0x57d3a5: what can be used from the Blood Castle and Chaos Castle safe zone.
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

// 0x57d3cf..0x57d458: with auto attack, the other choice (0x6c8e38) by class and skill: −1 with those that do not
// follow it and 2 with the others.
std::int32_t AutoTargetOf(CharacterClass heroClass, std::uint8_t skill)
{
    constexpr std::int32_t NoAutoTarget = -1;
    constexpr std::int32_t FollowsTheChosen = 2;
    if (heroClass == CharacterClass::Elf)
    {
        if (skill != 0x18 && skill != 0x19 && skill != 0x34 && skill != 0x36 && skill != 0x4c)
            return NoAutoTarget;
    }
    else if (heroClass == CharacterClass::DarkKnight)
    {
        if (skill == 0x30)
            return NoAutoTarget;
    }
    else if (heroClass == CharacterClass::DarkLord)
    {
        if (skill == 0x40 || skill == 0x3f)
            return NoAutoTarget;
    }
    else if (heroClass == CharacterClass::DarkWizard)
    {
        if (skill == 0x3a || skill == 0x28)
            return NoAutoTarget;
    }
    // 0x57d42f..0x57d452: 0x43..0x48 and, through the SBB, 0x4b.
    if (InRange(skill, 0x43, 0x48) || skill == 0x4b)
        return NoAutoTarget;
    return FollowsTheChosen;
}

// 0x57d45d..0x57d4b0: the hero's action (+0x111). A player, 1 to 12, 0x8a, 0x44, 0x48, 0x4a or 0x76 to 0x79;
// otherwise, nothing and, if a player, the right button's frames (0x7d368a4) at 0. Another type, 0 or 1.
enum class ActionGate : std::uint8_t
{
    Passes,
    Stops,
    StopsAndForgetsTheHold,
};

ActionGate GateOfTheAction(const Kernel::ActorState& hero)
{
    const std::uint8_t action = hero.animation.action;
    if (hero.type != PlayerType)
        return action > 1 ? ActionGate::Stops : ActionGate::Passes;
    if (InRange(action, 1, 0xc) || action == 0x8a || action == 0x44 || action == 0x48 || action == 0x4a ||
        InRange(action, 0x76, 0x79))
        return ActionGate::Passes;
    return ActionGate::StopsAndForgetsTheHold;
}

bool Mounted(const Kernel::ActorState& hero)
{
    const std::uint16_t model = hero.pet.model;
    return model == 0x1c14 || model == 0x1c15 || model == 0x1c16 || model == 0x1c37;
}

Models::LogicalAccess OwnAccess(const HeroSkillContext& context)
{
    return Models::LogicalAccess(context.world.Runtime().ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
}

Models::LogicalAccess EntryAccess(const HeroSkillContext& context, std::uint8_t skill)
{
    return Models::LogicalAccess(context.world.Runtime().movement.skillDepths[skill], context.random,
                                 static_cast<std::uint16_t>(Tables::SkillRecordSize));
}

std::uint8_t ReadCell(std::uint8_t& depth, std::uint8_t cell, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(depth, random, Models::SmallLogicalCloseRandoms);
    return cell;
}

// 0x57d2da..0x57d307: byte +0x31 of the entry (0x5aea30 opens it and 0x4e7c40 closes it).
Core::Expected<std::uint8_t, std::string> PartyByteOf(std::uint8_t skill, const HeroSkillContext& context)
{
    const auto record = context.skills->Record(skill);
    if (!record)
        return Core::Unexpected(std::format("0x57d2f1: Skill.bmd no tiene la skill {}", skill));
    const auto entry = EntryAccess(context, skill);
    return std::to_integer<std::uint8_t>((*record)[PartyOffset]);
}

// 0x5a5540(skill): the siege ones (0x2c, 0x2d, 0x2e, 0x39, 0x49 and 0x4a) only on map 30 with the siege (0x4294b0).
bool UsableOnThisMap(std::uint8_t skill, const Kernel::WorldRuntime& runtime)
{
    const bool siegeSkill =
        skill == 0x2c || skill == 0x2d || skill == 0x2e || skill == 0x39 || skill == 0x49 || skill == 0x4a;
    return !siegeSkill || (runtime.map == CastleSiegeMap && runtime.castleSiege.siegeActive);
}

// 0x420f70(hero, slot): the raven's orders. Without a raven (+0x430), without the click or the right button, or with a
// slot outside 0x78 to 0x7b, 0. With 0x7b (0x420fd6..0x42100f), if 0x558bb0 lets the chosen target be attacked and it
// is a player (1) or a monster (2) in +0x88, the A7 with order 3 and its key (+0x1f4); with the others, the A7 with
// slot − 0x78 and key 0xffff (0x421599..0x4219cd). The pet is the raven's +0x444, always 0 (Protocol::Pets::RavenPet).
// Through inline send(); without the socket (+0xc of 0x6c82a0 at −1), it is not sent. Then, with or without the A7,
// the release, the click, the button and the right button's frames at 0 and 1 (0x421aff..0x421b20). No rand().
Core::Expected<bool, std::string> UseTheRavenOrder(const Kernel::ActorState& hero, std::uint8_t slot,
                                                   const HeroSkillContext& context)
{
    constexpr std::uint8_t FirstOrderSlot = 0x78;
    constexpr std::uint8_t AttackOrderSlot = 0x7b;
    constexpr std::uint8_t AttackCommand = 3;
    constexpr std::uint8_t PlayerObject = 1;
    constexpr std::uint8_t MonsterObject = 2;
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    Ui::GameUiState& ui = runtime.ui;
    if (!hero.darkSpirit || (!ui.rightClick && !ui.rightDown) || !InRange(slot, FirstOrderSlot, AttackOrderSlot))
        return false;
    std::optional<Protocol::Pets::PetCommand> command;
    if (slot == AttackOrderSlot)
    {
        const auto attackable = Combat::IsAttackable(context.world, context.controlDown);
        if (!attackable.HasValue())
            return Core::Unexpected(attackable.Error());
        // 0x558bb0 already checked the −1 and the chosen target's slot.
        if (attackable.Value())
        {
            const Kernel::ActorState& chosen =
                context.world.Actors()[static_cast<std::size_t>(runtime.selectedTargets[2])];
            if (chosen.byte88 == MonsterObject || chosen.byte88 == PlayerObject)
                command = Protocol::Pets::PetCommand{
                    .pet = Protocol::Pets::RavenPet, .command = AttackCommand, .key = chosen.key};
        }
    }
    else
    {
        command = Protocol::Pets::PetCommand{.pet = Protocol::Pets::RavenPet,
                                             .command = static_cast<std::uint8_t>(slot - FirstOrderSlot),
                                             .key = Protocol::Pets::NoTargetKey};
    }
    if (command && context.send)
        context.send(Protocol::Pets::WritePetCommand(*command));
    ui.rightReleased = false;
    ui.rightClick = false;
    ui.rightDown = false;
    ui.rightHoldFrames = 0;
    return true;
}

// 0x559890(hero, item): the weapon skill of classes 1, 3 and 4. With what stops it, 0; the listed ones go to 0x559afc
// (UseTheListedWeaponSkill) and the others to the weapon's options (UseTheWeaponOptions, HeroSkillCasts).
Core::Expected<bool, std::string> UseTheWeaponSkill(Kernel::ActorState& hero, const Kernel::ItemState& item,
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
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    std::uint8_t skill = 0;
    {
        const auto own = OwnAccess(context);
        skill = Kernel::OwnSkillByte(context.world.OwnCharacter(), runtime.movement.weaponSkill.slot);
    }
    // 0x5599db..0x559afa: the listed skills and the types of the published hero's right weapon (+0x2b8, MOVSX).
    const Kernel::ActorState& published = context.world.Hero();
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

// 0x57d532..0x57d758: for each hand (+0xb6c and +0xbb4), its weapon's skill. Table 0x599d70 (by skill − 0x2b) allows
// only 0x2b, 0x2f, 0x31, 0x3c to 0x3e, 0x41, 0x4a and 0x4c while mounted. True: it used it (0x57df46, the record closed
// and nothing more).
Core::Expected<bool, std::string> UseTheWeaponSkills(Kernel::ActorState& hero, CharacterClass heroClass,
                                                     std::uint8_t skill, const HeroSkillContext& context)
{
    static constexpr std::array<std::uint8_t, 0x22> MountedSkills{0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                                                  0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0};
    Kernel::WorldRuntime& runtime = context.world.Runtime();
    for (std::size_t hand = 0; hand < WeaponHands; ++hand)
    {
        if (heroClass == CharacterClass::DarkKnight || heroClass == CharacterClass::MagicGladiator ||
            heroClass == CharacterClass::DarkLord)
        {
            const std::uint32_t index = static_cast<std::uint32_t>(skill) - 0x2bu;
            const bool skipped = Mounted(hero) && (index >= MountedSkills.size() || MountedSkills[index] != 0);
            if (!skipped)
            {
                // 0x57d582..0x57d61b: the record open, 0x7758580 at 1, the slot and the chosen target if it is
                // attacked.
                const auto own = OwnAccess(context);
                Movement::WeaponSkillRequest& request = runtime.movement.weaponSkill;
                request.active = 1;
                request.slot = context.world.Hero().skillIndex;
                const auto attackable = Combat::IsAttackable(context.world, context.controlDown);
                if (!attackable.HasValue())
                    return Core::Unexpected(attackable.Error());
                request.target = attackable.Value() ? runtime.selectedTargets[2] : -1;
                const auto used = UseTheWeaponSkill(hero, context.world.OwnCharacter().equipment[hand], context);
                if (!used.HasValue() || used.Value())
                    return used;
            }
        }
        if (heroClass == CharacterClass::Elf)
        {
            const auto own = OwnAccess(context);
            const auto used = UseTheElfWeaponOptions(hero, context.world.OwnCharacter().equipment[hand], context);
            if (!used.HasValue() || used.Value())
                return used;
        }
    }
    return false;
}

// 0x57d787..0x58188e and 0x5897aa..0x5897b1: the class branch. 0x43 to 0x48 (0x5808da, HeroSkillsTail) belong to all.
Core::Expected<void, std::string> UseTheClassSkill(Kernel::ActorState& hero, CharacterClass heroClass,
                                                   std::uint8_t skill, const HeroSkillContext& context)
{
    if (heroClass == CharacterClass::Elf)
        return UseTheElfSkill(hero, skill, context);
    if (heroClass == CharacterClass::DarkKnight || heroClass == CharacterClass::MagicGladiator ||
        heroClass == CharacterClass::DarkLord)
    {
        if (auto used = UseTheKnightSkill(hero, heroClass, skill, context); !used.HasValue())
            return used;
        // 0x5897a7..0x5897b1: the Magic Gladiator continues in the Wizard's branch, which repeats the class test.
        if (heroClass == CharacterClass::MagicGladiator)
            return UseTheWizardSkill(hero, skill, context);
    }
    if (heroClass == CharacterClass::DarkWizard)
        return UseTheWizardSkill(hero, skill, context);
    // 0x5897b1: classes 1, 4 and 5 to 7, to 0x5808da (0x43 to 0x48).
    return UseTheSharedTail(hero, skill, context);
}

// 0x57d12c..0x57d1b8: the right button. With the charge frames (0x7d368a4), the release uses it and otherwise they
// count; otherwise, the click or the button down uses it and 0x28 starts charging as 0x3a. Returns whether it uses it
// and leaves the skill.
enum class RightButton : std::uint8_t
{
    Idle,
    Uses,
    Leaves,
};

RightButton StepTheRightButton(Kernel::ActorState& hero, std::uint8_t& skill, Kernel::WorldState& world)
{
    Ui::GameUiState& ui = world.Runtime().ui;
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
        world.Hero().byte1bc = 0;
    }
    ui.rightClick = false;
    return RightButton::Uses;
}
} // namespace

Core::Expected<void, std::string> UseTheHeroSkill(Kernel::ActorState& hero, const HeroSkillContext& context)
{
    Kernel::WorldState& world = context.world;
    Kernel::WorldRuntime& runtime = world.Runtime();
    Movement::GameMovementState& state = runtime.movement;
    Kernel::MouseState& mouse = runtime.mouse;
    Ui::GameUiState& ui = runtime.ui;
    // 0x57cfe2..0x57d00f.
    if (state.inputBlocked || ui.mouseOnUi != 0 || (hero.flags78 & FrozenHeroFlag) == FrozenHeroFlag)
        return {};
    const CharacterClass heroClass = BaseClassOf(hero.characterClass);
    if (hero.byte80 == 1)
    {
        // 0x57d02b..0x57d04a: fst keeps the extended result for the comparison.
        const double faded = static_cast<double>(hero.alpha) - static_cast<double>(TeleportFade);
        hero.alpha = static_cast<float>(faded);
        if (faded < static_cast<double>(TeleportFade))
            hero.byte80 = 2;
    }
    // 0x57d051..0x57d10d: the skill of the published hero's slot +0x425, with the record open.
    std::uint8_t skill = 0;
    {
        const auto own = OwnAccess(context);
        skill = Kernel::OwnSkillByte(world.OwnCharacter(), world.Hero().skillIndex);
    }
    // 0x57d112: the reach stays in a local that the class branches read.
    const auto range = SkillRangeOf(skill, hero, context);
    if (!range.HasValue())
        return Core::Unexpected(range.Error());
    state.selectedSkillRange = range.Value();
    bool uses = false;
    if (state.movementBlocked)
    {
        // 0x57d261..0x57d282: locked, only the left click (the edge is spent) or the button down, without the clock.
        if (!mouse.pendingClick && !mouse.leftHeld)
            return {};
        mouse.pendingClick = false;
        uses = true;
    }
    else
    {
        const RightButton right = StepTheRightButton(hero, skill, world);
        if (right == RightButton::Leaves)
            return {};
        uses = right == RightButton::Uses;
        // 0x57d1bc..0x57d1ef: auto attack with the other choice (0x6c8e38) at 2 and a chosen target.
        if (AutoAttacksHere(runtime) && runtime.selectedTargets[4] == 2 && runtime.selectedTargets[2] != -1)
            uses = true;
        if (!uses)
        {
            // 0x57d23c..0x57d260.
            state.actionInputTime = context.worldTime;
            state.actionInputElapsed = 0.0f;
            return {};
        }
        // 0x57d1f8..0x57d22d: since the last time without use, in s; at 3600 it releases the right button.
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
    const Kernel::ActorState& published = world.Hero();
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
    // 0x57d33d..0x57d3a5: in the safe zone, only in Blood Castle or Chaos Castle and with the listed skills.
    if (hero.safeZone != 0)
    {
        if (!InRange(runtime.map, FirstBloodCastleMap, LastBloodCastleMap) && !IsChaosCastle(runtime.map))
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
    if (heroClass != CharacterClass::DarkWizard)
    {
        // 0x57d4be..0x57d51f: the target and, from the logical cell (Y before X), whether it is seen. MuRet takes the
        // line that leaves TerrainWall (0x55886d reads past the 65536 WORDs) as no line (08, P-C256). 0x57d4c7: nobody
        // looks at the result.
        if (auto aimed = AimTheHeroSkill(hero, context); !aimed.HasValue())
            return Core::Unexpected(std::move(aimed).Error());
        const auto y = ReadCell(hero.cellYAccesses, hero.cellY, context.random);
        const auto x = ReadCell(hero.cellXAccesses, hero.cellX, context.random);
        const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
        if (!terrain.attributes)
            return Core::Unexpected(std::string("0x558800: falta TerrainWall para ver el blanco de la skill"));
        const bool seen =
            Pathfinding::CanSeeTheCell(terrain.attributes->cells, x, y, std::bit_cast<std::int32_t>(state.targetX),
                                       std::bit_cast<std::int32_t>(state.targetY))
                .value_or(false);
        if (seen)
        {
            const auto used = UseTheWeaponSkills(hero, heroClass, skill, context);
            if (!used.HasValue())
                return Core::Unexpected(used.Error());
            if (used.Value())
                return {};
        }
        else
        {
            // 0x57d760..0x57d774: the three at 0 and the chosen target at −1.
            state.weaponSkill = Movement::WeaponSkillRequest{.active = 0, .slot = 0, .target = -1};
        }
    }
    return UseTheClassSkill(hero, heroClass, skill, context);
}
} // namespace Client::Skills
