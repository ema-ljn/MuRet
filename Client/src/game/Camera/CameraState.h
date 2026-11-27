#pragma once

#include "Terrain/TerrainCulling.h"

// What the camera owns of the original's globals.
namespace Client::Camera
{
struct CameraState
{
    // 0x7b4ccb8, the trapezoid's effective level from one 0x5fb570 to the next, in the three scenes: only 0x5fb570
    // writes it (0x5fb731). In the .bss: 0.
    Terrain::TerrainPolygonState terrainPolygon{};
};
} // namespace Client::Camera
