#include "Actors/Emote.h"

#include "Core/Expected.h"
#include "Kernel/CharacterClass.h"
#include "Models/Animation.h"
#include "Models/IModelLibrary.h"
#include "Actors/ActionSend.h"
#include "Kernel/ActorState.h"
#include "Tables/AddedEmotes.h"
#include "UI/Emotes.h"

#include <cstdint>
#include <format>
#include <string>

namespace Client::Actors
{
namespace
{
// 0x576a84..0x576a94: the current action, from 1 to 12 or 0x8a.
constexpr std::uint8_t FirstStandAction = 1;
constexpr std::uint8_t LastStandAction = 0xc;
constexpr std::uint8_t MarkedSpecialStand = 0x8a;
// 0x576a9d..0x576abc: the elf (+0x1d4 & 7 == 2) and the animations without the elf's, from 0xb7 to 0xc0 (signed CMP).
constexpr std::uint8_t FirstSharedAnimation = 0xb7;
constexpr std::uint8_t LastSharedAnimation = 0xc0;

bool IsStanding(std::uint8_t action)
{
    return (action >= FirstStandAction && action <= LastStandAction) || action == MarkedSpecialStand;
}

bool IsElf(const Kernel::ActorState& actor)
{
    return Kernel::BaseClassOf(actor.characterClass) == Kernel::CharacterClass::Elf;
}

std::uint8_t AnimationFor(const Kernel::ActorState& actor, std::uint8_t animation)
{
    const bool shared = animation >= FirstSharedAnimation && animation <= LastSharedAnimation;
    return IsElf(actor) && !shared ? static_cast<std::uint8_t>(animation + 1) : animation;
}

Core::Expected<bool, std::string> PlayTheAnimation(Kernel::ActorState& actor, std::uint8_t animation,
                                                   std::uint8_t action, const EmoteContext& context)
{
    if (!IsStanding(actor.animation.action))
        return false;
    // 0x576abf: 0x4ea030 with the model of the character's type (+2), not always Player.bmd's.
    const auto* skeleton = context.models.SkeletonOf(Models::ModelId{actor.type});
    if (skeleton == nullptr)
        return Core::Unexpected(
            std::format("0x576abf: el emote sin las acciones del modelo {:#x} del personaje (0x4ea030)", actor.type));
    Models::SetAction(actor.animation, *skeleton, animation);
    // 0x576ac7..0x576e97: C1 05 18 with the direction of [0x743a170] + 0x24 and the action, XOR32 inline.
    if (auto sent = SendTheAction(context.hero.angles.z, action, context.send, "0x576b3e"); !sent.HasValue())
        return Core::Unexpected(sent.Error());
    return true;
}

Core::Expected<void, std::string> PlayAndSend(std::uint8_t animation, std::uint8_t action, const EmoteContext& context)
{
    if (auto played = PlayTheAnimation(context.hero, animation, action, context); !played.HasValue())
        return Core::Unexpected(played.Error());
    // The case's C1 05 18 (0x5777aa..0x577b75 in 0x577796, the same in the other 21), with the ESI of 0x576ed4.
    return SendTheAction(context.hero.angles.z, action, context.send, "0x577823");
}
} // namespace

Core::Expected<bool, std::string> PlayTheEmote(Kernel::ActorState& actor, UI::EmoteCase emote,
                                               const EmoteContext& context)
{
    return PlayTheAnimation(actor, AnimationFor(actor, emote.animation), emote.action, context);
}

Core::Expected<void, std::string> DoTheEmote(UI::EmoteCase emote, const EmoteContext& context)
{
    return PlayAndSend(AnimationFor(context.hero, emote.animation), emote.action, context);
}

Core::Expected<void, std::string> DoTheAddedEmote(const Tables::AddedEmote& emote, const EmoteContext& context)
{
    return PlayAndSend(IsElf(context.hero) ? emote.elfAction : emote.action, emote.action, context);
}
} // namespace Client::Actors
