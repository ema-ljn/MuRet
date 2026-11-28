#pragma once

#include "EngineApi.h"
#include "Pathfinding/Path.h"
#include "Data/Terrain/TerrainGrid.h"
#include "Pathfinding/PathCell.h"

#include <cstdint>
#include <optional>

namespace Client::Pathfinding
{
struct TerrainWalls;
}

// PathFinding2 (0x4eac00) and the A* of 0x4ead70; 06 § Caminar. With distance 0 the goal is the cell; otherwise the
// cells of the radius (0x4eae7c..0x4eb054, the elf that walks up to hit, 0x566271; 06 § Pegar al llegar).
namespace Client::Pathfinding
{
class ENGINE_API PathFinder
{
public:
    explicit PathFinder(TerrainWalls& walls);
    explicit PathFinder(Data::Terrain::TerrainLayer<std::uint16_t>& walls);

    // Exact (500 pops), then the closest (50). Zero in the start's x or y fails (Q-C156), like clicking one's own cell.
    // With the start's x and y non-zero, clearGoal zeroes the goal's whole WORD, even if no path is found later; with x
    // or y at 0 it returns before touching the goal (0x4eadc1..0x4eadce). clearGoal is 0x4ead70's +0x20 flag, which
    // 0x4eac00 computes in 0x4eac0a..0x4eac41 and passes to both passes: a Crywolf altar picked with the event active
    // (Events::Crywolf::CrywolfAltarGoal).
    // With a non-zero `distance` (0x4eadd4's comparison with 0x6b86a0's 0), clearGoal does not count and the goal is
    // the cells within `distance` of `goal`: r = ftol(distance); in each row dy from −r to r, those with |dx| ≤ r −
    // |dy| always and those outside if sqrt(dx² + dy²) < distance (FILD, FSQRT and FCOMP with the float); with the
    // exact pass, only those with the TerrainWall WORD below the wall. The index is linear (row · 256 + x), which at
    // the edges wraps to the next row, and outside 0..65535 marks nothing. A distance outside ftol's profile finds no
    // path.
    [[nodiscard]] std::optional<Path> Find(PathCell start, PathCell goal, bool clearGoal = false,
                                           float distance = 0.0f);

private:
    class SearchState;

    Data::Terrain::TerrainLayer<std::uint16_t>& m_walls;
};
} // namespace Client::Pathfinding
