#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Scenes
{
struct ActorState;
struct WorldEntryResources;
// 4e9cd0: acción de golpe del Fenrir según las dos manos, sin sonido ni RNG.
[[nodiscard]] std::uint8_t FenrirHitActionOf(const ActorState& actor);
[[nodiscard]] Core::Expected<void, std::string> SetActorShock(ActorState& actor, std::uint16_t damage,
                                                              const WorldEntryResources& resources);
} // namespace Client::Scenes
