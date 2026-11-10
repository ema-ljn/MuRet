#include "Scenes/ActorAppearance.h"

#include "GameLogic/CharacterAppearance.h"
#include "Scenes/ActorState.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Scenes
{
GameLogic::CharacterAppearance AppearanceOfActor(const ActorState& actor)
{
    GameLogic::CharacterAppearance appearance;
    appearance.characterClass = actor.characterClass;
    appearance.skin = actor.skin;
    appearance.scale = actor.scale;
    const auto worn = [](const ActorItemPart& part)
    { return GameLogic::WornItem{part.model, part.level, part.flags1b != 0, part.flags1c != 0}; };
    for (std::size_t part = 0; part < actor.body.size(); ++part)
    {
        appearance.bodyParts[part] = worn(actor.body[part]);
    }
    if (actor.hands[0].model != NoItemModel)
    {
        appearance.rightHand = worn(actor.hands[0]);
    }
    if (actor.hands[1].model != NoItemModel)
    {
        appearance.leftHand = worn(actor.hands[1]);
    }
    const auto item = [](std::uint16_t model)
    { return model == NoItemModel ? std::nullopt : std::optional<std::uint16_t>{model}; };
    appearance.wings = item(actor.wings.model);
    appearance.pet = item(actor.pet.model);
    appearance.head = item(actor.head);
    return appearance;
}
} // namespace Client::Scenes
