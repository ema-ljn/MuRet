#pragma once

#include "Core/Expected.h"
#include "World/WorldObjectStepError.h"

namespace Client::World
{
struct WorldObject;
struct WorldObjectStepContext;
// 52f550: snapshots Hero XY/action, impulso condicional, servicio síncrono y amortiguación común.
[[nodiscard]] Core::Expected<void, WorldObjectStepError> StepWorldObjectSecondary(WorldObject& object,
                                                                                  WorldObjectStepContext& context);
} // namespace Client::World
