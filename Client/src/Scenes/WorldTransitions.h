#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Protocol::World
{
struct Teleport;
struct Regen;
} // namespace Client::Protocol::World
namespace Client::Scenes
{
struct WorldEntryResources;
class IWorldTransitionPublisher;
// 06, «Los cambios de mapa»: carga síncrona dentro del handler, sin cambiar la escena.
[[nodiscard]] Core::Expected<void, std::string> ApplyTeleport(const Protocol::World::Teleport& reply,
                                                              std::uint16_t playerKey,
                                                              const WorldEntryResources& resources,
                                                              IWorldTransitionPublisher& publisher);
[[nodiscard]] Core::Expected<void, std::string> ApplyRegen(const Protocol::World::Regen& reply, std::uint16_t playerKey,
                                                           const WorldEntryResources& resources);
} // namespace Client::Scenes
