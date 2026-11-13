#pragma once

#include "GameLogic/Path.h"
#include "Data/Terrain/TerrainGrid.h"
#include "GameLogic/PathCell.h"

#include <cstdint>
#include <optional>

namespace Client::World
{
struct TerrainWalls;
}

// Caminar con distancia 0: PathFinding2 (0x4eac00) y el A* de 0x4ead70; 06 § Caminar.
// No incluye la búsqueda por radio de las acciones de combate.
namespace Client::GameLogic
{
class PathFinder
{
public:
    explicit PathFinder(World::TerrainWalls& walls);
    explicit PathFinder(Data::Terrain::TerrainLayer<std::uint16_t>& walls);

    // Exacto (500 extracciones), luego el más cercano (50). Cero en x/y del inicio falla
    // (Q-C156), igual que el clic en la propia celda. Con x/y del inicio distintos de 0,
    // clearGoal pone el WORD completo de la meta en 0, incluso si luego no se consigue un camino.
    // Con x o y del inicio en 0 sale antes de tocar la meta (0x4eadc1..0x4eadce). clearGoal es la
    // bandera +0x20 de 0x4ead70, que 0x4eac00 calcula en 0x4eac0a..0x4eac41 y pasa a las dos pasadas:
    // un altar de Crywolf elegido con el evento activo (Scenes::CrywolfAltarGoal).
    [[nodiscard]] std::optional<Path> Find(PathCell start, PathCell goal, bool clearGoal = false);

private:
    class SearchState;

    Data::Terrain::TerrainLayer<std::uint16_t>& m_walls;
};
} // namespace Client::GameLogic
