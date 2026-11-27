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

// 0x513100, checked against PE c32bbd5f (89 instructions, 353 bytes).
namespace Client::Actors
{
namespace
{
// 0x51311c..0x51312e: 0x4f2250 only with the action outside both windows.
constexpr std::uint8_t FirstBusyAction = 0x22;
constexpr std::uint8_t LastBusyAction = 0x97;
constexpr std::uint8_t FirstEmoteAction = 0xc5;
constexpr std::uint8_t LastEmoteAction = 0xcc;

// 0x513146..0x51324d: the part compared with the base model of each round's part. The helmet's is its own (+0x218);
// the others, the next one: +0x258 with the armor, +0x278 with the pants, +0x298 with the gloves and the right hand
// (+0x2b8) with the boots (Q-C1024).
const Kernel::ActorItemPart& ComparedPart(const Kernel::ActorState& actor, std::size_t part)
{
    if (part == 0)
        return actor.body[0];
    if (part + 1 < actor.body.size())
        return actor.body[part + 1];
    return actor.hands[0];
}
} // namespace

Core::Expected<ActorStopResult, std::string> ApplyTheClassParts(Kernel::ActorState& actor, const ActorStopContext& stop)
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
    // 0x513140..0x51324d: the whole class (+0x1d4) is read once, after 0x4f2250. Each round compares the word, signed
    // (MOVSX), with base + (class & 7) and writes base + (class >> 3) · 5 + (class & 7), with the level (+2) and
    // bytes +3 and +4 at 0. The comparisons read the next part before the next round writes it.
    const std::uint8_t characterClass = actor.characterClass;
    const auto baseClass = static_cast<std::uint8_t>(characterClass & 7u);
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        const auto bodyPart = static_cast<Kernel::BodyPart>(part);
        const auto compared = static_cast<std::int16_t>(ComparedPart(actor, part).model);
        if (compared != static_cast<std::int32_t>(Kernel::ClassPartModel(bodyPart, baseClass)))
            continue;
        Kernel::ActorItemPart& worn = actor.body[part];
        worn.level = 0;
        worn.flags1b = 0;
        worn.flags1c = 0;
        worn.model = Kernel::ClassPartModel(bodyPart, characterClass);
    }
    // 0x513255.
    ApplyHeadAndScale(actor);
    return result;
}
} // namespace Client::Actors
