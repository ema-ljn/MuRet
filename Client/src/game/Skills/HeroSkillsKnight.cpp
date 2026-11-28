#include "Skills/HeroSkillsKnight.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Kernel/CharacterClass.h"
#include "Actors/WalkPath.h"
#include "Maps/MapId.h"
#include "Math/Angles.h"
#include "Protocol/Skills/Skill.h"
#include "Models/Animation.h"
#include "Models/LogicalAccess.h"
#include "Math/OriginalAngles.h"
#include "Actors/ActorName.h"
#include "Kernel/ActorState.h"
#include "Combat/CombatActionSpeeds.h"
#include "Movement/GameMovementState.h"
#include "Combat/Attackable.h"
#include "Skills/HeroSkillCasts.h"
#include "Skills/HeroSkills.h"
#include "Kernel/OwnCharacterRecord.h"
#include "Skills/Skill4e9920.h"
#include "Skills/Skill4e9c50.h"
#include "Skills/Skill551f40.h"
#include "Skills/Skill57cf90.h"
#include "Skills/SkillDelays.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Math/Ftol.h"
#include "Terrain/WorldTerrain.h"
#include "Terrain/TerrainDefines.h"
#include "Tables/PlayerModels.h"
#include "Audio/SoundId.h"

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

namespace Client::Skills
{
namespace
{
constexpr std::uint16_t PlayerType = Tables::PlayerSkeletonModel;
constexpr std::int16_t NoWeapon = -1;
constexpr std::int16_t FirstStaffModel = 0xc12;
constexpr std::int16_t StaffModelsEnd = 0xe12;
constexpr std::uint16_t UniriaModel = 0x1c14;
constexpr std::uint16_t DarkHorseModel = 0x1c16;
constexpr std::uint16_t FenrirModel = 0x1c37;
constexpr std::size_t DarkHorseSlot = 8; // [0x7681eb4] + 0xb6c + 8 · 0x48 (+0x1a, the durability: +0xdc6)
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;
constexpr std::uint16_t NoKey = 0xffff;
constexpr std::string_view GameMasterName = "webzen"; // 0x6c915c..0x6c91a4, one per skill
constexpr std::int32_t MinimumOffset = -8;
constexpr std::int32_t MaximumOffset = 7;
constexpr std::uint8_t OffsetBias = 8;
constexpr unsigned HighNibble = 4;
constexpr std::uint8_t LowNibbleMask = 0xf;
constexpr double CellSize = Terrain::CellSizeAsDouble;                                                  // [0x6b86b4]
constexpr double HalfCell = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x42480000}));       // [0x6b86b0]
constexpr double CellReciprocal = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3c23d70a})); // [0x6b86c0]
constexpr double TurnsOf256 = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3f360b61}));     // [0x6ba440]
constexpr double TurnFraction = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3b360b61}));   // [0x6ba444]
constexpr double ByteScale = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x437f0000}));      // [0x6b9f98]
constexpr double HalfTurn = Math::HalfTurnAsDouble;                                                     // [0x6b86bc]
constexpr double WalkReach = static_cast<double>(std::bit_cast<float>(std::uint32_t{0x3f99999a}));      // [0x6b8bb0]

bool InRange(std::int32_t value, std::int32_t first, std::int32_t last)
{
    return value >= first && value <= last;
}

Models::LogicalAccess OwnAccess(const HeroSkillContext& context)
{
    return Models::LogicalAccess(context.world.Runtime().ownRecordDepth, context.random, Models::OwnRecordCloseRandoms);
}

SkillDelayContext DelaysOf(const HeroSkillContext& context)
{
    return SkillDelayContext{.world = context.world, .random = context.random, .skills = context.skills};
}

const Kernel::ActorState& PublishedHero(const HeroSkillContext& context)
{
    return context.world.Hero();
}

// 0x558eb0 compares the pointer with [0x743a170]: `hero`'s slot in the table, or one past it if absent.
std::size_t SlotOf(const Kernel::WorldState& world, const Kernel::ActorState& hero)
{
    const auto actors = world.Actors();
    const std::less<const Kernel::ActorState*> before;
    if (before(&hero, actors.data()) || !before(&hero, actors.data() + actors.size()))
        return actors.size();
    return static_cast<std::size_t>(&hero - actors.data());
}

