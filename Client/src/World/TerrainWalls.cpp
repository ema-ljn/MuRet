#include "World/TerrainWalls.h"

#include "Data/Terrain/TerrainGrid.h"

#include <cstdint>
#include <span>

namespace Client::World
{
void TerrainWalls::Rebuild(std::span<const TerrainOccupant> occupants)
{
    for (std::uint16_t& cell : cells)
    {
        cell = static_cast<std::uint16_t>(cell & ~CharacterWallBit);
    }
    constexpr std::uint8_t NonBlockingKind = 8;
    for (const TerrainOccupant& occupant : occupants)
    {
        if (!occupant.live || occupant.dying || occupant.kind == NonBlockingKind)
        {
            continue;
        }
        // 0x5f82a0 conserva sólo los ocho bits bajos de cada coordenada lógica.
        const auto x = static_cast<std::uint8_t>(occupant.logicalX);
        const auto y = static_cast<std::uint8_t>(occupant.logicalY);
        cells[Data::Terrain::CellIndex(x, y)] |= CharacterWallBit;
    }
}
} // namespace Client::World
