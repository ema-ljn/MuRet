#pragma once
#include "World/WorldObjectRegistrations.h"
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
namespace Client::World
{
// 5491e0, último slot0; buffers prestados durante el servicio síncrono.
struct WorldObjectSpriteRequest
{
    std::int32_t type;
    glm::vec3& position;
    float scale;
    glm::vec3& light;
    WorldObjectIdentity owner;
    float rotation;
};
} // namespace Client::World
