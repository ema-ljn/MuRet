#pragma once

#include "Core/IClock.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Render/ModelBatch.h"
#include "World/TerrainLighting.h"

#include <array>
#include <bit>
#include <cstdint>
#include <vector>

// El terreno de un mundo como triángulos del ModelBatch (CL7.1): lo que dibuja la cadena de 0x5fc260,
// verificada celda por celda contra la traza de Lorencia (06 § Terreno). Cada celda es el fan de
// cuatro vértices de 0x5fac90 en dos triángulos, con las capas que elige 0x5fa830 por el alfa de sus
// esquinas, las UV de 0x5fa5c0 y la luz y el alfa de cada esquina de 0x5fa260 y 0x5fa350. No hay pase
// del pasto (en el mapa 55 no dibuja nada, 06 § El terreno del login) ni el agua de los mapas 7, 0x1e
// y 0x22.
namespace Client::World
{
// El tile i es el slot i + 0x23 (0x5fa260 y 0x5fa350).
inline constexpr std::uint16_t FirstTileSlot = 0x23;
// Lo que mide una celda (0x6b86b4).
inline constexpr float TerrainCellSize = 100.0f;
static_assert(std::bit_cast<std::uint32_t>(TerrainCellSize) == 0x42C80000, "la constante de 0x6b86b4");
// La altura de una esquina con el bit 0x40 del atributo (DAT_006c9d5c en la .data).
inline constexpr float FixedCornerHeight = 1200.0f;
static_assert(std::bit_cast<std::uint32_t>(FixedCornerHeight) == 0x44960000, "el valor de DAT_006c9d5c");
// Cuántas veces se repite un tile cada 64 celdas: la u avanza 64 / ancho por celda (0x6ba6b0).
inline constexpr float TileSpan = 64.0f;
static_assert(std::bit_cast<std::uint32_t>(TileSpan) == 0x42800000, "la constante de 0x6ba6b0");
// Los bits del atributo de una celda que mira el terreno (0x5fac90).
inline constexpr std::uint16_t HiddenCellBit = 0x08;
inline constexpr std::uint16_t FixedHeightBit = 0x40;

// El ancho y el alto del buffer de la textura de un tile (+0x20 y +0x24 de su slot): la potencia de 2.
struct TileSize
{
    float width = 1.0f;
    float height = 1.0f;
};

// Lo que el terreno lee de un mundo. La luz es la sombreada (0x7b4ccc4, la que 0x5fbf60 copia de
// 0x7c0ccc4 en cada paso, 06 § Terreno), sin luces dinámicas.
struct TerrainView
{
    const Data::Terrain::TerrainMapping* mapping = nullptr;
    const Data::Terrain::TerrainHeights* heights = nullptr;
    const Data::Terrain::TerrainAttributes* attributes = nullptr;
    const TerrainLighting* lighting = nullptr;
    std::array<TileSize, 256> tiles{};
    // El mapa (DAT_006c9dd0): en el 0x1e el agua corre al revés.
    std::uint8_t map = 0;
};

// Las celdas de los bloques que ve la cámara (0x5fbb00): de firstX a lastX y de firstY a lastY, los
// comienzos de los bloques de 4 × 4 (DAT_07d2d364, DAT_006c9d68, DAT_07d2d368 y DAT_006c9d6c).
struct TerrainRange
{
    int firstX = 0;
    int lastX = 0;
    int firstY = 0;
    int lastY = 0;
};

// El agua: el corrimiento de la u de este frame (0x78ecbec) y la ola de cada celda del último paso
// (0x78acbd8, por índice de la grilla). Sin olas, el agua va quieta.
struct TerrainWater
{
    float offset = 0.0f;
    std::vector<float> waves;
};

// 0x5fc260, en cada frame: (ftol(T) % 20000) · 5e-5, con T el reloj en ms; en el mapa 8,
// (ftol(T) % 40000) · 2,5e-5.
[[nodiscard]] float WaterOffsetAt(Core::Duration clock, std::uint8_t map);

// 0x5fbf60, en cada paso: la ola de cada celda de la ventana (de firstX a lastX + 3 y de firstY a
// lastY + 3) es sin(x · 5 + (ftol(T) % 720000) · 0,002) · 10, con la x de la celda (x · 50 en el mapa
// 8); las demás quedan como estaban. Las olas nacen en 0.
void UpdateWaves(TerrainWater& water, Core::Duration clock, const TerrainRange& range, std::uint8_t map);

// La celda (x, y), como 0x5fac90 con 0x5fa830: nada con el bit 0x08; si no, la capa de abajo opaca
// (la 2 si las cuatro esquinas van en alfa 1, la 1 si no) y, con alguna esquina por debajo de 1 y
// alguna por encima de 0, enseguida la capa 2 con la mezcla normal y el alfa de cada esquina. Es
// agua (el corrimiento y la ola) la de abajo cuando es la capa 1 en 5, y la de alfa cuando además la
// capa 2 es 5. Las esquinas van por índice, (x, y), (x + 1, y), (x + 1, y + 1) y (x, y + 1) en la
// grilla de 256: la última celda de una fila toma las de la fila siguiente, como el original.
void DrawTerrainCell(Render::ModelBatch& batch, const TerrainView& view, const TerrainWater& water, std::uint8_t x,
                     std::uint8_t y);

// El pase del terreno de 0x5fc190 y 0x5fc0e0 en el rango: los bloques de 4 × 4 por fila de bloques
// (de firstY a lastY, de a 4) y después por columna (de firstX a lastX), y en cada bloque las 16
// celdas por fila y después por columna, con DrawTerrainCell. El recorte de cada bloque y de cada
// celda (0x5fba90) es de la tarea 3.
void DrawTerrain(Render::ModelBatch& batch, const TerrainView& view, const TerrainRange& range,
                 const TerrainWater& water);
} // namespace Client::World
