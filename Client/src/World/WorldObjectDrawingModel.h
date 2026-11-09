#pragma once
#include "World/WorldObjectStepModel.h"
#include <glm/ext/vector_float3.hpp>
#include <cstdint>
#include <span>
namespace Client::World
{
// Vista prestada de metadata del MISMO modelo de Step/pose, sin copias ni pool paralelo.
// El owner mantiene almacenamiento/bindings estables durante servicios.
struct WorldObjectDrawingModel
{
    WorldObjectStepModel& step;
    glm::vec3& light;                            // +48
    glm::vec3& shadowOrigin;                     // +6c/+70/+74
    const std::int16_t& meshCount;               // +24
    std::span<const std::int16_t> meshMaterials; // +38 -> entry+c
    const std::int16_t& frame;                   // +a8
    std::span<const std::int16_t> actionKeys;    // +30 -> action*10+8
};
} // namespace Client::World
