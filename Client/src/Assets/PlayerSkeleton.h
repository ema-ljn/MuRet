#pragma once

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Models/Skeleton.h"

namespace Client::Assets
{
// MakeSkeleton plus what 0x60dbe0 sets on Player.bmd: each action's speed (Tables::PlayerActionSpeed; the 57
// unwritten ones stay at 0, Q-C87), the four that do not repeat (PlayerActionsWithoutRepeat) and the head bone
// (PlayerHeadBone).
[[nodiscard]] Core::Expected<Models::Skeleton, Models::SkeletonError>
MakePlayerSkeleton(const Data::Models::Model& model);
} // namespace Client::Assets
