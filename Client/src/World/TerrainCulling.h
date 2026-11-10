#pragma once

#include "Core/Expected.h"
#include "Render/ModelBatch.h"
#include "Render/Pass3D.h"
#include "World/TerrainCells.h"

#include <glm/ext/matrix_float4x4.hpp>
#include <glm/ext/vector_float2.hpp>
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

// El trapecio de 0x5fb570 en celdas: izquierda lejana, derecha lejana, derecha cercana, izquierda cercana.
// El global del original (0x78ecbf0 y 0x78ecc00) es Scenes::WorldRuntime::terrainPolygon; el valor por
// defecto, los ceros de la .bss.
struct TerrainPolygon
{
    std::array<float, 4> x{};
    std::array<float, 4> y{};
    // En las escenas que no son 2, 4 ni 5, 0x5fba90 deja pasar todo.
    bool everything = false;
};

// Nivel efectivo persistente (0x7b4ccb8), distinto del solicitado de la cámara. Su dueño es
// Scenes::WorldRuntime, uno para todas las escenas; el 0 del DTO es el de la .bss (*estático*).
struct TerrainPolygonState
{
    std::int32_t effectiveLevel = 0;
};

// Snapshot sin alias: translation es el argumento3, objectPosition es el selector XY de mapa30.
// Los booleanos son los resultados completos de dos consultas independientes de nombre en mapa6.
// Geometría finita representable; yaw usado dentro del dominio de OriginalRotation. El selector30
// permite quiet NaN/inf y elige el perfil pequeño (06 § El trapecio completo).
struct TerrainPolygonInput
{
    std::int32_t scene = 5;
    std::int32_t map = 0;
    glm::vec3 translation{};
    glm::vec2 objectPosition{};
    float cameraYaw = 0.0f;
    std::int32_t worldWidth = 640;
    bool webzenProfile = false;
    bool webzenYaw = false;
};

struct TerrainPolygonCameraInput
{
    std::int16_t requestedLevel = 0;
    float distance = 1000.0f;
    float targetDistance = 1000.0f;
};

enum class TerrainPolygonError : std::uint8_t
{
    InvalidCameraDistance,
    InvalidEffectiveLevel,
};

struct TerrainPolygonFrame
{
    TerrainPolygonState nextState;
    TerrainPolygon polygon;
};

// Un frame original: especiales6/30 retienen sin consumir cámara. En normal, valida D/meta en
// [-2^63,2^63), trunca a int64 y compara los bajos32 con signo; luego valida el nivel elegido0..4.
// Un error no publica estado ni geometría (ADR0009); la función no altera previous.
[[nodiscard]] Core::Expected<TerrainPolygonFrame, TerrainPolygonError>
TerrainPolygonFrameOf(const TerrainPolygonState& previous, const TerrainPolygonInput& input,
                      const TerrainPolygonCameraInput& camera);

// Consulta sin transición. Other calcula arrays y solo desactiva el recorte del consumidor.
[[nodiscard]] Core::Expected<TerrainPolygon, TerrainPolygonError> TerrainPolygonOf(const TerrainPolygonInput& input,
                                                                                   std::int32_t effectiveLevel);

// 0x5fb570 alrededor de `hero` (DAT_0743a170: en el login, el (11.679; 14.191; 333) que 0x6350b0 le
// escribe en cada frame): el trapecio de la escena, girado −yaw de la cámara en el mapa 0x37 y 45° en
// los demás. `screenWidth` es el de 0x5bb8d0. Wrapper de nivel0 y ambos predicados false: usa
// hero también como selector30/5. Other conserva el sentinel legacy de arrays+0; sin estado oculto.
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
