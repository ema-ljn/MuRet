#pragma once

#include <glm/ext/vector_float3.hpp>

#include <cstdint>
#include <functional>

namespace Client::Pathfinding
{
struct Path;
} // namespace Client::Pathfinding
namespace Client::Actors
{
struct MovePathState
{
    Pathfinding::Path& path;
    glm::vec3& position;
    float& yaw;
    std::uint32_t& logicalX;
    std::uint32_t& logicalY;
};
enum class LogicalAxis
{
    X,
    Y
};
enum class LogicalStoreStage
{
    BeginningBefore,
    BeginningAfter,
    ArrivalBefore,
    ArrivalAfter
};
struct LogicalCellEffects
{
    // The boundary keeps pre/store/post of each axis, even if the value does not change. The service that owns P keeps
    // its counters/aliases; Y's BeginningAfter differs from ArrivalAfter. Beginning keeps k; Arrival rereads
    // path.current after each axis's pre-callback. Empty is a no-op. During a call do not reassign the callback that is
    // running.
    std::function<void(LogicalAxis, LogicalStoreStage, std::uint32_t&)> onStore;
};
// 0x4ea270. Disjoint, live referents; actor/buffers not relocated during the call. Valid path: count<=MaxPathCells
// and progress<=3; the callbacks may alter their live values within that domain. Returns the arrival; does not move XY
// except on arrival.
[[nodiscard]] bool MovePath(MovePathState state, std::uint8_t rotate, const LogicalCellEffects& effects = {});
} // namespace Client::Actors
