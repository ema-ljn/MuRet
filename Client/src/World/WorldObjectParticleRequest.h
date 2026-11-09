#pragma once

#include "World/WorldObjectRegistrations.h"

#include <glm/ext/vector_float3.hpp>

#include <cstdint>

namespace Client::World
{
// Argumentos síncronos de 53fe70. Las referencias sólo viven durante CreateParticle.
struct WorldObjectParticleRequest
{
    std::int32_t type;
    glm::vec3& position;
    const glm::vec3& angles;
    glm::vec3& light;
    std::int32_t subtype;
    float scale;
    WorldObjectIdentity owner;
};
} // namespace Client::World
