#pragma once

#include <glm/ext/vector_float3.hpp>

namespace Client::World
{
// Segmento del último pase 3D (0x620480), que entrega el caller; no se recalcula la cámara aquí.
struct TerrainPickingSegment
{
    glm::vec3 origin{};
    glm::vec3 end{};
};
} // namespace Client::World
