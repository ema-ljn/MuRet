#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Kernel
{
struct ActorState;
} // namespace Client::Kernel

namespace Client::Scenes
{
struct WorldEntryResources;
} // namespace Client::Scenes

namespace Client::Actors
{
// 4e9cd0: acción de golpe del Fenrir según las dos manos, sin sonido ni RNG.
[[nodiscard]] std::uint8_t FenrirHitActionOf(const Kernel::ActorState& actor);
[[nodiscard]] Core::Expected<void, std::string> SetActorShock(Kernel::ActorState& actor, std::uint16_t damage,
                                                              const Scenes::WorldEntryResources& resources);
} // namespace Client::Actors
