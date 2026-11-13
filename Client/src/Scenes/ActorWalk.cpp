#include "Scenes/ActorWalk.h"

#include "Core/Expected.h"
#include "Data/Models/PlayerModels.h"
#include "Data/Tables/ItemTable.h"
#include "Render/Models/Animation.h"
#include "Render/Models/Cloth.h"
#include "Scenes/ActorState.h"
#include "Scenes/MonsterSounds.h"
#include "Scenes/PlayerActionSpeeds.h"
#include "Scenes/WorldRuntime.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
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
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t FirstRunningClass = 3;
constexpr std::uint8_t SecondRunningClass = 4;
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

void UpdateRun(ActorState& actor, WorldRuntime& runtime)
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
    const std::uint8_t characterClass = actor.characterClass & ClassMask;
    const ActorItemPart& part = actor.body[IsSwimmingMap(runtime.map) ? Gloves : Boots];
    const bool canRun = characterClass == FirstRunningClass || characterClass == SecondRunningClass ||
                        (part.model != NoItemModel && part.level >= RunningPartLevel);
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

Core::Expected<bool, std::string> Marked(int model, const Data::Tables::ItemTable* items)
{
    if (items == nullptr)
    {
        return Core::Unexpected(std::string("0x4f3b5f: falta Item.bmd para la marca +0x1e del arma"));
    }
    const auto record = items->Record(static_cast<std::size_t>(model - Data::Models::FirstItemModel));
    if (!record)
    {
        return Core::Unexpected(std::string("0x4f3b5f: falta el registro del arma en Item.bmd"));
    }
    return (*record)[WeaponMarkOffset] != std::byte{0};
}

Core::Expected<std::uint8_t, std::string> MarkedAction(int right, const Data::Tables::ItemTable* items, bool running,
                                                       std::uint8_t marked)
{
    const auto mark = Marked(right, items);
    if (!mark.HasValue())
    {
        return Core::Unexpected(mark.Error());
    }
    return mark.Value() ? marked : static_cast<std::uint8_t>(running ? 0x17 : 0xf);
}

