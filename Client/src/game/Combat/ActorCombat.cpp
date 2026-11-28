#include "Combat/ActorCombat.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Tables/MonsterSkillTable.h"
#include "Maps/MapId.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Models/LogicalAccess.h"
#include "Kernel/ActorState.h"
#include "Combat/CombatActionSpeeds.h"
#include "Effects/HighModelEffects.h"
#include "Sound/MonsterSounds.h"
#include "Effects/SkillBurstEffects.h"
#include "Effects/WizardEffects.h"
#include "Session/WorldEntry.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Effects/ModelEffects.h"
#include "Effects/WizardModelEffects.h"
#include "Terrain/WorldTerrain.h"
#include "Assets/FaunaModels.h"
#include "Audio/SoundId.h"
#include "Sound/SoundIds.h"
#include "Models/Cloth.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>

namespace Client::Combat
{
namespace
{
constexpr std::uint16_t Uniria = 0x1c14;
constexpr std::uint16_t Dinorant = 0x1c15;
constexpr std::uint16_t DarkHorse = 0x1c16;
constexpr std::uint16_t Fenrir = 0x1c37;
constexpr int NoModel = -1;
// 0x4f2c4c and 0x4f4c28: byte +0x1e of the weapon's Item.bmd record ([0x7758550] − 0xadca + model · 0x54).
constexpr std::size_t WeaponMarkOffset = 0x1e;
// 0x4f5af0: the frame 0x4f5660 leaves, 0.001f.
constexpr float DeathFrame = std::bit_cast<float>(0x3a83126fu);
using Maps::FirstChaosCastleMap;
using Maps::LastChaosCastleMap;

// 0x4f4a10 compares the models as signed WORDs.
int Signed(std::uint16_t model)
{
    return static_cast<std::int16_t>(model);
}
// Group 4 indices 0 to 6, 17, 20 and 21 (MuMain's bows).
bool IsLeftGroup4(int model)
{
    return (model >= 0xa12 && model < 0xa19) || model == 0xa23 || model == 0xa26 || model == 0xa27;
}
// Group 4 indices 8 to 14, 16 and 18 onward (MuMain's crossbows).
bool IsRightGroup4(int model)
{
    return (model >= 0xa1a && model < 0xa21) || model == 0xa22 || (model >= 0xa24 && model < 0xc12);
}
// Groups 0 to 2 (0x212..0x811).
bool IsMelee(int model)
{
    return model >= 0x212 && model < 0x812;
}
bool HasModelSoundSubtype(const Kernel::ActorState& actor)
{
    return actor.subtype >= 0xd1 && actor.subtype <= 0xd3;
}
// 0x4f50b8 and 0x4f5a09 test C3 after FCOMP with 0: equal to 0 or unordered.
bool FrameIsZero(const Kernel::ActorState& actor)
{
    return !(actor.animation.frame < 0.0f || actor.animation.frame > 0.0f);
}
bool IsHero(const Kernel::ActorState& actor, const Kernel::WorldState& world)
{
    return &actor == &world.Hero();
}

Core::Expected<bool, std::string> IsMarked(int model, const ActorCombatContext& context)
{
    if (context.items == nullptr)
        return Core::Unexpected(std::string("0x4f4a10: falta Item.bmd para leer la marca +0x1e del arma"));
    const auto record = context.items->Record(static_cast<std::size_t>(model - Tables::FirstItemModel));
    if (!record)
        return Core::Unexpected(std::string("0x4f4a10: Item.bmd no tiene el registro del arma de la derecha"));
    return (*record)[WeaponMarkOffset] != std::byte{0};
}

// 0x4ea030, with the skeleton of the character's model.
Core::Expected<void, std::string> SetTheAction(Kernel::ActorState& actor, const ActorCombatContext& context,
                                               std::uint8_t action)
{
    const Models::Skeleton* skeleton = nullptr;
    if (actor.type == Tables::PlayerSkeletonModel && context.playerSkeleton != nullptr)
        skeleton = context.playerSkeleton;
    else
    {
        skeleton = Assets::FaunaModelsOrEmpty(context.faunaModels).SkeletonOf(Models::ModelId{actor.type});
        if (skeleton == nullptr && context.models != nullptr)
            skeleton = context.models->SkeletonOf(Models::ModelId{actor.type});
    }
    if (skeleton != nullptr)
        Models::SetAction(actor.animation, *skeleton, action);
    return {};
}

// 0x4f4a4e..0x4f4ae8: Fenrir outside the safe zone.
std::uint8_t FenrirAttack(int right, int left)
{
    if (right >= 0x812 && right < 0x817)
        return 0x5f;
    if (IsLeftGroup4(left))
        return 0x61;
    if (IsRightGroup4(right))
        return 0x5e;
    if (right == NoModel)
        return left == NoModel ? 0x56 : 0x60;
    return left == NoModel ? 0x60 : 0x5c;
}

// 0x4f4b4b..0x4f4c5e: Uniria and Dinorant outside the safe zone.
Core::Expected<std::uint8_t, std::string> RideAttack(int right, int left, const ActorCombatContext& context)
{
    if (right >= 0x812 && right < 0x817)
        return std::uint8_t{0x34};
    if (right >= 0x817 && right < 0xa12)
        return std::uint8_t{0x35};
    if (IsLeftGroup4(left))
        return std::uint8_t{0x36};
    if (IsRightGroup4(right))
        return std::uint8_t{0x37};
    if (right == NoModel)
        return std::uint8_t{0x32};
    const auto marked = IsMarked(right, context);
    if (!marked.HasValue())
        return Core::Unexpected(marked.Error());
    return marked.Value() ? std::uint8_t{0x33} : std::uint8_t{0x32};
}

// 0x4f4c63..0x4f4f2f: on foot, by the weapons. The two that pick at random draw rand() & 1.
Core::Expected<std::uint8_t, std::string> GroundAttack(const Kernel::ActorState& actor, int right, int left,
                                                       const ActorCombatContext& context)
{
    const std::uint8_t count = actor.attackCount;
    if (right == NoModel && left == NoModel)
        return std::uint8_t{0x22};
    if (IsMelee(right))
    {
        const auto marked = IsMarked(right, context);
        if (!marked.HasValue())
            return Core::Unexpected(marked.Error());
        if (!marked.Value())
        {
            if (!IsMelee(left))
                return static_cast<std::uint8_t>((count & 1) + 0x23);
            // 0x4f4cf0: the table 0x4f5278 by +0x38f & 3.
            constexpr std::array<std::uint8_t, 4> TwoWeapons{0x23, 0x25, 0x24, 0x26};
            return TwoWeapons[count & 3];
        }
        if (right == 0x227 || right == 0x231 || right == 0x229)
            return std::uint8_t{0x8d};
        return static_cast<std::uint8_t>(count % 3 + 0x27);
    }
    if (IsMelee(left))
        return static_cast<std::uint8_t>((context.random.Next() & 1) + 0x25);
    if (right >= 0xc12 && right < 0xe12)
    {
        const auto marked = IsMarked(right, context);
        if (!marked.HasValue())
            return Core::Unexpected(marked.Error());
        const auto drawn = context.random.Next() & 1;
        return static_cast<std::uint8_t>(drawn + (marked.Value() ? 0x90 : 0x23));
    }
    if (right == 0x813 || right == 0x814)
        return std::uint8_t{0x2a};
    if (right >= 0x812 && right < 0xa12)
        return static_cast<std::uint8_t>(count % 3 + 0x2b);
    const bool noWings = actor.wings.model == Kernel::NoItemModel;
    if (IsLeftGroup4(left))
        return noWings ? std::uint8_t{0x2e} : std::uint8_t{0x30};
    if (IsRightGroup4(right))
        return noWings ? std::uint8_t{0x2f} : std::uint8_t{0x31};
    return std::uint8_t{0x22};
}

Core::Expected<void, std::string> PlayerAttack(Kernel::ActorState& actor, const ActorCombatContext& context)
{
    // 0x4f4a2a: 0x4f3f80, Player.bmd's speeds from the own record (3848 rand() when it closes).
    RefreshCombatActionSpeeds(context.world, context.random);
    const int right = Signed(actor.hands[0].model);
    const int left = Signed(actor.hands[1].model);
    const bool outside = actor.safeZone == 0;
    const std::uint16_t pet = actor.pet.model;
    if (pet == Fenrir && outside)
    {
        if (auto set = SetTheAction(actor, context, FenrirAttack(right, left)); !set.HasValue())
            return set;
        // 0x4f4af0: the Dark Lord overwrites the action with 0x59.
        if (Kernel::BaseClassOf(actor.characterClass) == Kernel::CharacterClass::DarkLord)
            return SetTheAction(actor, context, 0x59);
        return {};
    }
    if (pet == DarkHorse && outside)
        return SetTheAction(actor, context, 0x50);
    const auto action = (pet == Uniria || pet == Dinorant) && outside ? RideAttack(right, left, context)
                                                                      : GroundAttack(actor, right, left, context);
    if (!action.HasValue())
        return Core::Unexpected(action.Error());
    return SetTheAction(actor, context, action.Value());
}

// A protected byte's read: it opens its entry, reads and closes it, with the rand() of the close that leaves it without
// accesses (0x40b3f0: one, for the byte).
constexpr std::uint16_t ByteCloseRandoms = 1;
std::uint8_t ReadProtected(const std::uint8_t& value, std::uint8_t& accesses, Crt::CrtRandom& random)
{
    const Models::LogicalAccess access(accesses, random, ByteCloseRandoms);
    return value;
}

template <typename T, std::size_t N> bool IsOneOf(const std::array<T, N>& values, T value)
{
    return std::ranges::find(values, value) != values.end();
}

// 0x43a2f0's +0x376 groups (the byte table of 0x43a644 over +0x376 − 0x91 and its jumps at 0x43a628).
constexpr std::array<std::uint16_t, 6> KalimaFirstGroup{0x91, 0xaf, 0xb7, 0xbf, 0x105, 0x10d};
constexpr std::array<std::uint16_t, 12> KalimaElevenGroups{0x93, 0xb1, 0xb9, 0xc1, 0x107, 0x10f,
                                                           0x94, 0xb2, 0xba, 0xc2, 0x108, 0x110};
constexpr std::array<std::uint16_t, 6> KalimaFourthGroup{0x95, 0xb3, 0xbb, 0xc3, 0x109, 0x111};
constexpr std::array<std::uint16_t, 12> KalimaDrawingGroup{0xa0, 0xa1, 0xb4,  0xb5,  0xbc,  0xbd,
                                                           0xc4, 0xc5, 0x10a, 0x10b, 0x112, 0x113};
// 0x43a3c5: the values of +0x38e − 1 that the table of 0x43a6d0 sends to action 4 (0x43a3d2).
constexpr std::array<std::uint8_t, 6> KalimaSecondActionSkills{1, 0x11, 0x37, 0xc8, 0xc9, 0xca};
constexpr std::uint8_t KalimaSkill = 0x11;
constexpr std::uint8_t FirstKalimaSkill = 1;
constexpr std::uint8_t FirstAttack = 3;
constexpr std::uint8_t SecondAttack = 4;

// 0x43a2f0: Kalima's monsters, on any map. It reads +0x38e (and closes its entry) before choosing the action; the last
// group's draw rand() & 1 (0x43a5d4 and 0x43a5fb, the signed AND of a rand() that is never negative). It returns
// whether it handled the actor (AL at 1): every one in the table, even those that set no action (0x43a5ce).
Core::Expected<bool, std::string> AttackOfKalima(Kernel::ActorState& actor, const ActorCombatContext& context)
{
    const std::uint16_t kind = actor.word376;
    std::optional<std::uint8_t> action;
    if (IsOneOf(KalimaFirstGroup, kind))
    {
        // 0x43a322..0x43a3e6: the inline close (0x43a385..0x43a3ab) and the switch at 0x43a3cb.
        const std::uint8_t skill = ReadProtected(actor.byte38e, actor.byte38eAccesses, context.random);
        action = IsOneOf(KalimaSecondActionSkills, skill) ? SecondAttack : FirstAttack;
    }
    else if (IsOneOf(KalimaElevenGroups, kind))
    {
        // 0x43a3fa..0x43a48b (closes with 0x40b0a0) and 0x43a48c..0x43a533 (inline): 0x11, action 4; otherwise 3.
        const std::uint8_t skill = ReadProtected(actor.byte38e, actor.byte38eAccesses, context.random);
        action = skill == KalimaSkill ? SecondAttack : FirstAttack;
    }
    else if (IsOneOf(KalimaFourthGroup, kind))
    {
        // 0x43a534..0x43a5ce: 1, action 4; 0x11, action 3; anything else, none.
        const std::uint8_t skill = ReadProtected(actor.byte38e, actor.byte38eAccesses, context.random);
        if (skill == FirstKalimaSkill)
            action = SecondAttack;
        else if (skill == KalimaSkill)
            action = FirstAttack;
    }
    else if (IsOneOf(KalimaDrawingGroup, kind))
    {
        action = static_cast<std::uint8_t>((context.random.Next() & 1) + FirstAttack);
    }
    else
    {
        return false;
    }
    if (action)
    {
        if (auto set = SetTheAction(actor, context, *action); !set.HasValue())
            return Core::Unexpected(set.Error());
    }
    return true;
}

// 0x443750: the 0x123 of map 0x1f (0x43ff10). With +0x38e open, '2' (0x32) gives action 3 and anything else 4
// (0x44382a..0x443897); then it closes the entry (0x443946..0x443974). It always handles the actor.
Core::Expected<bool, std::string> AttackOfTheTrials(Kernel::ActorState& actor, const ActorCombatContext& context)
{
    constexpr std::int32_t TrialsMap = Maps::LandOfTrialsMap;
    constexpr std::uint16_t TrialsKind = 0x123;
    constexpr std::uint8_t FirstActionSkill = 0x32;
    if (context.world.Runtime().map != TrialsMap || actor.word376 != TrialsKind)
        return false;
    const Models::LogicalAccess access(actor.byte38eAccesses, context.random, ByteCloseRandoms);
    if (auto set = SetTheAction(actor, context, actor.byte38e == FirstActionSkill ? FirstAttack : SecondAttack);
        !set.HasValue())
        return Core::Unexpected(set.Error());
    return true;
}

// 0x4f92f0: with +0x42e open, looks for its value (unsigned) among the five skills of +0x376 in MonsterSkill.bmd
// (0x7437670 + +0x376 · 0x18); in slots 0 to 4, actions 3, 4, 8, 9 and 0xb (the table at 0x4f9564). Then it leaves
// +0x42e at 0xff (0x4f9476) and closes the entry. It returns whether it found it.
Core::Expected<bool, std::string> AttackFromTheSkillTable(Kernel::ActorState& actor, const ActorCombatContext& context)
{
    constexpr std::array<std::uint8_t, Tables::MonsterSkillCount> SkillActions{3, 4, 8, 9, 0xb};
    constexpr std::uint8_t UsedSkill = 0xff;
    const Models::LogicalAccess access(actor.byte42eAccesses, context.random, ByteCloseRandoms);
    const Tables::MonsterSkills listed = context.world.Runtime().monsterSkills.Of(actor.word376);
    const auto found = std::ranges::find(listed.skills, static_cast<std::int32_t>(actor.byte42e));
    const bool matched = found != listed.skills.end();
    if (matched)
    {
        const auto index = static_cast<std::size_t>(found - listed.skills.begin());
        if (auto set = SetTheAction(actor, context, SkillActions[index]); !set.HasValue())
            return Core::Unexpected(set.Error());
    }
    actor.byte42e = UsedSkill;
    return matched;
}

// 0x429270 (0x425070: maps 0x21 and 0x36) with 0x130, 0x131 and 0x135, and 0x437580 (0x4303c0: 0x22, Crywolf) with
// 0x154, 0x155, 0x158, 0x159 and 0x15d (the table at 0x4375cc): both call 0x4f92f0 and return its result.
Core::Expected<bool, std::string> AttackOfTheSkillMaps(Kernel::ActorState& actor, const ActorCombatContext& context)
{
    constexpr std::array<std::uint16_t, 3> DoppelgangerKinds{0x130, 0x131, 0x135};
    constexpr std::array<std::uint16_t, 5> CrywolfKinds{0x154, 0x155, 0x158, 0x159, 0x15d};
    const std::int32_t map = context.world.Runtime().map;
    const bool doppelganger = (map == 0x21 || map == 0x36) && IsOneOf(DoppelgangerKinds, actor.word376);
    const bool crywolf = map == 0x22 && IsOneOf(CrywolfKinds, actor.word376);
    if (!doppelganger && !crywolf)
        return false;
    return AttackFromTheSkillTable(actor, context);
}

// 0x4f5034..0x4f50b2: the five helpers, in their order and all of them (0x43a2f0, 0x42dcb0, 0x443750, 0x429270 and
// 0x437580), and action 3 or 4 by +0x38f if none handled it (BL at 1). 0x437580 at 1 jumps straight to 0x4f50b8.
Core::Expected<void, std::string> MonsterAttack(Kernel::ActorState& actor, const ActorCombatContext& context)
{
    auto kalima = AttackOfKalima(actor, context);
    if (!kalima.HasValue())
        return Core::Unexpected(kalima.Error());
    // 0x42dcb0, the second: the 0x19e and the 0x19f set +0x110 to 3 and action 5.
    const bool special = actor.type == 0x19e || actor.type == 0x19f;
    if (special)
    {
        actor.byte110 = 3;
        if (auto set = SetTheAction(actor, context, 5); !set.HasValue())
            return set;
    }
    auto trials = AttackOfTheTrials(actor, context);
    if (!trials.HasValue())
        return Core::Unexpected(trials.Error());
    // 0x429270 and 0x437580 do not overlap: their maps differ.
    auto skilled = AttackOfTheSkillMaps(actor, context);
    if (!skilled.HasValue())
        return Core::Unexpected(skilled.Error());
    if (kalima.Value() || special || trials.Value() || skilled.Value())
        return {};
    if (auto set = SetTheAction(actor, context, actor.attackCount % 3 == 0 ? 3 : 4); !set.HasValue())
        return set;
    ++actor.attackCount;
    return {};
}

// 0x4f50cf..0x4f51bb: by the weapon.
void PlayerAttackSound(const Kernel::ActorState& actor, const ActorCombatContext& context)
{
    const auto requestSound = [&actor, &context](Audio::SoundId id)
    { context.world.Runtime().RequestSound(id, actor.position); };
    const int left = Signed(actor.hands[1].model);
    const int right = Signed(actor.hands[0].model);
    if (IsLeftGroup4(left))
        requestSound(Audio::SoundId{0x55});
    else if (IsRightGroup4(right))
        requestSound(Audio::SoundId{0x56});
    else if (right >= 0xa1f && right < 0xa21)
        requestSound(Sound::MagicSound);
    else if (right == 0x21c || right == 0x812)
        requestSound(Audio::SoundId{0x3e});
    else if (right != NoModel || left != NoModel)
        requestSound(Audio::SoundId{static_cast<std::int32_t>((context.random.Next() & 1) + 0x3c)});
}

// 0x4f5acb..0x4f5ad8: with subtype 9, 5 more (the table's other sounds, in DrawModelSound).
std::int32_t SubtypeOffset(const Kernel::ActorState& actor)
{
    constexpr std::int32_t NinthSubtype = 9;
    return actor.subtype == NinthSubtype ? 5 : 0;
}

// The model's table: the context's or, without it, the one 0x612c80 wrote (WorldRuntime::modelSounds).
std::optional<Sound::ModelSounds> SoundsOf(const Kernel::ActorState& actor, const ActorCombatContext& context)
{
    if (context.modelSounds)
        return context.modelSounds(actor.type);
    return Sound::WrittenSounds(context.world.Runtime().modelSounds, actor.type);
}

void AttackSound(const Kernel::ActorState& actor, const ActorCombatContext& context)
{
    if (!FrameIsZero(actor))
        return;
    if (actor.type == Tables::PlayerSkeletonModel && !HasModelSoundSubtype(actor))
    {
        PlayerAttackSound(actor, context);
        return;
    }
    const auto requestSound = [&actor, &context](Audio::SoundId id)
    { context.world.Runtime().RequestSound(id, actor.position); };
    // 0x4f51c0..0x4f51ee
    if (HasModelSoundSubtype(actor))
        requestSound(Audio::SoundId{static_cast<std::int32_t>((context.random.Next() & 1) + 0x3c)});
    // 0x4f51f1..0x4f5262: WORD 2 or 3 of the model's table (DrawModelSound).
    constexpr std::size_t FirstAttackSound = 2;
    if (const auto drawn =
            Sound::DrawModelSound(SoundsOf(actor, context), FirstAttackSound, actor.subtype, context.random))
        requestSound(Audio::SoundId{*drawn});
}

// 0x4f5a09..0x4f5ae8: with the frame at 0.
void DeathSound(const Kernel::ActorState& actor, const ActorCombatContext& context)
{
    // 0x4f5a8b and 0x4f5ae8 get the actor as the origin.
    const auto requestSound = [&](Audio::SoundId id) { context.world.Runtime().RequestSound(id, actor.position); };
    const std::int32_t map = context.world.Runtime().map;
    if (map >= FirstChaosCastleMap && map <= LastChaosCastleMap)
    {
        requestSound(Audio::SoundId{static_cast<std::int32_t>((context.random.Next() & 1) + 0x5e)});
        return;
    }
    if (actor.type != Tables::PlayerSkeletonModel || HasModelSoundSubtype(actor))
    {
        // 0x4f5aa8..0x4f5ae8: WORD 4 of the model's table; 0xffff, nothing; otherwise, signed, plus 5 with subtype 9
        // and plus 0xd2. Without the table, 0x60dc80's WORD at 0 (SoundsOrZeros): the 0xd2. No rand().
        const Sound::ModelSounds table = Sound::SoundsOrZeros(SoundsOf(actor, context));
        constexpr std::size_t DeathSoundWord = 4;
        if (table[DeathSoundWord] == -1)
            return;
        constexpr std::int32_t MonsterSoundBase = 0xd2;
        requestSound(Audio::SoundId{table[DeathSoundWord] + SubtypeOffset(actor) + MonsterSoundBase});
        return;
    }
    const Kernel::CharacterClass characterClass = Kernel::BaseClassOf(actor.characterClass);
    requestSound(Audio::SoundId{characterClass == Kernel::CharacterClass::Elf        ? 0x6e
                                : characterClass == Kernel::CharacterClass::DarkLord ? 0x1c7
                                                                                     : 0x6c});
    // 0x4f5a6b
    if (actor.pet.model == Fenrir)
        requestSound(Audio::SoundId{0x278});
}
// The Effects::WizardModelEffects services for 0x4f4a10 and 0x4f5660: the world's particles and terrain.
Effects::WizardEffectServices WizardServicesOf(const ActorCombatContext& context)
{
    Kernel::WorldState& world = context.world;
    return Effects::WizardEffectServicesOf(
        world, context.random,
        Effects::WizardSceneParts{.particles = &world.Particles(),
                                  .terrain = &Terrain::TerrainOrEmpty(context.terrain),
                                  .dynamicLighting = context.dynamicLighting,
                                  .faunaModels = context.faunaModels});
}

// 0x4f589d..0x4f58f6 (the player) and 0x4f5938..0x4f5991 (the 0x170): 0x518cf0(0xd5, +0x10, +0x1c, +0xf4, 0, 0, −1, …)
// once and the 0xd6 ten times, ownerless: the bones that fly off, each with its five rand() (0x51b72b). With the table
// full, nothing and no rand().
Core::Expected<void, std::string> ScatterTheBones(Kernel::ActorState& actor, const ActorCombatContext& context,
                                                  std::uint32_t firstCall, std::uint32_t boneCall)
{
    constexpr int Bones = 10; // [ebp-0x8]
    const Effects::WizardEffectServices services = WizardServicesOf(context);
    Effects::ModelEffects& effects = context.world.Map().effects;
    for (int bone = -1; bone < Bones; ++bone)
    {
        const std::int16_t type = bone < 0 ? Effects::FirstBoneEffect : Effects::SecondBoneEffect;
        const auto created = Effects::CreateWizardModelEffect(
            effects, Effects::WizardArguments(type, actor.position, actor.angles, actor.light, 0), context.random,
            services);
        if (!created.HasValue())
            return Core::Unexpected(std::format("{:#x}: el hueso {:#x} de la muerte: {}",
                                                bone < 0 ? firstCall : boneCall, type, created.Error()));
    }
    return {};
}
} // namespace

ActorCombatContext CombatContextOf(const Scenes::WorldEntryResources& resources)
{
    return ActorCombatContext{.world = resources.world,
                              .random = resources.cloth.random,
                              .playerSkeleton = resources.models.SkeletonOf(Tables::PlayerSkeletonModelId),
                              .models = &resources.models,
                              .items = resources.items,
                              .terrain = &Scenes::TerrainOf(resources),
                              .dynamicLighting = Scenes::DynamicLightingOf(resources),
                              .faunaModels = Scenes::FaunaModelsOf(resources)};
}

Core::Expected<void, std::string> SetActorAttack(Kernel::ActorState& actor, const ActorCombatContext& context)
{
    switch (actor.type)
    {
    case Tables::PlayerSkeletonModel:
        if (auto attacked = PlayerAttack(actor, context); !attacked.HasValue())
            return attacked;
        break;
    case 0x27:
    {
        // 0x4f4f3a..0x4f4f5b: 0x518cf0(0xd4, +0x10, +0x1c, +0xf4, 0, 0, −1, 0, 0, 0, 0), the ownerless saw (with the
        // table full, nothing). Then sound 0x10.
        const auto sawed = Effects::CreateWizardModelEffect(
            context.world.Map().effects,
            Effects::WizardArguments(Effects::SawEffect, actor.position, actor.angles, actor.light, 0), context.random,
            WizardServicesOf(context));
        if (!sawed.HasValue())
            return Core::Unexpected(std::format("0x4f4f5b: la sierra 0xd4 del 0x27: {}", sawed.Error()));
        context.world.Runtime().RequestSound(Audio::SoundId{0x10}, actor.position);
        break;
    }
    case 0x28:
        if (auto set = SetTheAction(actor, context, 1); !set.HasValue())
            return set;
        context.world.Runtime().RequestSound(Audio::SoundId{0x10}, actor.position);
        break;
    case 0x33:
    {
        // 0x4f4f98..0x4f4fc4: 0x518cf0(0x5da, +0x10, +0x1c, +0xf4, 0, 0, −1, 0, 0, 0, 0), the ownerless strike
        // (Effects/SkillBurstEffects; with the table full, nothing), and sound 0x79.
        const auto struck =
            Effects::CreateHighModelEffect({.type = Effects::FlameBurstEffect,
                                            .position = actor.position,
                                            .angles = actor.angles,
                                            .light = actor.light},
                                           Effects::HighEffectContext{.world = context.world,
                                                                      .random = context.random,
                                                                      .models = context.models,
                                                                      .terrain = context.terrain,
                                                                      .faunaModels = context.faunaModels});
        if (!struck.HasValue())
            return Core::Unexpected(std::format("0x4f4fb9: {}", struck.Error()));
        context.world.Runtime().RequestSound(Audio::SoundId{0x79});
        break;
    }
    case 0x172:
    {
        // 0x4f4fd7..0x4f502f
        const auto first = context.random.Next() & 7;
        const auto action = first < 3 ? (first > 0 ? 8u : 9u) : (context.random.Next() & 1) + 3;
        if (auto set = SetTheAction(actor, context, static_cast<std::uint8_t>(action)); !set.HasValue())
            return set;
        break;
    }
    default:
        if (auto attacked = MonsterAttack(actor, context); !attacked.HasValue())
            return attacked;
        break;
    }
    AttackSound(actor, context);
    // 0x4f5265: +0x38f + 1 always (the common monsters already added one at 0x4f50b2).
    ++actor.attackCount;
    return {};
}

Core::Expected<void, std::string> SetActorDie(Kernel::ActorState& actor, const ActorCombatContext& context)
{
    // 0x4f5682..0x4f587b: the hero opens the own record, sets the life (+0x22) to 0 and closes it.
    if (IsHero(actor, context.world))
    {
        const Models::LogicalAccess own(context.world.Runtime().ownRecordDepth, context.random,
                                        Models::OwnRecordCloseRandoms);
        context.world.OwnCharacter().life = 0;
    }
    if (actor.type == Tables::PlayerSkeletonModel && !HasModelSoundSubtype(actor))
    {
        if (auto set = SetTheAction(actor, context, 0xc3); !set.HasValue())
            return set;
    }
    else if (actor.type == Tables::PlayerSkeletonModel || actor.type == 0x16b || actor.type == 0x170)
    {
        // 0x4f58a7, 0x4f5942 and 0x4f599e clear +0 before the first effect creator.
        actor.alive = false;
        if (actor.type == 0x16b)
        {
            // 0x4f59ad..0x4f59f4: eight pairs 0x518cf0(0xe5 and then 0xe6, +0x10, +0x1c, +0xf4, 0, 0, −1, 0, 0, 0, 0),
            // the chips that fly off (Effects::ModelEffects::CreateStoneChip, 0x51b659), each with its eight rand().
            // With the table full, nothing and no rand(); it goes on with the sound and the frame.
            constexpr int Pairs = 8; // [ebp-0x8]
            Effects::ModelEffects& effects = context.world.Map().effects;
            for (int pair = 0; pair < Pairs; ++pair)
            {
                static_cast<void>(effects.CreateStoneChip(Effects::FirstSplinterEffect, actor.position, actor.angles,
                                                          actor.light, context.random));
                static_cast<void>(effects.CreateStoneChip(Effects::SecondSplinterEffect, actor.position, actor.angles,
                                                          actor.light, context.random));
            }
        }
        else
        {
            const bool player = actor.type == Tables::PlayerSkeletonModel;
            if (auto bones =
                    ScatterTheBones(actor, context, player ? 0x4f58c2u : 0x4f595du, player ? 0x4f58e7u : 0x4f5982u);
                !bones.HasValue())
                return bones;
        }
        context.world.Runtime().RequestSound(Audio::SoundId{0x7c}, actor.position); // 0x4f59fe
    }
    else if (auto set = SetTheAction(actor, context, 6); !set.HasValue())
    {
        return set;
    }
    if (!FrameIsZero(actor))
        return {};
    DeathSound(actor, context);
    actor.animation.frame = DeathFrame;
    return {};
}
} // namespace Client::Combat
