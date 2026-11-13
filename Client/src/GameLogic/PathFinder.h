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

// PathFinding2 (0x4eac00) y el A* de 0x4ead70; 06 § Caminar. Con distancia 0, la meta es la celda; con otra, las
// celdas del radio (0x4eae7c..0x4eb054, el elfo que se acerca a pegar, 0x566271; 06 § Pegar al llegar).
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
    // Con x o y del inicio en 0 sale antes de tocar la meta (0x4eadc1..0x4eadce).
    // Con `distance` distinta de 0 (la comparación de 0x4eadd4 con el 0 de 0x6b86a0), clearGoal no cuenta y la meta son
    // las celdas a menos de `distance` de `goal`: r = ftol(distance); en cada fila dy de −r a r, las de |dx| ≤ r − |dy|
    // siempre y las de afuera si sqrt(dx² + dy²) < distance (FILD, FSQRT y FCOMP con el float); con el paso exacto,
    // solo las que tienen el WORD del TerrainWall debajo del muro. El índice es el lineal (fila · 256 + x), que en los
    // bordes pasa a la fila de al lado, y fuera de 0..65535 no marca nada. Una distancia fuera del perfil de ftol no
    // encuentra camino.
    [[nodiscard]] std::optional<Path> Find(PathCell start, PathCell goal, bool clearGoal = false,
                                           float distance = 0.0f);

private:
    class SearchState;

    Data::Terrain::TerrainLayer<std::uint16_t>& m_walls;
};
} // namespace Client::GameLogic
