#pragma once

#include "Core/Expected.h"

#include <string>

namespace Client::Kernel
{
struct GameObjectPass;
struct WorldObject;
} // namespace Client::Kernel

namespace Client::MapScripts::Noria
{
// 0x6032fc: the caller already consumed the common rand() of 0x602ef4. The global pose holds world matrices.
[[nodiscard]] Core::Expected<void, std::string> StepNoriaObjectFrame(Kernel::WorldObject& object, int commonRandom,
                                                                     const Kernel::GameObjectPass& pass);
} // namespace Client::MapScripts::Noria
