#include "GameLogic/PathFinder.h"

#include "Data/Terrain/TerrainGrid.h"
#include "GameLogic/Path.h"
#include "GameLogic/PathCell.h"
#include "World/OriginalFtol.h"
#include "World/TerrainWalls.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <optional>
#include <vector>

namespace Client::GameLogic
{
namespace
{
constexpr int ExactLimit = 500;
constexpr int NearestLimit = 50;
constexpr int InfiniteCost = 1950000000;
constexpr int StraightCost = 5;
constexpr int DiagonalCost = 7;
constexpr int NormalWall = 2;
constexpr int SafeWall = 4;
constexpr std::uint16_t IgnoredNeighborBits = 0xe0;
constexpr std::uint16_t ExactGoalBypassBit = 0x20;
constexpr std::uint8_t OpenFlag = 1;
constexpr std::uint8_t ClosedFlag = 2;
constexpr std::uint8_t GoalFlag = 4;
constexpr std::size_t NoNode = std::numeric_limits<std::size_t>::max();

// Los ocho pares de 0x6d5d80. El mismo orden decide los empates de padres y de la lista abierta.
constexpr std::array<std::array<int, 2>, 8> Neighbors{
    {{{-1, -1}}, {{0, -1}}, {{1, -1}}, {{-1, 0}}, {{1, 0}}, {{-1, 1}}, {{0, 1}}, {{1, 1}}}};

std::size_t Index(PathCell cell)
{
    return Data::Terrain::CellIndex(cell.x, cell.y);
}

std::optional<PathCell> Neighbor(PathCell cell, const std::array<int, 2>& offset)
{
    const int x = cell.x + offset[0];
    const int y = cell.y + offset[1];
    constexpr int Side = static_cast<int>(Data::Terrain::TerrainSide);
    if (x < 0 || y < 0 || x >= Side || y >= Side)
    {
        return std::nullopt;
    }
    return PathCell{static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)};
}

int Heuristic(PathCell cell, PathCell goal)
{
    const int dx = std::abs(cell.x - goal.x);
    int dy = std::abs(cell.y - goal.y);
    // 0x4eb2c2 pone dy en cero, no h. Así la diagonal inmediata empata con la recta (Q-C159).
    if (dx == 1 && dy == 1)
    {
        dy = 0;
    }
    constexpr int AxisWeight = 15;
    constexpr int DiagonalWeight = 21;
    constexpr int RoundUp = 3;
    constexpr int Divisor = 4;
    return (std::abs(dx - dy) * AxisWeight + RoundUp + std::min(dx, dy) * DiagonalWeight) / Divisor;
}
} // namespace

// Estado privado de una pasada; sólo flags, costos y padres de esa pasada pueden intervenir.
// Limpiar toda la grilla equivale a limpiar [mínimo..máximo] tocados en 0x4ead7b..0x4eadbe.
class PathFinder::SearchState
{
public:
    SearchState(Data::Terrain::TerrainLayer<std::uint16_t>& walls, PathCell start, PathCell goal)
        : m_walls(walls), m_start(start), m_goal(goal), m_cells(Data::Terrain::TerrainCellCount)
    {
    }

