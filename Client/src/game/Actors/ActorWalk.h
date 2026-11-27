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
    // +0xaa del modelo del personaje (0x612c80, Assets::FaunaModels::SoundActions), los sonidos de sus acciones que lee
    // 0x4f3f0f con otro modelo que el 0x20a. Vacío, los de WorldRuntime::modelSounds (MonsterSounds.h).
    std::span<const std::int16_t> modelSounds{};
};
using ActorWalkResult = ActorStopResult;
// 0x4f37c0: Run, velocidades compartidas, acción y sufijo de sonidos, en ese orden.
[[nodiscard]] Core::Expected<ActorWalkResult, std::string> SetActorWalk(Kernel::ActorState& actor,
                                                                        const ActorWalkContext& context);
// 0x508668/0x505770: latches por frame y solicitudes de sonido. El caller selecciona al héroe.
[[nodiscard]] std::array<std::optional<std::int32_t>, 2>
TakeActorFootstepSounds(Kernel::ActorState& actor, std::int32_t map, std::int32_t heroCellLayer);
} // namespace Client::Actors
