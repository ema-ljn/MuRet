#include "Assets/PlayerSkeleton.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerActions.h"
#include "Tables/PlayerBones.h"
#include "Models/Skeleton.h"
#include "Tables/AddedEmotes.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace Client::Assets
{
Core::Expected<Models::Skeleton, Models::SkeletonError>
MakePlayerSkeleton(const Data::Models::Model& model, std::span<const Tables::AddedEmote> addedEmotes)
{
    Core::Expected<Models::Skeleton, Models::SkeletonError> made = Models::MakeSkeleton(model);
    if (!made.HasValue())
    {
        return made;
    }
    Models::Skeleton skeleton = std::move(made).Value();
    for (std::size_t action = 0; action < skeleton.actions.size(); ++action)
    {
        skeleton.actions[action].speed = Tables::PlayerActionSpeed(action).value_or(0.0f);
    }
    for (const std::uint8_t action : Tables::PlayerActionsWithoutRepeat)
    {
        if (action < skeleton.actions.size())
        {
            skeleton.actions[action].repeats = false;
        }
    }
    skeleton.headBone = Tables::PlayerHeadBone;
    const auto playAsTheEmote = [&skeleton](std::uint8_t action, const Tables::AddedEmote& emote)
    {
        if (action >= skeleton.actions.size())
            return;
        skeleton.actions[action].speed = emote.keysPerStep;
        skeleton.actions[action].repeats = emote.repeats;
    };
    for (const Tables::AddedEmote& emote : addedEmotes)
    {
        if (emote.elfAction >= Tables::FirstAddedEmoteAction)
            playAsTheEmote(emote.elfAction, emote);
    }
    for (const Tables::AddedEmote& emote : addedEmotes)
        playAsTheEmote(emote.action, emote);
    return skeleton;
}
} // namespace Client::Assets
