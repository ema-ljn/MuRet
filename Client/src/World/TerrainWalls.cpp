#include "World/TerrainWalls.h"

#include "Data/Terrain/TerrainGrid.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace Client::World
{
void ClearCharacterWalls(Data::Terrain::TerrainLayer<std::uint16_t>& cells)
{
    for (std::uint16_t& cell : cells)
    {
        cell = static_cast<std::uint16_t>(cell & ~CharacterWallBit);
    }
}

void MarkCharacterWall(Data::Terrain::TerrainLayer<std::uint16_t>& cells, int logicalX, int logicalY)
{
    // 0x5f82a0 conserva sólo los ocho bits bajos de cada coordenada lógica.
    const auto x = static_cast<std::uint8_t>(logicalX);
    const auto y = static_cast<std::uint8_t>(logicalY);
    cells[Data::Terrain::CellIndex(x, y)] |= CharacterWallBit;
}

void OpenTheGates(Data::Terrain::TerrainLayer<std::uint16_t>& cells)
{
    // 0x42a136: el byte bajo del WORD de 0x7d0d548, una fila después de 0x7d0d348.
    constexpr int GateWidth = 4;
    for (const GateCell& gate : GateCells)
    {
        const auto below = static_cast<std::uint8_t>(gate.y + 1);
        if ((cells[Data::Terrain::CellIndex(gate.x, below)] & BlockedWallBit) == 0)
        {
            continue;
        }
        for (int dx = -1; dx < GateWidth - 1; ++dx)
        {
            std::uint16_t& cell = cells[Data::Terrain::CellIndex(static_cast<std::uint8_t>(gate.x + dx), below)];
            cell = static_cast<std::uint16_t>(cell & ~BlockedWallBit);
        }
    }
}

std::optional<bool> CanSeeTheCell(const Data::Terrain::TerrainLayer<std::uint16_t>& cells, int x, int y, int targetX,
                                  int targetY)
{
    // 0x55880e..0x558866.
    constexpr int Row = static_cast<int>(Data::Terrain::TerrainSide);
    int dx = targetX - x;
    int dy = targetY - y;
    int stepX = 1;
    int stepY = Row;
    if (dx < 0)
    {
        dx = -dx;
        stepX = -1;
    }
    if (dy < 0)
    {
        dy = -dy;
        stepY = -Row;
    }
    int major = dy;
    int minor = dx;
    int majorStep = stepY;
    int minorStep = stepX;
    if (dx > dy)
    {
        major = dx;
        minor = dy;
        majorStep = stepX;
        minorStep = stepY;
    }
    // 0x55886d..0x5588b1: con el byte bajo de cada WORD.
    constexpr std::uint16_t LowestWall = 4;
    constexpr std::uint8_t SeeThroughBits = 0x20 | 0x40 | 0x80;
    int index = static_cast<int>(Data::Terrain::CellIndex(static_cast<std::uint8_t>(x), static_cast<std::uint8_t>(y)));
    int error = 0;
    for (int step = 0; step <= major; ++step)
    {
        if (index < 0 || index >= static_cast<int>(Data::Terrain::TerrainCellCount))
        {
            return std::nullopt;
        }
        const std::uint16_t wall = cells[static_cast<std::size_t>(index)];
        if (wall >= LowestWall && (static_cast<std::uint8_t>(wall) & SeeThroughBits) == 0)
        {
            return false;
        }
        error += minor;
        if (error > major / 2)
        {
            index += minorStep;
            error -= major;
        }
        index += majorStep;
    }
    return true;
}

void TerrainWalls::Rebuild(std::span<const TerrainOccupant> occupants)
{
    ClearCharacterWalls(cells);
    for (const TerrainOccupant& occupant : occupants)
    {
        if (!occupant.live || occupant.dying || occupant.kind == NonBlockingKind)
        {
            continue;
        }
        MarkCharacterWall(cells, occupant.logicalX, occupant.logicalY);
    }
}
} // namespace Client::World
