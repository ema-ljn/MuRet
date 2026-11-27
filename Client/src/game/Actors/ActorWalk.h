#pragma once

#include "Core/Expected.h"
#include "Actors/ActorActions.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Actors
{
class PlayerActionSpeeds;
struct ActorWalkContext
{
    ActorStopContext stop;
    PlayerActionSpeeds& actionSpeeds;
    bool hero = false;
    const Models::Skeleton* actorSkeleton = nullptr;
    // +0xaa of the character's model (0x612c80, Assets::FaunaModels::SoundActions), the sounds of its actions that
    // 0x4f3f0f reads with a model other than 0x20a. Empty, those of WorldRuntime::modelSounds (MonsterSounds.h).
    std::span<const std::int16_t> modelSounds{};
};
using ActorWalkResult = ActorStopResult;
// 0x4f37c0: Run, shared speeds, action and sound suffix, in that order.
[[nodiscard]] Core::Expected<ActorWalkResult, std::string> SetActorWalk(Kernel::ActorState& actor,
                                                                        const ActorWalkContext& context);
// 0x508668/0x505770: per-frame latches and sound requests. The caller selects the hero.
[[nodiscard]] std::array<std::optional<std::int32_t>, 2>
TakeActorFootstepSounds(Kernel::ActorState& actor, std::int32_t map, std::int32_t heroCellLayer);
} // namespace Client::Actors
