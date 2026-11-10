#pragma once

#include "Core/Expected.h"
#include "Core/IClock.h"
#include "Data/Terrain/TerrainAttributes.h"
#include "Data/Terrain/TerrainHeights.h"
#include "Data/Terrain/TerrainMapping.h"
#include "Render/ModelBatch.h"
#include "World/TerrainLighting.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// El terreno de un mundo como triángulos del ModelBatch (CL7.1): lo que dibuja la cadena de 0x5fc260,
// verificada celda por celda contra la traza de Lorencia (06 § Terreno). Cada celda es el fan de
// cuatro vértices de 0x5fac90 en dos triángulos, con las capas que elige 0x5fa830 por el alfa de sus
// esquinas, las UV de 0x5fa5c0 y la luz y el alfa de cada esquina de 0x5fa260 y 0x5fa350. El pase del
// pasto es la misma celda con 0x7d0d31c en 2 (06 § El pasto): solo lo dibuja el juego (en el mapa 55
// no dibuja nada, 06 § El terreno del login). No está el agua de los mapas 7 (0x5fa4b0, con el 5.º
// argumento de 0x5fa5c0 en 1) y 0x22; el corrimiento al revés del 0x1e, sí.
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
    // DAT_006c9d5c compartida con la cámara: 1.200, o 1.201 después del primer 0x40.
    float fixedHeight = FixedCornerHeight;
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

// El pasto de la capa 1 en k va en el slot 0x32 + k (0x5faa8c). 0x61a560 sube en 0x32..0x34
// TileGrass01..03.tga de World<N>\ (0x63bd00, GL_NEAREST y repitiendo, sin avisar si falta).
inline constexpr std::uint16_t FirstGrassSlot = 0x32;
inline constexpr std::size_t GrassTextureCount = 3;
// Las filas del corrimiento del pasto que lee el pase: 0x5faabf toma la fila & 0xff (Q-C146).
inline constexpr std::size_t GrassOffsetRows = 256;

// Lo que el pase del pasto lee además del terreno (0x5fa830 con 0x7d0d31c en 2).
struct TerrainGrass
{
    // El alto del buffer (+0x24 del slot) de cada textura del pasto subida, de 0x32 a 0x34; sin valor,
    // la que no está (+0x34 en 0): 0x6070a0 suelta los slots 0x23..0x67 en cada carga y 0x63bd00 no
    // sube la que falta o no se lee.
    std::array<std::optional<float>, GrassTextureCount> heights{};
    // El corrimiento de la u de cada fila (0x7b0cca4, que llena 0x5f8300 al cargar el mapa).
    std::span<const float> offsets{};
};

// La celda (x, y) en el pase del pasto, como 0x5fac90 con 0x5fa830 y 0x7d0d31c en 2: nada con el bit
// 0x08, con alguna esquina en alfa por encima de 0, en los mapas 11 a 17 o con el slot 0x32 + capa 1 sin
// textura. Si no, un GL_QUADS con la mezcla de 0x620a00(1) parado sobre la diagonal de la celda: abajo
// las esquinas (x + 1, y + 1) y (x, y) con su altura; arriba las mismas, 50 menos en x y el doble del
// alto del buffer de la textura más en z, con la ola de la esquina (x, y) y la de (x + 1, y) en y (Q-C227). La u
// es x · 0,25 más el corrimiento de la fila y el color, la luz de cada esquina en orden, con alfa 1
// (glColor3fv). Corta con error si el corrimiento no tiene las 256 filas o si el slot es 0x41 o más, que
// MuRet no sube (0x5faaa3: las wt de 0x616ec0, las hojas…); del 0x35 al 0x40 no los sube nadie.
[[nodiscard]] Core::Expected<void, std::string> DrawGrassCell(Render::ModelBatch& batch, const TerrainView& view,
                                                              const TerrainWater& water, const TerrainGrass& grass,
                                                              std::uint8_t x, std::uint8_t y);

} // namespace Client::World
