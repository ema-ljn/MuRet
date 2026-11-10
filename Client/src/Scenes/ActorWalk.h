#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorActions.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Client::Scenes
{
struct ActorState;
class PlayerActionSpeeds;
struct ActorWalkContext
{
    ActorStopContext stop;
    PlayerActionSpeeds& actionSpeeds;
    bool hero = false;
    const Render::Models::Skeleton* actorSkeleton = nullptr;
};
using ActorWalkResult = ActorStopResult;
// 0x4f37c0: Run, velocidades compartidas, acción y sufijo de sonidos, en ese orden.
[[nodiscard]] Core::Expected<ActorWalkResult, std::string> SetActorWalk(ActorState& actor,
                                                                        const ActorWalkContext& context);
// 0x508668/0x505770: latches por frame y solicitudes de sonido. El caller selecciona al héroe.
[[nodiscard]] std::array<std::optional<std::int32_t>, 2> TakeActorFootstepSounds(ActorState& actor, std::int32_t map,
                                                                                 std::int32_t heroCellLayer);
} // namespace Client::Scenes
