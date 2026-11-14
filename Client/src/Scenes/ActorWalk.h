#pragma once

#include "Core/Expected.h"
#include "Scenes/ActorActions.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
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
    // +0xaa del modelo del personaje (0x612c80, Scenes::FaunaModels::SoundActions), los sonidos de sus acciones que lee
    // 0x4f3f0f con otro modelo que el 0x20a. Vacío, ese sonido corta.
    std::span<const std::int16_t> modelSounds{};
};
using ActorWalkResult = ActorStopResult;
// 0x4f37c0: Run, velocidades compartidas, acción y sufijo de sonidos, en ese orden.
[[nodiscard]] Core::Expected<ActorWalkResult, std::string> SetActorWalk(ActorState& actor,
                                                                        const ActorWalkContext& context);
// 0x508668/0x505770: latches por frame y solicitudes de sonido. El caller selecciona al héroe.
[[nodiscard]] std::array<std::optional<std::int32_t>, 2> TakeActorFootstepSounds(ActorState& actor, std::int32_t map,
                                                                                 std::int32_t heroCellLayer);
} // namespace Client::Scenes
