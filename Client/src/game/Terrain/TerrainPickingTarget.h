#pragma once

#include <cstdint>

namespace Client::Terrain
{
// The DWORD stores TargetX/Y and the cursor output of MoveHero's 0x59ddf3..0x59deb3 branch. DontMove does not prevent
// computing the path. A failed retry keeps the three previous fields.
struct TerrainPickingTarget
{
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    bool dontMove = false;
};
} // namespace Client::Terrain
