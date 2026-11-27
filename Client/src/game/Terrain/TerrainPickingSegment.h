#pragma once

#include <glm/ext/vector_float3.hpp>

namespace Client::Terrain
{
// The segment of the last 3D pass (0x620480), which the caller hands over; the camera is not recomputed here.
struct TerrainPickingSegment
{
    glm::vec3 origin{};
    glm::vec3 end{};
};
} // namespace Client::Terrain
