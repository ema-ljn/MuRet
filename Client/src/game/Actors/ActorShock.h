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
// 0x4e9cd0: Fenrir's hit action by both hands, without sound or RNG.
[[nodiscard]] std::uint8_t FenrirHitActionOf(const Kernel::ActorState& actor);
[[nodiscard]] Core::Expected<void, std::string> SetActorShock(Kernel::ActorState& actor, std::uint16_t damage,
                                                              const Scenes::WorldEntryResources& resources);
} // namespace Client::Actors
