#include "World/TerrainWalls.h"

#include "Data/Terrain/TerrainGrid.h"

#include <cstdint>
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
