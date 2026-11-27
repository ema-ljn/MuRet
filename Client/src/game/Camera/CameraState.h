#pragma once

#include "Terrain/TerrainCulling.h"

// What the camera owns of the original's globals.
namespace Client::Camera
{
struct CameraState
{
    // 0x7b4ccb8, the trapezoid's effective level from one 0x5fb570 to the next, in the three scenes: only 0x5fb570
    // writes it (0x5fb731). In the .bss: 0.
    Terrain::TerrainPolygonState trapezoidLevel{};
    // The trapezoid of the last 0x5fb570, in cells: the Ys at 0x78ecbf0 and the Xs at 0x78ecc00, which only 0x5fba55
    // and 0x5fba68 write. Only 0x5fbb00 calls 0x5fb570 (0x5fbeef), and the drawings of the login (0x6351b7), the
    // selection (0x63475c) and the game (0x637366) call 0x5fbb00: one for the three scenes. Only 0x5fba90 reads it,
    // with the scene of the moment (0x6d5dc8): the game's first step after the wait (0x508f00) reads the selection's
    // last drawing's, and the login's first step, whatever is there. In the .bss: zeros, a trapezoid with no area at
    // (0, 0) for which 0x5fba90 gives 0 on the four sides (06 § El paso de los personajes and Q-C217).
    Terrain::TerrainPolygon trapezoid{};
};
} // namespace Client::Camera
