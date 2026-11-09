#pragma once

#include "World/WorldObjectRegistrations.h"
#include <glm/ext/vector_float3.hpp>
#include <cstdint>

namespace Client::World
{
// 518cf0: once palabras, key=-1 y cuatro ceros finales; buffers prestados sólo durante el servicio.
struct WorldObjectRuntimeEffectRequest
{
    std::int32_t type;
    glm::vec3& position;
    const glm::vec3& angles;
    glm::vec3& light;
    std::int32_t subtype;
    WorldObjectIdentity owner;
};
} // namespace Client::World
