#include "Actors/WalkPath.h"

#include "Data/Terrain/TerrainGrid.h"
#include "Kernel/WorldRuntime.h"
#include "Kernel/WorldState.h"
#include "Maps/MapId.h"
#include "Pathfinding/Path.h"
#include "Pathfinding/PathCell.h"
#include "Pathfinding/PathFinder.h"
#include "Protocol/Events/Events.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace Client::Actors
{
bool CrywolfAltarGoal(const Kernel::WorldState& world)
{
    // 0x4eac0a..0x4eac1a.
    const Kernel::WorldRuntime& runtime = world.Runtime();
    if (!Protocol::Events::CrywolfIsReadyOrStarted(runtime.crywolf.stage) || runtime.map != Maps::CrywolfMap)
        return false;
    // 0x4eac1c..0x4eac3f: [0x743a16c] + 0x438 · [0x6c8e54] + 2, with a signed 16-bit CMP.
    const std::int32_t chosen = runtime.chosenNpc.index;
    const auto actors = world.Actors();
    if (chosen < 0 || static_cast<std::size_t>(chosen) >= actors.size())
        return false;
    const auto type = static_cast<std::int16_t>(actors[static_cast<std::size_t>(chosen)].type);
    return type >= static_cast<std::int16_t>(FirstCrywolfAltarType) &&
           type <= static_cast<std::int16_t>(LastCrywolfAltarType);
}

std::optional<Pathfinding::Path> FindThePath(const Kernel::WorldState& world,
                                             Data::Terrain::TerrainLayer<std::uint16_t>& walls,
                                             Pathfinding::PathCell start, Pathfinding::PathCell goal, float distance)
{
    return Pathfinding::PathFinder(walls).Find(start, goal, CrywolfAltarGoal(world), distance);
}
} // namespace Client::Actors
