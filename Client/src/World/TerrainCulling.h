#pragma once

#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "World/TerrainCells.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float3.hpp>

#include <array>
#include <cstdint>

// Qué celdas del terreno pasan el recorte del original (CL7.1, tarea 3): el rango de bloques de
// 0x5fbb00 (los cinco puntos del frustum, la cámara y las cuatro esquinas lejanas) y el trapecio de
// 0x5fb570 alrededor del héroe, que 0x5fba90 prueba en el centro de cada bloque (con un margen de −40,
// DAT_006c9d60) y de cada celda. Verificado: en Lorencia y en la selección da las celdas de sus trazas,
// en su orden (06 § Terreno). Todo en double donde la x87 no redondea y en float donde guarda un float,
// con las constantes por sus bits.
namespace Client::World
{
// La escena de DAT_006d5dc8, que elige el trapecio: el login (2), la selección (4) y el juego (5).
// En las otras, 0x5fba90 deja pasar todo.
enum class CullingScene : std::uint8_t
{
    Login = 2,
    CharacterSelect = 4,
    Game = 5,
    Other = 0,
};

// 0x5fbb00 con la cámara de `pass` (su modelview, la de 0x620db0, que 0x620370 lee de GL) y
// `camera`: el far sin el × 1,4 (DAT_006d5d44) y el fov (DAT_006d5d48). `widthFactor` es el primer
// argumento, el ancho del mundo de 0x5bb8d0 (640 sin ventanas) por 0,0015625.
[[nodiscard]] TerrainRange TerrainRangeOf(const glm::mat4& view, const Render::Camera3D& camera,
                                          float widthFactor = 1.0f);

// El trapecio de 0x5fb570 en celdas, con el nivel de cámara 0 (DAT_07b4ccb8): las cuatro esquinas, de
// la izquierda lejana a la izquierda cercana.
struct TerrainPolygon
{
    std::array<float, 4> x{};
    std::array<float, 4> y{};
    // En las escenas que no son 2, 4 ni 5, 0x5fba90 deja pasar todo.
    bool everything = false;
};

// 0x5fb570 alrededor de `hero` (DAT_0743a170: en el login, el (11.679; 14.191; 333) que 0x6350b0 le
// escribe en cada frame): el trapecio de la escena, girado −yaw de la cámara en el mapa 0x37 y 45° en
// los demás. `screenWidth` es el de 0x5bb8d0. El mapa 6 con un héroe "webzen" y el 0x1e del juego no
// van.
[[nodiscard]] TerrainPolygon TerrainPolygonOf(CullingScene scene, std::uint8_t map, const glm::vec3& hero,
                                              float cameraYaw, int screenWidth = 640);

// 0x5fba90(x, y, margen): pasa si los cuatro lados dan más que `margin`.
[[nodiscard]] bool PolygonContains(const TerrainPolygon& polygon, double x, double y, double margin);

// El margen de cada bloque (DAT_006c9d60, −40 en la .data, que nadie escribe) y el de cada celda.
inline constexpr double BlockMargin = -40.0;
inline constexpr double CellMargin = 0.0;

// El pase del terreno de 0x5fc190 y 0x5fc0e0 en el rango: los bloques de 4 × 4 por fila de bloques
// (de firstY a lastY, de a 4) y después por columna (de firstX a lastX); el que pasa el trapecio en su
// centro (x + 2, y + 2) con BlockMargin, sus 16 celdas por fila y después por columna, y la que lo
// pasa en su centro (x + 0,5, y + 0,5), con DrawTerrainCell. DAT_07d36899, la vista de arriba que deja
// pasar todo, no se prende nunca.
void DrawTerrain(Render::ModelBatch& batch, const TerrainView& view, const TerrainRange& range,
                 const TerrainPolygon& polygon, const TerrainWater& water);
} // namespace Client::World
