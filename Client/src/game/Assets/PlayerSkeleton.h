#pragma once

#include "Core/Expected.h"
#include "Data/Models/Model.h"
#include "Models/Skeleton.h"
#include "Tables/AddedEmotes.h"

#include <span>

namespace Client::Assets
{
// MakeSkeleton plus what 0x60dbe0 sets on Player.bmd: each action's speed (Tables::PlayerActionSpeed; the 57
// unwritten ones stay at 0, Q-C87), the four that do not repeat (PlayerActionsWithoutRepeat) and the head bone
// (PlayerHeadBone). Then MuRet's added emotes set the speed and the repeat of the actions the model has: an added elf
// action takes its row's, and a row's own action its own.
[[nodiscard]] Core::Expected<Models::Skeleton, Models::SkeletonError>
MakePlayerSkeleton(const Data::Models::Model& model, std::span<const Tables::AddedEmote> addedEmotes = {});
} // namespace Client::Assets
