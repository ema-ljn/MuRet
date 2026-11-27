#include "Actors/ActorWalk.h"

#include "Core/Expected.h"
#include "Crt/CrtRandom.h"
#include "Kernel/CharacterClass.h"
#include "Tables/PlayerModels.h"
#include "Tables/ItemTable.h"
#include "Models/Animation.h"
#include "Kernel/ActorState.h"
#include "Sound/MonsterSounds.h"
#include "Actors/PlayerActionSpeeds.h"
#include "Kernel/WorldRuntime.h"
#include "Audio/SoundId.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <tuple>

namespace Client::Actors
{
namespace
{
constexpr std::uint8_t RunThreshold = 40;
constexpr std::uint16_t Fenrir = 0x1c37;
constexpr std::uint16_t DarkHorse = 0x1c16;
constexpr std::uint16_t Raven = 0x1c17;
constexpr std::uint16_t FlyingMount = 0x1c15;
constexpr std::uint16_t GroundMount = 0x1c14;
constexpr std::uint32_t SlowFlag = 2;
constexpr std::size_t WeaponMarkOffset = 0x1e;
constexpr Kernel::CharacterClass FirstRunningClass = Kernel::CharacterClass::MagicGladiator;
constexpr Kernel::CharacterClass SecondRunningClass = Kernel::CharacterClass::DarkLord;
constexpr std::uint8_t RunningPartLevel = 5;
constexpr std::size_t Gloves = 3;
constexpr std::size_t Boots = 4;
constexpr std::uint16_t BowSentinel = 0xa19;
constexpr std::uint16_t CrossbowSentinel = 0xa21;
constexpr std::int32_t FastFenrirCounter = 20;

bool IsSwimmingMap(std::int32_t map)
{
    constexpr std::int32_t Atlans = 7;
    constexpr std::int32_t FirstKalima = 24;
    constexpr std::int32_t LastKalima = 29;
    constexpr std::int32_t SeventhKalima = 36;
    return map == Atlans || (map >= FirstKalima && map <= LastKalima) || map == SeventhKalima;
}

void UpdateRun(Kernel::ActorState& actor, Kernel::WorldRuntime& runtime)
{
    if (actor.safeZone != 0)
    {
        actor.run = 0;
        return;
    }
    if (actor.run >= RunThreshold)
    {
        return;
    }
    const Kernel::CharacterClass characterClass = Kernel::BaseClassOf(actor.characterClass);
    const Kernel::ActorItemPart& part = actor.body[IsSwimmingMap(runtime.map) ? Gloves : Boots];
    const bool canRun = characterClass == FirstRunningClass || characterClass == SecondRunningClass ||
                        (part.model != Kernel::NoItemModel && part.level >= RunningPartLevel);
    if (actor.pet.model == Fenrir)
    {
        ++runtime.movementSpeed.itemCounterBits;
    }
    else if (canRun)
    {
        ++actor.run;
    }
}

int Signed(std::uint16_t model)
{
    return static_cast<std::int16_t>(model);
}

bool RightBow(int model)
{
    return (model >= 0xa1a && model < 0xa21) || model == 0xa22 || (model >= 0xa24 && model < 0xc12);
}

bool LeftBow(int model)
{
    return (model >= 0xa12 && model < 0xa19) || model == 0xa23 || model == 0xa26 || model == 0xa27;
}

Core::Expected<bool, std::string> Marked(int model, const Tables::ItemTable* items)
{
    if (items == nullptr)
    {
        return Core::Unexpected(std::string("0x4f3b5f: falta Item.bmd para la marca +0x1e del arma"));
    }
    const auto record = items->Record(static_cast<std::size_t>(model - Tables::FirstItemModel));
    if (!record)
    {
        return Core::Unexpected(std::string("0x4f3b5f: falta el registro del arma en Item.bmd"));
    }
    return (*record)[WeaponMarkOffset] != std::byte{0};
}

Core::Expected<std::uint8_t, std::string> MarkedAction(int right, const Tables::ItemTable* items, bool running,
                                                       std::uint8_t marked)
{
    const auto mark = Marked(right, items);
    if (!mark.HasValue())
    {
        return Core::Unexpected(mark.Error());
    }
    return mark.Value() ? marked : static_cast<std::uint8_t>(running ? 0x17 : 0xf);
}

Core::Expected<std::uint8_t, std::string> GroundAction(const Kernel::ActorState& actor, const ActorWalkContext& context)
{
    const int right = Signed(actor.hands[0].model);
    const int left = Signed(actor.hands[1].model);
    const bool running = actor.run >= RunThreshold;
    const std::uint8_t unarmed =
        running ? 0x16 : (Kernel::BaseClassOf(actor.characterClass) == Kernel::CharacterClass::Elf ? 0xe : 0xd);
    const std::int32_t map = context.stop.runtime.map;
    if ((right == -1 && left == -1) || (actor.safeZone != 0 && (map < 11 || map > 23)))
    {
        return unarmed;
    }
    if (right >= 0x212 && right < 0x812)
    {
        // 0x4f3c7c: two weapons of these groups while running avoid reading Item.bmd.
        if (running && left >= 0x212 && left < 0x812)
        {
            return std::uint8_t{0x18};
        }
        const bool special = right == 0x227 || right == 0x229 || right == 0x231;
        return MarkedAction(right, context.stop.items, running,
                            static_cast<std::uint8_t>(special ? (running ? 0x8c : 0x8b) : (running ? 0x19 : 0x10)));
    }
    if (right >= 0xc12 && right < 0xe12)
    {
        return MarkedAction(right, context.stop.items, running, static_cast<std::uint8_t>(running ? 0x1a : 0x12));
    }
    // 0x4f3bca/0x4f3bd4: the second literal is 0xa14, not 0x814.
    if (!running && (right == 0x813 || right == 0xa14))
    {
        return std::uint8_t{0x11};
    }
    if (right >= 0x812 && right < 0xa12)
    {
        return static_cast<std::uint8_t>(running ? 0x1a : 0x12);
    }
    if (LeftBow(left))
    {
        return static_cast<std::uint8_t>(running ? 0x1b : 0x13);
    }
    return RightBow(right) ? static_cast<std::uint8_t>(running ? 0x1c : 0x14) : unarmed;
}

// The helpers 0x4e9f50/0x4e9da0 keep the bow and crossbow sentinels before looking at the class.
std::uint8_t FenrirAction(const Kernel::ActorState& actor, bool fast)
{
    const std::uint16_t right = actor.hands[0].model;
    const std::uint16_t left = actor.hands[1].model;
    unsigned bucket = 0;
    bool forcedElf = false;
    if (right != Kernel::NoItemModel && left != Kernel::NoItemModel && right != CrossbowSentinel && left != BowSentinel)
    {
        bucket = 1;
    }
    else if (right != Kernel::NoItemModel && (left == Kernel::NoItemModel || left == BowSentinel))
    {
        bucket = 2;
        forcedElf = left == BowSentinel;
    }
    else if (left != Kernel::NoItemModel)
    {
        bucket = 3;
        forcedElf = right == CrossbowSentinel;
    }
    if (!fast)
    {
        return static_cast<std::uint8_t>(0x7a + bucket);
    }
    const Kernel::CharacterClass characterClass = Kernel::BaseClassOf(actor.characterClass);
    const unsigned base = forcedElf || characterClass == Kernel::CharacterClass::Elf ? 0x72U
                          : characterClass == FirstRunningClass                      ? 0x6eU
                                                                                     : 0x6aU;
    return static_cast<std::uint8_t>(base + bucket);
}

Core::Expected<std::optional<std::uint8_t>, std::string> ChooseAction(const Kernel::ActorState& actor,
                                                                      const ActorWalkContext& context)
{
    const bool safe = actor.safeZone != 0;
    const auto chosen = [](std::uint8_t action) { return std::optional{action}; };
    if (actor.type != Tables::PlayerSkeletonModel)
    {
        return chosen(2);
    }
    if (actor.pet.model == Fenrir && !safe)
    {
        const bool fast =
            std::bit_cast<std::int32_t>(context.stop.runtime.movementSpeed.itemCounterBits) >= FastFenrirCounter;
        return chosen(FenrirAction(actor, fast));
    }
    if (actor.pet.model == DarkHorse && !safe)
    {
        return chosen(0x4b);
    }
    if (actor.hands[1].model == Raven && safe)
    {
        return chosen(0x49);
    }
    if ((actor.pet.model == GroundMount || actor.pet.model == FlyingMount) && !safe)
    {
        const auto map = context.stop.runtime.map;
        if (actor.pet.model == FlyingMount && (map == 8 || map == 10))
        {
            return std::optional<std::uint8_t>{}; // 0x4f3a14: keeps the action, but runs the suffix.
        }
        return chosen(
            actor.hands[0].model == Kernel::NoItemModel && actor.hands[1].model == Kernel::NoItemModel ? 0x20 : 0x21);
    }
    if (actor.wings.model != Kernel::NoItemModel && !safe)
    {
        return chosen(RightBow(Signed(actor.hands[0].model)) ? 0x1f : 0x1e);
    }
    if (!safe && IsSwimmingMap(context.stop.runtime.map))
    {
        return chosen(actor.run < RunThreshold ? 0x15 : 0x1d);
    }
    const auto ground = GroundAction(actor, context);
    if (!ground.HasValue())
    {
        return Core::Unexpected(ground.Error());
    }
    return chosen(ground.Value());
}

std::optional<std::int32_t> FootstepSound(const Kernel::ActorState& actor, std::int32_t map, std::int32_t layer)
{
    constexpr std::uint8_t FirstFlyingAction = 0x1e;
    constexpr std::uint8_t SecondFlyingAction = 0x1f;
    if (actor.animation.action == FirstFlyingAction || actor.animation.action == SecondFlyingAction)
    {
        return std::nullopt;
    }
    if (map == 2 && layer != 3 && layer < 10)
    {
        return 10;
    }
    if ((map == 0 || map == 3) && layer == 0)
    {
        return 9;
    }
    return IsSwimmingMap(map) && actor.safeZone == 0 ? 11 : 8;
}

Core::Expected<ActorWalkResult, std::string> StepSound(const Kernel::ActorState& actor, const ActorWalkContext& context)
{
    auto& random = context.stop.random;
    const auto requestSound = [&context, &actor](Audio::SoundId sound)
    { context.stop.runtime.RequestSound(sound, actor.position); };
    if (actor.type == 0x16d)
    {
        requestSound(Audio::SoundId{0x7c});
        return ActorWalkResult{.sound = Audio::SoundId{0x7c}};
    }
    if (Kernel::BaseClassOf(actor.characterClass) == SecondRunningClass && actor.pet.model == DarkHorse &&
        actor.safeZone == 0)
    {
        const std::int32_t sound = 0x1c3 + random.Next() % 3;
        requestSound(Audio::SoundId{sound});
        return ActorWalkResult{.sound = Audio::SoundId{sound}};
    }
    const std::uint8_t action = actor.animation.action;
    if (actor.pet.model == Fenrir && actor.safeZone == 0)
    {
        if (action >= 0x6a && action <= 0x75)
        {
            const std::int32_t sound = 0x273 + random.Next() % 3;
            requestSound(Audio::SoundId{sound});
            return ActorWalkResult{.sound = Audio::SoundId{sound}};
        }
        if (action >= 0x7a && action <= 0x7d)
        {
            const std::int32_t sound = 0x273 + (random.Next() & 1);
            requestSound(Audio::SoundId{sound});
            return ActorWalkResult{.sound = Audio::SoundId{sound}};
        }
    }
    const int mask = context.hero ? 63 : 15;
    const bool selected = (random.Next() & mask) == 0;
    const bool modelSound = actor.subtype >= 209 && actor.subtype <= 211;
    if (!selected || (actor.type == Tables::PlayerSkeletonModel && !modelSound))
    {
        return ActorWalkResult{};
    }
    // 0x4f3edc..0x4f3f6c: subtypes 209 to 211 request 0x7b before picking WORD 0 or 1 of the model's table.
    // The second request does not erase the first even though it is the last one left in the diagnostic.
    ActorWalkResult result;
    if (modelSound)
    {
        requestSound(Audio::SoundId{0x7b});
        result.sound = Audio::SoundId{0x7b};
    }
    std::optional<Sound::ModelSounds> sounds = Sound::WrittenSounds(context.stop.runtime.modelSounds, actor.type);
    if (context.modelSounds.size() == std::tuple_size_v<Sound::ModelSounds>)
    {
        sounds.emplace();
        std::ranges::copy(context.modelSounds, sounds->begin());
    }
    constexpr std::size_t FirstWalkSound = 0;
    if (const auto drawn = Sound::DrawModelSound(sounds, FirstWalkSound, actor.subtype, random))
    {
        requestSound(Audio::SoundId{*drawn});
        result.sound = Audio::SoundId{*drawn};
    }
    return result;
}
} // namespace

Core::Expected<ActorWalkResult, std::string> SetActorWalk(Kernel::ActorState& actor, const ActorWalkContext& context)
{
    UpdateRun(actor, context.stop.runtime);
    if (actor.type == Tables::PlayerSkeletonModel)
    {
        context.actionSpeeds.SetWalking((actor.flags78 & SlowFlag) != 0);
    }
    const auto action = ChooseAction(actor, context);
    if (!action.HasValue())
    {
        return Core::Unexpected(action.Error());
    }
    if (action.Value())
    {
        if (actor.type == Tables::PlayerSkeletonModel)
        {
            Models::SetAction(actor.animation, context.stop.playerSkeleton, *action.Value());
        }
        else if (context.actorSkeleton != nullptr)
        {
            Models::SetAction(actor.animation, *context.actorSkeleton, *action.Value());
        }
    }
    return StepSound(actor, context);
}
std::array<std::optional<std::int32_t>, 2> TakeActorFootstepSounds(Kernel::ActorState& actor, std::int32_t map,
                                                                   std::int32_t heroCellLayer)
{
    constexpr std::uint8_t FirstWalkAction = 0xd;
    constexpr std::uint8_t LastWalkAction = 0x21;
    constexpr std::uint8_t SpecialWalkAction = 0x8b;
    constexpr std::uint8_t SpecialRunAction = 0x8c;
    constexpr std::uint8_t HorseWalkAction = 0x4b;
    std::array<std::optional<std::int32_t>, 2> sounds{};
    const auto action = actor.animation.action;
    if (actor.type != Tables::PlayerSkeletonModel ||
        !((action >= FirstWalkAction && action <= LastWalkAction) || action == SpecialWalkAction ||
          action == SpecialRunAction || action == HorseWalkAction))
    {
        return sounds;
    }
    constexpr std::array<float, 2> Thresholds{1.5f, 4.5f};
    const std::array<std::uint8_t*, 2> latches{&actor.firstFootstepSound, &actor.secondFootstepSound};
    for (std::size_t step = 0; step < sounds.size(); ++step)
    {
        if (actor.animation.frame >= Thresholds[step] && *latches[step] == 0)
        {
            *latches[step] = 1;
            sounds[step] = FootstepSound(actor, map, heroCellLayer);
        }
    }
    return sounds;
}
} // namespace Client::Actors
