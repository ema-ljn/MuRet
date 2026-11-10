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