    std::optional<Path> Run(bool exact, int wall, bool clearGoal, float distance)
    {
        if (m_start.x == 0 || m_start.y == 0)
        {
            return std::nullopt;
        }
        // 0x4eadd4..0x4eade8: FCOMP con C3, así que una distancia NaN también va por el radio.
        if (distance == 0.0f)
        {
            std::uint16_t& goalWall = m_walls[Index(m_goal)];
            if (clearGoal)
            {
                goalWall = 0;
            }
            if (exact && goalWall >= wall && (goalWall & ExactGoalBypassBit) == 0)
            {
                return std::nullopt;
            }
            m_cells[Index(m_goal)].flags = GoalFlag;
        }
        else if (!MarkTheRadius(exact, wall, distance))
        {
            return std::nullopt;
        }
        Insert(m_start, 0);
        m_cells[Index(m_start)].flags |= OpenFlag;
        return Explore(exact, wall);
    }

private:
    // 0x4eae7c..0x4eb054: la meta por radio. Cada celda que pasa queda con la bandera de meta (el `mov byte, 4`).
    bool MarkTheRadius(bool exact, int wall, float distance)
    {
        const auto radius = World::OriginalFtolLow(static_cast<double>(distance));
        if (!radius)
        {
            return false;
        }
        const int r = *radius;
        constexpr int Side = static_cast<int>(Data::Terrain::TerrainSide);
        const auto mark = [&](int index)
        {
            if (index < 0 || index >= static_cast<int>(Data::Terrain::TerrainCellCount))
            {
                return;
            }
            const auto cell = static_cast<std::size_t>(index);
            if (!exact || m_walls[cell] < wall)
            {
                m_cells[cell].flags = GoalFlag;
            }
        };
        for (int dy = -r; dy <= r; ++dy)
        {
            const int row = (m_goal.y + dy) * Side;
            const int inner = -(r - std::abs(dy));
            for (int dx = inner; dx <= 0; ++dx)
            {
                mark(row + dx + m_goal.x);
                mark(row - dx + m_goal.x);
            }
            for (int dx = -r; dx < inner; ++dx)
            {
                // 0x4eaf86..0x4eaf93: C0 con el FCOMP, menor o sin orden.
                const double length = std::sqrt(static_cast<double>(dx * dx + dy * dy));
                if (!(length >= static_cast<double>(distance)))
                {
                    mark(row + dx + m_goal.x);
                    mark(row - dx + m_goal.x);
                }
            }
        }
        return true;
    }

    struct CellState
    {
        int cost = 0;
        PathCell parent{};
        std::uint8_t flags = 0;
    };

    struct OpenNode
    {
        PathCell cell;
        int priority;
        std::size_t left = NoNode;
        std::size_t right = NoNode;
    };

    // 0x4eb770: árbol sin balancear, clave menor a la izquierda, todos los iguales a la derecha.
    // Los índices permanecen válidos aunque el vector que posee los nodos se realoque.
    void Insert(PathCell cell, int priority)
    {
        std::size_t parent = NoNode;
        std::size_t node = m_root;
        while (node != NoNode)
        {
            parent = node;
            node = priority < m_open[node].priority ? m_open[node].left : m_open[node].right;
        }
        const std::size_t added = m_open.size();
        m_open.push_back(OpenNode{.cell = cell, .priority = priority});
        if (parent == NoNode)
        {
            m_root = added;
        }
        else if (priority < m_open[parent].priority)
        {
            m_open[parent].left = added;
        }
        else
        {
            m_open[parent].right = added;
        }
    }

    // 0x4eb110..0x4eb124 sólo llama 0x4c0dd0 con el mínimo: su hijo izquierdo siempre es nulo.
    // Su hijo derecho ocupa su sitio; no se necesita la rama genérica de borrado con dos hijos.
    PathCell PopMinimum()
    {
        std::size_t parent = NoNode;
        std::size_t node = m_root;
        while (m_open[node].left != NoNode)
        {
            parent = node;
            node = m_open[node].left;
        }
        if (parent == NoNode)
        {
            m_root = m_open[node].right;
        }
        else
        {
            m_open[parent].left = m_open[node].right;
        }
        return m_open[node].cell;
    }

    // 0x4eb13e..0x4eb1ee recalcula al sacar, no al descubrir. Sólo actualiza con costo menor.
    void Close(PathCell cell, bool first)
    {
        CellState& current = m_cells[Index(cell)];
        current.cost = first ? 0 : InfiniteCost;
        for (const auto& offset : Neighbors)
        {
            const auto neighbor = Neighbor(cell, offset);
            if (!neighbor || (m_cells[Index(*neighbor)].flags & ClosedFlag) == 0)
            {
                continue;
            }
            const int step = offset[0] == 0 || offset[1] == 0 ? StraightCost : DiagonalCost;
            const int cost = m_cells[Index(*neighbor)].cost + step;
            if (cost < current.cost)
            {
                current.cost = cost;
                current.parent = *neighbor;
            }
        }
        current.flags |= ClosedFlag;
    }

