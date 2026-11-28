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
// 0x6032fc: the caller already consumed the common rand() of 0x602ef4. The global pose holds world matrices.
[[nodiscard]] Core::Expected<void, std::string>
StepNoriaObjectFrame(WorldObjects::WorldObject& object, int commonRandom, const WorldObjects::GameObjectPass& pass);
} // namespace Client::MapScripts::Noria
