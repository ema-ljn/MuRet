#pragma once

#include "Data/Terrain/TerrainGrid.h"
#include "Pathfinding/Path.h"
#include "Pathfinding/PathCell.h"

#include <cstdint>
#include <optional>

namespace Client::Kernel
{
class WorldState;
} // namespace Client::Kernel

namespace Client::Actors
{
// Crywolf's altars, the NPC types 0x4eac00 and the contract (0x435a00..) look for.
inline constexpr std::uint16_t FirstCrywolfAltarType = 0x204;
inline constexpr std::uint16_t LastCrywolfAltarType = 0x208;

// 0x4eac0a..0x4eac41, on entry to 0x4eac00: the stage at 3 or 4 (0x435a00) on map 34 (0x4303c0) and the signed type
// (+2) of the character at 0x6c8e54 in 0x204..0x208 set 0x4ead70's +0x20 flag, so the A* zeroes the goal's WORD and
// the path reaches the altar's cell. Every call reads 0x6c8e54, whoever walks (Q-C1351). With it at −1 the original
// reads record −1, outside the table (Q-C1350): MuRet takes no altar.
[[nodiscard]] bool CrywolfAltarGoal(const Kernel::WorldState& world);

// 0x4eac00, PathFinding2: the A* from `start` to `goal` with Crywolf's altar flag computed on entry, which a `distance`
// other than 0 does not count (0x4eadd4).
[[nodiscard]] std::optional<Pathfinding::Path> FindThePath(const Kernel::WorldState& world,
                                                           Data::Terrain::TerrainLayer<std::uint16_t>& walls,
                                                           Pathfinding::PathCell start, Pathfinding::PathCell goal,
                                                           float distance = 0.0f);
} // namespace Client::Actors
