#include "Assets/PlayerSkeleton.h"

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Data/Models/PlayerActions.h"
#include "Data/Models/PlayerModels.h"
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
        skeleton.actions[action].speed = Data::Models::PlayerActionSpeed(action).value_or(0.0f);
    }
    for (const std::uint8_t action : Data::Models::PlayerActionsWithoutRepeat)
    {
        if (action < skeleton.actions.size())
        {
            skeleton.actions[action].repeats = false;
        }
    }
    skeleton.headBone = Data::Models::PlayerHeadBone;
    return skeleton;
}
} // namespace Client::Assets
