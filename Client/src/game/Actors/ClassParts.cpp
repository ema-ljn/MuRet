#include "Actors/ClassParts.h"

#include "Core/Expected.h"
#include "Tables/PlayerModels.h"
#include "Kernel/CharacterAppearance.h"
#include "Actors/ActorActions.h"
#include "Actors/ActorEquipment.h"
#include "Kernel/ActorState.h"

#include <cstddef>
#include <cstdint>
#include <string>

// 0x513100, cotejado con el PE c32bbd5f (89 instrucciones, 353 bytes).
namespace Client::Scenes
{
namespace
{
// 0x51311c..0x51312e: 0x4f2250 sólo con la acción fuera de las dos ventanas.
constexpr std::uint8_t FirstBusyAction = 0x22;
constexpr std::uint8_t LastBusyAction = 0x97;
constexpr std::uint8_t FirstEmoteAction = 0xc5;
constexpr std::uint8_t LastEmoteAction = 0xcc;

// 0x513146..0x51324d: la parte que se compara con el modelo base de la de cada vuelta. La del casco es la suya
// (+0x218); las otras, la siguiente: +0x258 con la armadura, +0x278 con los pantalones, +0x298 con los guantes y la
// mano derecha (+0x2b8) con las botas (Q-C1024).
const ActorItemPart& ComparedPart(const ActorState& actor, std::size_t part)
{
    if (part == 0)
        return actor.body[0];
    if (part + 1 < actor.body.size())
        return actor.body[part + 1];
    return actor.hands[0];
}
} // namespace

Core::Expected<ActorStopResult, std::string> ApplyTheClassParts(ActorState& actor, const ActorStopContext& stop)
{
    // 0x513107: CMP word [esi+2], 0x20a.
    if (actor.type != Tables::PlayerSkeletonModel)
        return ActorStopResult{};
    ActorStopResult result;
    const std::uint8_t action = actor.animation.action;
    const bool emote = action >= FirstEmoteAction && action <= LastEmoteAction;
    const bool busy = action >= FirstBusyAction && action <= LastBusyAction;
    if (!busy && !emote)
    {
        // 0x513135.
        auto stopped = SetActorStop(actor, stop);
        if (!stopped.HasValue())
            return Core::Unexpected(stopped.Error());
        result = stopped.Value();
    }
    // 0x513140..0x51324d: la clase entera (+0x1d4) se lee una vez, después de 0x4f2250. Cada vuelta compara el word con
    // signo (MOVSX) con base + (clase & 7) y escribe base + (clase >> 3) · 5 + (clase & 7), con el nivel (+2) y los
    // bytes +3 y +4 en 0. Las comparaciones leen la parte siguiente antes de que la vuelta siguiente la escriba.
    const std::uint8_t characterClass = actor.characterClass;
    const auto baseClass = static_cast<std::uint8_t>(characterClass & 7u);
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        const auto bodyPart = static_cast<GameLogic::BodyPart>(part);
        const auto compared = static_cast<std::int16_t>(ComparedPart(actor, part).model);
        if (compared != static_cast<std::int32_t>(GameLogic::ClassPartModel(bodyPart, baseClass)))
            continue;
        ActorItemPart& worn = actor.body[part];
        worn.level = 0;
        worn.flags1b = 0;
        worn.flags1c = 0;
        worn.model = GameLogic::ClassPartModel(bodyPart, characterClass);
    }
    // 0x513255.
    ApplyHeadAndScale(actor);
    return result;
}
} // namespace Client::Scenes
