#pragma once

#include "Core/Expected.h"

#include <cstdint>
#include <string>

namespace Client::Scenes
{
struct ActorState;
struct GameData;
struct SceneContext;
class IModelLibrary;
class WorldState;
// Caller real: Walk antes deMovePath, accesosP por store y posición sólo si no llegó.
[[nodiscard]] Core::Expected<bool, std::string> MoveActorAlongPath(ActorState& actor, WorldState& world,
                                                                   const SceneContext& context, const GameData& data,
                                                                   const IModelLibrary& models, std::uint8_t rotate);
} // namespace Client::Scenes
