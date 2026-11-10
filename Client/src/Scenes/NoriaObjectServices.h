#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::World
{
struct WorldObject;
}
namespace Client::Scenes
{
struct GameObjectPass;
// 6032fc: caller ya consumió el rand común de602ef4. La pose global contiene matrices en mundo.
[[nodiscard]] Core::Expected<void, std::string> StepNoriaObjectFrame(World::WorldObject& object, int commonRandom,
                                                                     const GameObjectPass& pass);
} // namespace Client::Scenes
