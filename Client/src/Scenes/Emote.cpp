#include "Scenes/Emote.h"

#include "Core/Expected.h"
#include "Render/Models/Animation.h"
#include "Scenes/ActionSend.h"
#include "Scenes/ActorState.h"
#include "Scenes/CharacterDrawing.h"
#include "UI/Emotes.h"

#include <cstdint>
#include <format>
#include <string>

namespace Client::Scenes
{
namespace
{
// 0x576a84..0x576a94: la acción de ahora, de 1 a 12 o 0x8a.
constexpr std::uint8_t FirstStandAction = 1;
constexpr std::uint8_t LastStandAction = 0xc;
constexpr std::uint8_t MarkedSpecialStand = 0x8a;
// 0x576a9d..0x576abc: el elfo (+0x1d4 & 7 == 2) y las animaciones sin la del elfo, de 0xb7 a 0xc0 (CMP con signo).
constexpr std::uint8_t ClassMask = 7;
constexpr std::uint8_t ElfClass = 2;
constexpr std::uint8_t FirstSharedAnimation = 0xb7;
constexpr std::uint8_t LastSharedAnimation = 0xc0;

bool IsStanding(std::uint8_t action)
{
    return (action >= FirstStandAction && action <= LastStandAction) || action == MarkedSpecialStand;
}

std::uint8_t AnimationFor(const ActorState& actor, std::uint8_t animation)
{
    const bool elf = (actor.characterClass & ClassMask) == ElfClass;
    const bool shared = animation >= FirstSharedAnimation && animation <= LastSharedAnimation;
    return elf && !shared ? static_cast<std::uint8_t>(animation + 1) : animation;
}
} // namespace

Core::Expected<bool, std::string> PlayTheEmote(ActorState& actor, UI::EmoteCase emote, const EmoteContext& context)
{
    if (!IsStanding(actor.animation.action))
        return false;
    // 0x576abf: 0x4ea030 con el modelo del tipo del personaje (+2), no siempre el de Player.bmd.
    const auto* skeleton = context.models.SkeletonOf(actor.type);
    if (skeleton == nullptr)
        return Core::Unexpected(
            std::format("0x576abf: el emote sin las acciones del modelo {:#x} del personaje (0x4ea030)", actor.type));
    Render::Models::SetAction(actor.animation, *skeleton, AnimationFor(actor, emote.animation));
    // 0x576ac7..0x576e97: C1 05 18 con la dirección de [0x743a170] + 0x24 y la acción, XOR32 en línea.
    if (auto sent = SendTheAction(context.hero.angles.z, emote.action, context.send, "0x576b3e"); !sent.HasValue())
        return Core::Unexpected(sent.Error());
    return true;
}

Core::Expected<void, std::string> DoTheEmote(UI::EmoteCase emote, const EmoteContext& context)
{
    if (auto played = PlayTheEmote(context.hero, emote, context); !played.HasValue())
        return Core::Unexpected(played.Error());
    // El C1 05 18 del caso (0x5777aa..0x577b75 en el 0x577796, igual en los otros 21), con el ESI de 0x576ed4.
    return SendTheAction(context.hero.angles.z, emote.action, context.send, "0x577823");
}
} // namespace Client::Scenes
