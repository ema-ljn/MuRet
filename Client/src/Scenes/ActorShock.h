#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Scenes
{
struct ActorState;
struct WorldEntryResources;
[[nodiscard]] Core::Expected<void, std::string> SetActorShock(ActorState& actor, std::uint16_t damage,
                                                              const WorldEntryResources& resources);
} // namespace Client::Scenes