    void Expand(PathCell cell, int wall)
    {
        for (const auto& offset : Neighbors)
        {
            const auto neighbor = Neighbor(cell, offset);
            if (!neighbor)
            {
                continue;
            }
            CellState& next = m_cells[Index(*neighbor)];
            const int attribute = m_walls[Index(*neighbor)] & ~IgnoredNeighborBits;
            if ((next.flags & OpenFlag) != 0 || attribute >= wall)
            {
                continue;
            }
            // 0x4eb304..0x4eb311 usa el costo del padre, sin sumarle el paso hacia el vecino.
            Insert(*neighbor, Heuristic(*neighbor, m_goal) + m_cells[Index(cell)].cost);
            next.flags |= OpenFlag;
            next.parent = cell;
        }
    }

    std::optional<Path> Explore(bool exact, int wall)
    {
        const int limit = exact ? ExactLimit : NearestLimit;
        PathCell closest = m_start;
        int closestHeuristic = InfiniteCost;
        for (int iteration = 0; iteration < limit && m_root != NoNode; ++iteration)
        {
            const PathCell cell = PopMinimum();
            Close(cell, iteration == 0);
            if ((m_cells[Index(cell)].flags & GoalFlag) != 0)
            {
                return Reconstruct(cell);
            }
            Expand(cell, wall);
            if (!exact)
            {
                const int heuristic = Heuristic(cell, m_goal);
                if (heuristic < closestHeuristic)
                {
                    closest = cell;
                    closestHeuristic = heuristic;
                }
            }
        }
        return exact ? std::nullopt : Reconstruct(closest);
    }

    std::optional<Path> Reconstruct(PathCell cell) const
    {
        std::array<PathCell, ExactLimit> reversed{};
        for (std::size_t count = 0; count < reversed.size(); ++count)
        {
            reversed[count] = cell;
            if (cell == m_start)
            {
                Path path;
                const std::size_t kept = std::min(count + 1, MaxPathCells);
                path.count = static_cast<std::uint8_t>(kept);
                for (std::size_t i = 0; i < kept; ++i)
                {
                    path.cells[i] = reversed[count - i];
                }
                return path;
            }
            cell = m_cells[Index(cell)].parent;
        }
        return std::nullopt;
    }

    Data::Terrain::TerrainLayer<std::uint16_t>& m_walls;
    PathCell m_start;
    PathCell m_goal;
    std::vector<CellState> m_cells;
    std::vector<OpenNode> m_open;
    std::size_t m_root = NoNode;
};

PathFinder::PathFinder(World::TerrainWalls& walls) : PathFinder(walls.cells) {}

PathFinder::PathFinder(Data::Terrain::TerrainLayer<std::uint16_t>& walls) : m_walls(walls) {}

std::optional<Path> PathFinder::Find(PathCell start, PathCell goal, bool clearGoal, float distance)
{
    auto path = SearchState(m_walls, start, goal).Run(true, NormalWall, clearGoal, distance);
    if (!path)
    {
        const auto startWall = m_walls[Index(start)];
        const auto goalWall = m_walls[Index(goal)];
        const bool safe = ((startWall | goalWall) & World::SafeZoneWallBit) != 0;
        const bool occupiedGoal = (goalWall & World::CharacterWallBit) != 0;
        const int wall = safe && !occupiedGoal ? SafeWall : NormalWall;
        path = SearchState(m_walls, start, goal).Run(false, wall, clearGoal, distance);
    }
    return path && path->count >= 2 ? path : std::nullopt;
}
} // namespace Client::GameLogic