// 0x581912..0x58197a: with no right weapon, or with a staff (0xc12 to 0xe11, MOVSX) in one of the published hero's
// hands, only the listed skills continue.
bool TheHandsAllow(const Kernel::ActorState& published, std::uint8_t skill)
{
    const auto right = static_cast<std::int16_t>(published.hands[0].model);
    const auto left = static_cast<std::int16_t>(published.hands[1].model);
    const auto staff = [](std::int16_t model) { return model >= FirstStaffModel && model < StaffModelsEnd; };
    if (right != NoWeapon && !staff(right) && !staff(left))
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

// 0x5819bc..0x581a6b and 0x581abb..0x581b6a: the horse's durability (+0xdc6), with the record already open.
std::uint8_t HorseDurability(const HeroSkillContext& context)
{
    const auto own = OwnAccess(context);
    return context.world.OwnCharacter().equipment[DarkHorseSlot].durability;
}

// 0x58197f..0x581b6c: [ebp−0x3d], whether it can be used, before the mana.
bool UsableHere(std::uint8_t skill, const Kernel::ActorState& published, const HeroSkillContext& context)
{
    const Kernel::WorldRuntime& runtime = context.world.Runtime();
    bool usable = true;
    if (skill == 0x3f)
        usable = runtime.hud.partyCount > 0;
    else if (skill == 0x4c)
        usable = published.pet.model == FenrirModel;
    else if (skill == 0x3e && HorseDurability(context) == 0)
        usable = false;
    // 0x581a71..0x581ab1: in Chaos Castle, the listed ones cannot; outside, 0x3e checks the horse again.
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

// 0x581b70..0x58236f: mana, stamina, the delay, energy and, for the Dark Lord, leadership. Empty: it ends without
// checking whether it could (0x582364).
Core::Expected<std::optional<bool>, std::string> PaysForTheSkill(Kernel::CharacterClass heroClass, std::uint8_t skill,
                                                                 const SkillCost& costs, bool usable,
                                                                 const HeroSkillContext& context)
{
    const SkillDelayContext delays = DelaysOf(context);
    const Kernel::OwnCharacterRecord& own = context.world.OwnCharacter();
    if (static_cast<std::int32_t>(costs.mana) > static_cast<std::int32_t>(own.mana))
    {
        // 0x581b86..0x5822d1: 0x558110(3) looks for the potion; with or without it, it cannot (Q-C1233).
        if (auto potion = UseAManaPotion(context, "0x581b9e"); !potion.HasValue())
            return Core::Unexpected(potion.Error());
        usable = false;
    }
    else if (usable)
    {
        if (static_cast<std::int32_t>(costs.stamina) > static_cast<std::int32_t>(own.bp))
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
    if (heroClass == Kernel::CharacterClass::DarkLord)
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

// 0x40b570 and 0x40b800 on +0x420 and then on +0x41c: Y first.
Cells ReadTheCells(Kernel::ActorState& hero, Crt::CrtRandom& random)
{
    Cells cells;
    {
        const Models::LogicalAccess access(hero.cellYAccesses, random, Models::SmallLogicalCloseRandoms);
        cells.y = hero.cellY;
    }
    {
        const Models::LogicalAccess access(hero.cellXAccesses, random, Models::SmallLogicalCloseRandoms);
        cells.x = hero.cellX;
    }
    return cells;
}

// 0x4e93f0(+0x10, +0x14, +0x3a8, +0x3ac) into +0x24.
void FaceTheTarget(Kernel::ActorState& hero)
{
    hero.angles.z = static_cast<float>(
        Math::CreateAngle(hero.position.x, hero.position.y, hero.attackPosition.x, hero.attackPosition.y));
}

// 0x583f12..0x583f63 (and 0x586436, 0x587aab and 0x588899): the distance from the hero to the center of the target's
// cell (0x77e1ba8 and 0x77e1bac) against the reach (0x5526f0's local) · 100; with NaN, in reach (TEST AH, 0x41).
bool OutOfRange(const Kernel::ActorState& hero, const Movement::GameMovementState& state)
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

// 0x58314f..0x58319c: the target's cell minus the hero's, each between −8 and 7: X + 8 on top and Y − 8 below, with
// the AND.
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
    const auto truncated = Math::OriginalFtolLow(value);
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

// The 1E's heading: +0x24 · 256/360.
Core::Expected<std::uint8_t, std::string> DirectionOf(const Kernel::ActorState& hero, std::string_view where)
{
    return LowByte(static_cast<double>(hero.angles.z) * TurnsOf256, where);
}

// 0x41: the target's cell (+0x3a8 and +0x3ac · 0.01).
Core::Expected<std::int32_t, std::string> CellOf(float coordinate, std::string_view where)
{
    return Ftol(static_cast<double>(coordinate) * CellReciprocal, where);
}

// 0x4e9920(+0x10, +0x14, _ftol(+0x3a8), _ftol(+0x3ac)): the relative cell, with the target's without the · 0.01.
Core::Expected<std::uint8_t, std::string> RelativeCellOf(const Kernel::ActorState& hero, std::string_view where)
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

// 0x558eb0(hero, chosen): the key, whose low 16 bits the 1E sends.
Core::Expected<std::uint16_t, std::string> ChosenKey(const Kernel::ActorState& hero, std::int32_t target,
                                                     const HeroSkillContext& context)
{
    const auto key = Combat::ChosenKeyOf(context.world, SlotOf(context.world, hero), target, context.controlDown);
    if (!key.HasValue())
        return Core::Unexpected(key.Error());
    return static_cast<std::uint16_t>(static_cast<std::uint32_t>(key.Value()));
}

// 0x5823b1..0x5823ce: with the weapon skill's chosen target (0x7758588) at −1, 0xffff without calling 0x558eb0.
Core::Expected<std::uint16_t, std::string> WeaponTargetKey(const Kernel::ActorState& hero,
                                                           const HeroSkillContext& context)
{
    const std::int32_t target = context.world.Runtime().movement.weaponSkill.target;
    if (target == -1)
        return NoKey;
    return ChosenKey(hero, target, context);
}

// The inline strstr with "webzen" on the published hero's name (+0x1d9): with it, nothing is sent.
bool SentByTheGameMaster(const HeroSkillContext& context)
{
    return Actors::ActorNameContains(PublishedHero(context), GameMasterName);
}

// The 1E (0x582446..0x582dd1 and the others): 0x5767948 with the skill, the hero's logical cells (Y before X) and the
// C3. The heading and bytes 7 to 11 draw no rand(): each branch computes them first.
struct DurationBytes
{
    std::uint8_t direction = 0;
    std::uint8_t towards = 0;
    std::uint8_t back = 0;
    std::uint16_t key = 0;
    std::uint8_t magicKey = 0;
};

void SendTheDurationMagic(Kernel::ActorState& hero, std::uint8_t skill, const DurationBytes& bytes,
                          const HeroSkillContext& context)
{
    context.world.Runtime().lastShotSkill = skill;
    const Cells cells = ReadTheCells(hero, context.random);
    // A C3: the padding (0x69fd70) and the serial (0x40ae60 and 0x40b0a0) draw two rand() before checking the socket.
    SendTheSkillPacket(context, Protocol::Skills::WriteDurationMagic({.skill = skill,
                                                                      .x = cells.x,
                                                                      .y = cells.y,
                                                                      .direction = bytes.direction,
                                                                      .towards = bytes.towards,
                                                                      .back = bytes.back,
                                                                      .key = bytes.key,
                                                                      .magicKey = bytes.magicKey}));
}

// The 19 (0x584dde..0x5854b4 and the others): without "webzen" and with the 300 ms brake, the skill and the player key
// ([0x57678bc]).
Core::Expected<void, std::string> SendTheMagicAttack(std::uint8_t skill, const HeroSkillContext& context,
                                                     std::string_view where)
{
    if (SentByTheGameMaster(context))
        return {};
    const auto brake = PassesTheSkillBrake(skill, context, where);
    if (!brake.HasValue())
        return Core::Unexpected(brake.Error());
    if (!brake.Value())
        return {};
    SendTheSkillPacket(context, Protocol::Skills::WriteMagicAttack(skill, context.playerKey));
    return {};
}

// 0x4ea030 with the player's skeleton: the branch's hero is a 0x20a (0x581912).
Core::Expected<void, std::string> PutTheAction(Kernel::ActorState& hero, std::uint8_t action,
                                               const HeroSkillContext& context)
{
    if (context.playerSkeleton == nullptr)
        return Core::Unexpected(std::format("0x4ea030: falta el esqueleto del jugador para la acción {:#x}", action));
    Models::SetAction(hero.animation, *context.playerSkeleton, action);
    return {};
}

// 0x58885c..0x58886f: the action and +0x378 at 0.
Core::Expected<void, std::string> ActAndStop(Kernel::ActorState& hero, std::uint8_t action,
                                             const HeroSkillContext& context)
{
    if (auto put = PutTheAction(hero, action, context); !put.HasValue())
        return put;
    hero.movement = 0;
    return {};
}

// 0x58977e: out of reach, the other choice (0x6c8e38) at −1.
Core::Expected<void, std::string> ForgetTheOtherTarget(const HeroSkillContext& context)
{
    context.world.Runtime().selectedTargets[4] = -1;
    return {};
}

// 0x582393..0x5830dc: skill 0x29. The heading towards the target, the key and the 1E with 0 in bytes 7 and 8; with
// "webzen", not sent. The speeds (0x4f3f80) and action 0x3d.
Core::Expected<void, std::string> UseSkill29(Kernel::ActorState& hero, std::uint8_t skill,
                                             const HeroSkillContext& context)
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
    Combat::RefreshCombatActionSpeeds(context.world, context.random);
    return ActAndStop(hero, 0x3d, context);
}

// 0x587a8d..0x58886f: skill 0x2a. The heading; out of reach, the other choice at −1 and nothing more. Otherwise, the
// relative cell of 0x4e9920 in byte 8, the key and the 1E; action 0x3e, without 0x4f3f80.
Core::Expected<void, std::string> UseSkill2a(Kernel::ActorState& hero, std::uint8_t skill,
                                             const HeroSkillContext& context)
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
                             {.direction = direction.Value(), .back = relative.Value(), .key = key.Value()}, context);
    }
    return ActAndStop(hero, 0x3e, context);
}

