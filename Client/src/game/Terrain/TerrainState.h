#pragma once

#include "Terrain/GroundMarks.h"
#include "Terrain/WorldTerrain.h"

// What the terrain owns of the original's globals.
namespace Client::Terrain
{
struct TerrainState
{
    // What 0x61a560 leaves of the current map's terrain: the mapping, the attributes (the TerrainWall, 0x7d0d348), the
    // heights and the light. Each load replaces it, except the attributes and the raw light (0x78ecc10) when Crywolf
    // reuses the load's paths (Q-C181).
    WorldTerrain current{};
    // The ground marks, 0x760cab0..0x760f990, in the .bss: all off at start. Only 0x548fe0, 0x549090 and 0x549130
    // touch them, and no load clears them: 0x6070a0 turns off the tables next to them, not this one (Q-C228). 0x505de0
    // fills them with model 0x182 (0x507486), and so does 0x52e810 (0x52e96c, Actors::LeaveTheRemains) from the body's
    // step (0x4fd5f0 → 0x4fcd00).
    GroundMarkTable groundMarks{};
};
} // namespace Client::Terrain
