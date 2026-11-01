// TerrainLight.OZJ, la luz de base de cada celda de un mundo (Client/docs/research/
// 05-formatos-de-datos.md, "Terreno de un mundo"; en el original, el cargador de 0x63b490). Se lee
// de Data\World<N>\TerrainLight.OZJ.
//
// Es un OZJ como el de las texturas (OzjTexture.h): 24 bytes que no se leen y un JPEG, que el
// original decodifica igual, con libjpeg y sus parámetros por defecto. Pero no lo sube como
// textura: pasa cada byte a un float de 0 a 1 y lo guarda en una tabla de 256 × 256 colores RGB
// (0x078ecc10), la luz que después usa el terreno (0x5f8d90). Dos diferencias con la textura:
//   - la fila 0 de la tabla es la última del JPEG (0063b490-terrainlight-ozj-loader.md §1);
//   - no mira el tamaño ni los componentes del JPEG (Q-C31). En el laboratorio, los 24
//     TerrainLight son de 256 × 256 y en color.

#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace Client::Data::Terrain
{

// Por cuánto se multiplica cada byte de la imagen: la constante de 0x6b88f8, que es 1/255 en float.
// 255 por ella da 1 justo, así que la luz va de 0 a 1.
inline constexpr float TerrainLightScale = std::bit_cast<float>(std::uint32_t{0x3B808081});
static_assert(TerrainLightScale == 1.0f / 255.0f, "la constante de 0x6b88f8 es 1/255 (0063b490 §7)");
static_assert(255.0f * TerrainLightScale == 1.0f, "el byte más alto da una luz de 1 (0063b490 §7)");

// Los canales de un color de la luz: rojo, verde y azul, como salen del JPEG.
inline constexpr std::size_t TerrainLightChannels = 3;
using TerrainLightColor = std::array<float, TerrainLightChannels>;
static_assert(sizeof(TerrainLightColor) == TerrainLightChannels * sizeof(float),
              "los colores van seguidos, como en la tabla del original");

struct TerrainLight
{
    // La luz de cada celda, en el orden de CellIndex: 256 × 256 × 3 floats, los 0xC0000 bytes de
    // la tabla del original.
    TerrainLayer<TerrainLightColor> cells;
};
static_assert(sizeof(TerrainLight) == 0xC0000, "el tamaño de la tabla del original (0063b490 §1)");

// Lee la luz de `file`, un TerrainLight.OZJ. La celda (x, y) es el píxel x de la fila 255 - y del
// JPEG, y cada canal es el byte por TerrainLightScale.
//   - La luz ocupa 768 KB y va en el heap, como las alturas (TerrainHeights.h).
//   - Si `file` no pasa de los 24 bytes que no se leen, devuelve DataError::FileTooShort.
//   - Si libjpeg no lo puede decodificar, devuelve DataError::InvalidData. El original devuelve
//     false, y quien lo llama (0x5f8f40) no lo mira: la tabla queda como estaba (Q-C32).
//   - Si el JPEG no es de 256 × 256 en color, devuelve DataError::InvalidData. El original no lo
//     mira, y escribe fuera de la tabla o lee fuera de la imagen (Q-C31).
Core::Expected<std::unique_ptr<TerrainLight>, DataError> LoadTerrainLight(std::span<const std::byte> file);

} // namespace Client::Data::Terrain