// 0x5830e1..0x583ece: skill 0x37. The heading, the cells (the first read) and the target's position in byte 7.
Core::Expected<void, std::string> UseSkill37(Kernel::ActorState& hero, std::uint8_t skill,
                                             const HeroSkillContext& context)
{
    const Movement::GameMovementState& state = context.world.Runtime().movement;
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
        SendTheDurationMagic(hero, skill, {.direction = direction.Value(), .towards = position, .key = key.Value()},
                             context);
    }
    Combat::RefreshCombatActionSpeeds(context.world, context.random);
    return ActAndStop(hero, 0x3d, context);
}

// 0x583ed3..0x584d6e: skill 0x38. Mounted (0x1c14 to 0x1c16 or Fenrir), nothing; out of reach, nothing. The heading is
// on the 255 scale (· 1/360 · 255) and byte 8 is the opposite heading ((+0x24 + 180) · 1/360 · 255). The action, 0x8d;
// Fenrir's 0x5d outside the safe zone (EBX, 0x58187f) never arrives, because Fenrir already left.
Core::Expected<void, std::string> UseSkill38(Kernel::ActorState& hero, std::uint8_t skill,
                                             const HeroSkillContext& context)
{
    const auto pet = static_cast<std::int16_t>(hero.pet.model);
    if ((pet >= static_cast<std::int16_t>(UniriaModel) && pet <= static_cast<std::int16_t>(DarkHorseModel)) ||
        pet == static_cast<std::int16_t>(FenrirModel))
        return {};
    const Movement::GameMovementState& state = context.world.Runtime().movement;
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
            {.direction = direction.Value(), .towards = position, .back = opposite.Value(), .key = key.Value()},
            context);
    }
    Combat::RefreshCombatActionSpeeds(context.world, context.random);
    const bool fenrir = hero.pet.model == FenrirModel && hero.safeZone == 0;
    return ActAndStop(hero, fenrir ? 0x5d : 0x8d, context);
}

