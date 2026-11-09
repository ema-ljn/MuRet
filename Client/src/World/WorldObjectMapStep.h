#pragma once

#include "Core/Expected.h"
#include "World/WorldObjectStepError.h"

namespace Client::World
{
struct WorldObject;
struct WorldObjectStepContext;

// Fases propias de604ab0; no filtran seen ni reinicializan estado persistente.
[[nodiscard]] Core::Expected<void, WorldObjectStepError> PrepareWorldObjectStep(WorldObject& object,
                                                                                WorldObjectStepContext& context);
void StepWorldObjectSceneCases(WorldObject& object, WorldObjectStepContext& context);
[[nodiscard]] Core::Expected<void, WorldObjectStepError> StepWorldObjectMapCases(WorldObject& object,
                                                                                 WorldObjectStepContext& context);
} // namespace Client::World
