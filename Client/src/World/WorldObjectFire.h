#pragma once

#include "Core/Expected.h"
#include "World/WorldObjectStepError.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::World
{
struct WorldObject;
struct WorldObjectStepContext;

// 52f360: rotación, traslación, tres jitters y las ramas0/1/2; otros valores sólo consumen jitter.
[[nodiscard]] Core::Expected<void, WorldObjectStepError> EmitWorldObjectFire(std::int32_t fireClass,
                                                                             WorldObject& object,
                                                                             const glm::vec3& offset,
                                                                             WorldObjectStepContext& context);
} // namespace Client::World