// 0x586419..0x587346: skill 0x3e (on the dark horse and outside the safe zone) and 0x41. Out of reach, the other
// choice at −1. The target's cell (+0x3a8 and +0x3ac · 0.01) against the hero's in byte 7, that of 0x4e9920 in 8, the
// key, and the heading after. 0x41 does 0x51 on the horse, 0x5b on Fenrir (outside the safe zone) or 0x94; 0x3e, 0x53
// and sound 0x1ce.
Core::Expected<void, std::string> UseSkill3eOr41(Kernel::ActorState& hero, std::uint8_t skill,
                                                 const HeroSkillContext& context)
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
            {.direction = direction.Value(), .towards = position, .back = relative.Value(), .key = key.Value()},
            context);
    }
    Combat::RefreshCombatActionSpeeds(context.world, context.random);
    if (skill == 0x3e)
    {
        if (auto put = PutTheAction(hero, 0x53, context); !put.HasValue())
            return put;
        context.world.Runtime().RequestSound(Audio::SoundId{0x1ce}); // 0x41ed10(0x1ce, 0, 0) (CL10)
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

// 0x58734b..0x587a88: skill 0x30. The 19 and, sent or not, action 0x3f.
Core::Expected<void, std::string> UseSkill30(Kernel::ActorState& hero, std::uint8_t skill,
                                             const HeroSkillContext& context)
{
    if (auto sent = SendTheMagicAttack(skill, context, "0x5873ba"); !sent.HasValue())
        return sent;
    return ActAndStop(hero, 0x3f, context);
}

// 0x584d73..0x5854b7, 0x5854fd.. and 0x585c8a..: skills 0x3f, 0x40 and 0x4b. The 19 and +0x378 at 0, no action.
Core::Expected<void, std::string> UseSkillWithTheMagicAttack(Kernel::ActorState& hero, std::uint8_t skill,
                                                             const HeroSkillContext& context, std::string_view where)
{
    if (auto sent = SendTheMagicAttack(skill, context, where); !sent.HasValue())
        return sent;
    hero.movement = 0;
    return {};
}

// 0x589744..0x589779: out of reach with a chosen target, the path to the cells closer than reach · 1.2 (as a float)
// to the target's cell (0x4eac00), with the hero's Y read before X. With a path, +0x378 at 1. With the target's cell
// outside 0..255, which PathFinding2 (0x58975f) takes as a linear index, MuRet drops the path after the reads, as with
// no path (08, P-C256).
Core::Expected<void, std::string> WalkTowardsTheTarget(Kernel::ActorState& hero, std::int32_t targetX,
                                                       std::int32_t targetY, const HeroSkillContext& context)
{
    const Cells cells = ReadTheCells(hero, context.random);
    const auto reach =
        static_cast<float>(static_cast<double>(context.world.Runtime().movement.selectedSkillRange) * WalkReach);
    if (!InRange(targetX, 0, 0xff) || !InRange(targetY, 0, 0xff))
        return {};
    const Terrain::WorldTerrain& terrain = Terrain::TerrainOrEmpty(context.terrain);
    if (!terrain.attributes)
        return Core::Unexpected(std::string("0x58975f: falta TerrainWall para acercarse con la 0x4c"));
    // 0x4eac0a..0x4eac41: the Crywolf altar flag, which does not count with a nonzero reach (0x4eadd4).
    const auto path =
        Actors::FindThePath(context.world, terrain.attributes->cells, {cells.x, cells.y},
                            {static_cast<std::uint8_t>(targetX), static_cast<std::uint8_t>(targetY)}, reach);
    if (!path)
        return {};
    hero.path = *path;
    hero.movement = 1;
    return {};
}

// 0x588874..0x5896e5: skill 0x4c (Fenrir). The target's cell (· 0.01); out of reach, with no weapon-skill chosen
// target, the other choice at −1, and with one, the path. In reach, the cell of 0x4e9920; with no chosen target,
// +0x434 at −1 and nothing more (not even +0x378). Otherwise: the heading, the delay (0x552990), the key (0x558eb0
// without checking the −1), +0x434 with the chosen target and the 1E with the magic key (0x57cf90 inline, in +0x1bd)
// in byte 11; +0x378 at 0 and the action of the hero's hands (0x4e9c50).
Core::Expected<void, std::string> UseSkill4c(Kernel::ActorState& hero, std::uint8_t skill,
                                             const HeroSkillContext& context)
{
    Movement::GameMovementState& state = context.world.Runtime().movement;
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
            {.direction = direction.Value(), .back = relative.Value(), .key = key.Value(), .magicKey = magicKey},
            context);
    }
    hero.movement = 0;
    if (hero.type != PlayerType)
        return {};
    return PutTheAction(hero, ActionByTheHands(hero), context);
}

// 0x58238c: table 0x599e10/0x599de0 by skill − 0x29 (up to 0x23); the others go to 0x5854be.
Core::Expected<void, std::string> UseTheSkill(Kernel::ActorState& hero, std::uint8_t skill,
                                              const HeroSkillContext& context)
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

Core::Expected<void, std::string> UseTheKnightSkill(Kernel::ActorState& hero, Kernel::CharacterClass heroClass,
                                                    std::uint8_t skill, const HeroSkillContext& context)
{
    // 0x581894: the own record stays open until 0x5854be..0x5897a2, where it closes (3848 rand() at 0).
    const auto own = OwnAccess(context);
    const auto costs = SkillCostOf(skill, context, "0x58190a");
    if (!costs.HasValue())
        return Core::Unexpected(costs.Error());
    if (hero.type != PlayerType)
        return {};
    const Kernel::ActorState& published = PublishedHero(context);
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
} // namespace Client::Skills