Core::Expected<std::uint8_t, std::string> GroundAction(const ActorState& actor, const ActorWalkContext& context)
{
    const int right = Signed(actor.hands[0].model);
    const int left = Signed(actor.hands[1].model);
    const bool running = actor.run >= RunThreshold;
    const std::uint8_t unarmed = running ? 0x16 : ((actor.characterClass & ClassMask) == ElfClass ? 0xe : 0xd);
    const std::int32_t map = context.stop.runtime.map;
    if ((right == -1 && left == -1) || (actor.safeZone != 0 && (map < 11 || map > 23)))
    {
        return unarmed;
    }
    if (right >= 0x212 && right < 0x812)
    {
        // 0x4f3c7c: dos armas de estos grupos corriendo evitan leer Item.bmd.
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
    // 0x4f3bca/0x4f3bd4: el segundo literal es 0xa14, no 0x814.
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

// Los helpers 0x4e9f50/0x4e9da0 conservan los sentinelas de arco/ballesta antes de mirar clase.
std::uint8_t FenrirAction(const ActorState& actor, bool fast)
{
    const std::uint16_t right = actor.hands[0].model;
    const std::uint16_t left = actor.hands[1].model;
    unsigned bucket = 0;
    bool forcedElf = false;
    if (right != NoItemModel && left != NoItemModel && right != CrossbowSentinel && left != BowSentinel)
    {
        bucket = 1;
    }
    else if (right != NoItemModel && (left == NoItemModel || left == BowSentinel))
    {
        bucket = 2;
        forcedElf = left == BowSentinel;
    }
    else if (left != NoItemModel)
    {
        bucket = 3;
        forcedElf = right == CrossbowSentinel;
    }
    if (!fast)
    {
        return static_cast<std::uint8_t>(0x7a + bucket);
    }
    const std::uint8_t characterClass = actor.characterClass & ClassMask;
    const unsigned base = forcedElf || characterClass == ElfClass ? 0x72U
                          : characterClass == FirstRunningClass   ? 0x6eU
                                                                  : 0x6aU;
    return static_cast<std::uint8_t>(base + bucket);
}

Core::Expected<std::optional<std::uint8_t>, std::string> ChooseAction(const ActorState& actor,
                                                                      const ActorWalkContext& context)
{
    const bool safe = actor.safeZone != 0;
    const auto chosen = [](std::uint8_t action) { return std::optional{action}; };
    if (actor.type != Data::Models::PlayerSkeletonModel)
    {
        if (context.actorSkeleton == nullptr)
        {
            return Core::Unexpected(std::string("0x4f3d83: falta el esqueleto propio del personaje no jugador"));
        }
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
            return std::optional<std::uint8_t>{}; // 0x4f3a14: conserva la acción, pero corre el sufijo.
        }
        return chosen(actor.hands[0].model == NoItemModel && actor.hands[1].model == NoItemModel ? 0x20 : 0x21);
    }
    if (actor.wings.model != NoItemModel && !safe)
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

std::optional<std::int32_t> FootstepSound(const ActorState& actor, std::int32_t map, std::int32_t layer)
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

Core::Expected<ActorWalkResult, std::string> StepSound(const ActorState& actor, const ActorWalkContext& context)
{
    auto& random = context.stop.random;
    if (actor.type == 0x16d)
    {
        return ActorWalkResult{.sound = 0x7c};
    }
    if ((actor.characterClass & ClassMask) == SecondRunningClass && actor.pet.model == DarkHorse && actor.safeZone == 0)
    {
        return ActorWalkResult{.sound = 0x1c3 + random.Next() % 3};
    }
    const std::uint8_t action = actor.animation.action;
    if (actor.pet.model == Fenrir && actor.safeZone == 0)
    {
        if (action >= 0x6a && action <= 0x75)
        {
            return ActorWalkResult{.sound = 0x273 + random.Next() % 3};
        }
        if (action >= 0x7a && action <= 0x7d)
        {
            return ActorWalkResult{.sound = 0x273 + (random.Next() & 1)};
        }
    }
    const int mask = context.hero ? 63 : 15;
    const bool selected = (random.Next() & mask) == 0;
    const bool modelSound = actor.subtype >= 209 && actor.subtype <= 211;
    if (!selected || (actor.type == Data::Models::PlayerSkeletonModel && !modelSound))
    {
        return ActorWalkResult{};
    }
    // 0x4f3edc..0x4f3f6c: los subtipos 209 a 211 piden el 0x7b, y después todos la WORD 0 o la 1 de la tabla del modelo
    // (DrawModelSound, la que escribió el alta: un 0x20a no la tiene). El último sonido es el que queda.
    ActorWalkResult result;
    if (modelSound)
        result.sound = 0x7b;
    constexpr std::size_t FirstWalkSound = 0;
    if (const auto drawn = DrawModelSound(WrittenSounds(context.stop.runtime.modelSounds, actor.type), FirstWalkSound,
                                          actor.subtype, random))
        result.sound = drawn;
    return result;
}
} // namespace

Core::Expected<ActorWalkResult, std::string> SetActorWalk(ActorState& actor, const ActorWalkContext& context)
{
    UpdateRun(actor, context.stop.runtime);
    if (actor.type == Data::Models::PlayerSkeletonModel)
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
        const auto& skeleton =
            actor.type == Data::Models::PlayerSkeletonModel ? context.stop.playerSkeleton : *context.actorSkeleton;
        Render::Models::SetAction(actor.animation, skeleton, *action.Value());
    }
    return StepSound(actor, context);
}
std::array<std::optional<std::int32_t>, 2> TakeActorFootstepSounds(ActorState& actor, std::int32_t map,
                                                                   std::int32_t heroCellLayer)
{
    constexpr std::uint8_t FirstWalkAction = 0xd;
    constexpr std::uint8_t LastWalkAction = 0x21;
    constexpr std::uint8_t SpecialWalkAction = 0x8b;
    constexpr std::uint8_t SpecialRunAction = 0x8c;
    constexpr std::uint8_t HorseWalkAction = 0x4b;
    std::array<std::optional<std::int32_t>, 2> sounds{};
    const auto action = actor.animation.action;
    if (actor.type != Data::Models::PlayerSkeletonModel ||
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
} // namespace Client::Scenes
