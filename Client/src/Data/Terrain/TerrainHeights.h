#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// TerrainHeight.OZB, la altura de cada celda de un mundo (Client/docs/research/
// 05-formatos-de-datos.md, "Terreno de un mundo"; en el original, el cargador de 0x5f9020). Se lee
// de Data\World<N>\TerrainHeight.OZB.
//
// Sin cifrar. Es un BMP de 256 × 256 en grises, con 4 bytes delante que el original saltea (su
// exportador, 0x63b280, repite ahí los 4 primeros del BMP). En claro:
//
//   offset   qué es
//   0        4 bytes que no se leen
//   4        lo que el original toma como cabecera del BMP: 0x438 bytes
//   0x43C    un byte por celda, en el orden de CellIndex (TerrainGrid.h), sin dar vuelta las filas
//
// Q-C02: el original no mira bfOffBits, el campo donde el BMP dice dónde empiezan sus píxeles.
// En los BMP de 8 bits del laboratorio dice 0x436, y el original lee 2 bytes más adelante: cada
// fila le queda corrida dos celdas. Se reproduce, porque el agua y los atributos del mundo se
// editaron sobre el terreno así, corrido, como lo ve el juego (005f9020-terrainheight-ozb.md §6).
//
// Por eso no lo lee una biblioteca de BMP. La que ya está en el proyecto, SDL_LoadBMP_IO de SDL3,
// salta a bfOffBits y da vuelta las filas: con el de Devias da 64.500 alturas distintas de las del
// original. Y no carga el de World11 (cortado) ni el de World19 (sin "BM"). Son unas pocas líneas.
namespace Client::Data::Terrain
{
// Los bytes del principio que el original saltea con fseek antes de leer el BMP (0x5f91d3).
inline constexpr std::size_t TerrainHeightsPrefixSize = 4;
// Lo que el original copia como cabecera del BMP: 0x10e dwords (005f9020 §2). Un BMP de 8 bits con
// paleta tiene 0x436 (14 + 40 + 256 colores de 4 bytes); el original toma 2 bytes de más. De la
// cabecera no se queda nada: en el original solo la lee el guardador de 0x5f92b0, una herramienta
// de desarrollo (005f9020 §2).
inline constexpr std::size_t TerrainHeightsBitmapHeaderSize = 0x438;
// Dónde está la altura de la celda 0 en el archivo.
inline constexpr std::size_t TerrainHeightsValuesOffset = TerrainHeightsPrefixSize + TerrainHeightsBitmapHeaderSize;
static_assert(TerrainHeightsValuesOffset == 0x43C, "el origen que mide el research (005f9020 §2)");
// El archivo entero: el prefijo y los 0x10438 bytes que lee el original con un solo fread.
inline constexpr std::size_t TerrainHeightsSize = TerrainHeightsValuesOffset + TerrainCellCount;
static_assert(TerrainHeightsSize == 66620, "el tamaño que mide el research (005f9020 §6)");

// Por cuánto se multiplica cada byte para pasarlo a altura, la constante de 0x6b8b94. Todos los
// bytes por 1,5 o por 3 dan un float justo, sin redondeo.
inline constexpr float TerrainHeightsScale = 1.5f;
// La de un solo mundo, el 0x37 del original (0x6b86b8), que guarda su terreno en World56.
inline constexpr float TerrainHeightsScaleOfWorld56 = 3.0f;
// La carpeta de ese mundo: World56. Ningún otro mundo la comparte (05, Terreno de un mundo).
inline constexpr std::uint8_t TerrainHeightsWorldWithDoubleScale = 56;

struct TerrainHeights
{
    // La altura de cada celda, en las unidades del mundo: el byte por TerrainHeightsScale.
    TerrainLayer<float> cells;
};

// Lee las alturas de `file`, el TerrainHeight.OZB de la carpeta World<world>.
//   - Las alturas ocupan 256 KB y van en el heap, como el mapeo del .map (TerrainMapping.h).
//   - No mira la cabecera del BMP, como el original: en el laboratorio, la de World11 es de un BMP
//     de 24 bits y la de World19 son todos ceros, y los dos cargan (Q-C02).
//   - Si `file` es más corto que TerrainHeightsSize, devuelve DataError::FileTooShort. El original
//     no mira lo que lee su fread, y las celdas que faltan quedan con lo que había en el heap
//     (Q-C24).
//   - Si es más largo, el resto se ignora, como en el original.
Core::Expected<std::unique_ptr<TerrainHeights>, DataError> LoadTerrainHeights(std::span<const std::byte> file,
                                                                              std::uint8_t world);
} // namespace Client::Data::Terrain
