#include "GameLogic/MovePath.h"

#include "GameLogic/Path.h"
#include "Render/Models/OriginalAngles.h"

#include <glm/ext/vector_float2.hpp>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Client::GameLogic
{
namespace
{
// 0x4ea270: centros y cuatro subpuntos literales, no polinomio continuo.
constexpr double CellScale = 100.0;
constexpr double CellCenter = 0.5;
constexpr double NearDistance = 20.0;
constexpr double SnapAngle = 45.0;
constexpr double TurnFraction = 0.5;
constexpr std::uint8_t LastQuarter = 3;
constexpr std::array<std::array<double, 4>, 3> Weights{{{9.0 / 128.0, 111.0 / 128.0, 29.0 / 128.0, 3.0 / 128.0},
                                                        {1.0 / 16.0, 9.0 / 16.0, 9.0 / 16.0, 1.0 / 16.0},
                                                        {3.0 / 128.0, 29.0 / 128.0, 111.0 / 128.0, 9.0 / 128.0}}};

void Notify(const LogicalCellEffects& effects, LogicalAxis axis, LogicalStoreStage stage, std::uint32_t& cell)
{
    if (effects.onStore)
    {
        effects.onStore(axis, stage, cell);
    }
}

void StoreLogicalCell(MovePathState state, std::size_t index, bool arrival, const LogicalCellEffects& effects)
{
    const auto before = arrival ? LogicalStoreStage::ArrivalBefore : LogicalStoreStage::BeginningBefore;
    const auto after = arrival ? LogicalStoreStage::ArrivalAfter : LogicalStoreStage::BeginningAfter;
    Notify(effects, LogicalAxis::X, before, state.logicalX);
    state.logicalX = state.path.cells[index].x;
    Notify(effects, LogicalAxis::X, after, state.logicalX);
    Notify(effects, LogicalAxis::Y, before, state.logicalY);
    state.logicalY = state.path.cells[index].y;
    Notify(effects, LogicalAxis::Y, after, state.logicalY);
}

glm::vec2 Target(const Path& path)
{
    std::array<glm::vec2, 4> points{};
    const int first = static_cast<int>(path.current) - 1;
    for (int r = 0; r < static_cast<int>(points.size()); ++r)
    {
        const auto index = static_cast<std::size_t>(std::clamp(first + r, 0, static_cast<int>(path.count) - 1));
        points[static_cast<std::size_t>(r)] = {
            static_cast<float>((static_cast<double>(path.cells[index].x) + CellCenter) * CellScale),
            static_cast<float>((static_cast<double>(path.cells[index].y) + CellCenter) * CellScale)};
    }
    if (path.progress == LastQuarter)
    {
        return points[2];
    }
    const auto& w = Weights[path.progress];
    const auto component = [&](int axis)
    {
        const double sum =
            ((static_cast<double>(points[2][axis]) * w[2]) - (static_cast<double>(points[3][axis]) * w[3])) +
            (static_cast<double>(points[1][axis]) * w[1]);
        return static_cast<float>(sum - (static_cast<double>(points[0][axis]) * w[0]));
    };
    return {component(0), component(1)};
}

bool AdvancePath(MovePathState state, const glm::vec2& target, const LogicalCellEffects& effects)
{
    ++state.path.progress;
    if (state.path.progress <= LastQuarter)
    {
        return false;
    }
    ++state.path.current;
    state.path.progress = 0;
    if (state.path.current < state.path.count - 1)
    {
        return false;
    }
    state.path.current = static_cast<std::uint8_t>(state.path.count - 1);
    StoreLogicalCell(state, state.path.current, true, effects);
    state.position.x = target.x;
    state.position.y = target.y;
    return true;
}

void UpdateHeading(MovePathState state, const glm::vec2& target)
{
    using Render::Models::AngleDistance;
    using Render::Models::CreateAngle;
    using Render::Models::TurnAngle;
    const auto heading = static_cast<float>(CreateAngle(state.position.x, state.position.y, target.x, target.y));
    const double distance = AngleDistance(state.yaw, heading, 1);
    if (distance >= SnapAngle)
    {
        state.yaw = heading;
        return;
    }
    const auto step = static_cast<float>(distance * TurnFraction);
    state.yaw = static_cast<float>(TurnAngle(state.yaw, heading, step));
}
} // namespace

bool MovePath(MovePathState state, std::uint8_t rotate, const LogicalCellEffects& effects)
{
    if (state.path.current >= state.path.count)
    {
        return false;
    }
    assert(state.path.count <= MaxPathCells && state.path.progress <= LastQuarter);
    if (state.path.progress == 0)
    {
        const auto next =
            std::min(static_cast<std::size_t>(state.path.current) + 1, static_cast<std::size_t>(state.path.count) - 1);
        StoreLogicalCell(state, next, false, effects);
    }
    const glm::vec2 target = Target(state.path);
    const double dx = static_cast<double>(state.position.x) - static_cast<double>(target.x);
    const double dy = static_cast<double>(state.position.y) - static_cast<double>(target.y);
    const double distance = std::sqrt((dy * dy) + (dx * dx));
    // TEST AH,0x41 incluye unordered; no sustituir por <=.
    if (!(distance > NearDistance) && AdvancePath(state, target, effects))
    {
        return true;
    }
    if (rotate != 0)
    {
        UpdateHeading(state, target);
    }
    return false;
}
} // namespace Client::GameLogic
