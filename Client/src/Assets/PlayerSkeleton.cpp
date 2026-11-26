#include "Assets/PlayerSkeleton.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Tables/PlayerActions.h"
#include "Tables/PlayerModels.h"
#include "Models/Skeleton.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace Client::Assets
{
Core::Expected<Models::Skeleton, Models::SkeletonError> MakePlayerSkeleton(const Data::Models::Model& model)
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
    return skeleton;
}
} // namespace Client::Assets
