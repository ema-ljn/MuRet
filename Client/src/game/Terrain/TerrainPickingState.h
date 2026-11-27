#pragma once

#include <glm/ext/vector_float3.hpp>

namespace Client::Terrain
{
// The persistent state of 0x5fc260/0x5fac90/0x622440. The defaults are MuRet's DTO: these bodies do not prove the
// native state before the first picking.
struct TerrainPickingState
{
    bool selected = false;
    float selectedX = 0.0f;
    float selectedY = 0.0f;
    // A parameter of the segment, not a distance in world units.
    float maxT = 0.0f;
    glm::vec3 hit{};
};
} // namespace Client::Terrain
