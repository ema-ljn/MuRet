#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::WorldObjects
{
struct WorldObject;
}
namespace Client::WorldObjects
{
struct GameObjectPass;
} // namespace Client::WorldObjects

namespace Client::MapScripts::Noria
{
// 6032fc: caller ya consumió el rand común de602ef4. La pose global contiene matrices en mundo.
[[nodiscard]] Core::Expected<void, std::string>
StepNoriaObjectFrame(WorldObjects::WorldObject& object, int commonRandom, const WorldObjects::GameObjectPass& pass);
} // namespace Client::MapScripts::Noria
