// EncTerrain<N>.map, las capas de tiles de un mundo (Client/docs/research/05-formatos-de-datos.md,
// "Terreno de un mundo"; en el original, el cargador de 0x5f87b0). Se lee de
// Data\World<N>\EncTerrain<N>.map.
//
// 0x30002 bytes cifrados con K16 de punta a punta, en un solo flujo y sin XOR3:
//
//   offset    qué es
//   0         la versión: el original no la lee (su exportador escribe 0)
//   1         el número del mundo: el N de la carpeta World<N>
//   0x00002   capa 1: el tile de cada celda
//   0x10002   capa 2: el tile que se mezcla encima
//   0x20002   alfa: cuánto se ve la capa 2, un byte por celda
//
// Cada capa tiene un valor por celda, en el orden de CellIndex (TerrainGrid.h). Qué es cada capa
// sale de lo que hacen con ellas los que las leen; el research lo da como probable.
//
// Si el archivo no está, el original carga el mundo igual, con valores por defecto y sin avisar
// (L1 = 0, L2 = 0xFF, alfa = 0; Q-C27). Eso es de quien abre el archivo, en la CL7, como la tabla
// de rand() que llena el mismo 0x5f8300.

#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace Client::Data::Terrain
{

// El byte con el número del mundo, el que devuelve el cargador del original (0x5f88b5).
inline constexpr std::size_t TerrainMappingWorldOffset = 1;
// Dónde empieza cada capa en el archivo, después de los 2 bytes de cabecera y una detrás de la
// otra (005f87b0-encterrain-map.md §3). El cifrado no cambia los lugares.
inline constexpr std::size_t TerrainMappingLayer1Offset = 2;
inline constexpr std::size_t TerrainMappingLayer2Offset = TerrainMappingLayer1Offset + TerrainCellCount;
inline constexpr std::size_t TerrainMappingAlphaOffset = TerrainMappingLayer2Offset + TerrainCellCount;
static_assert(TerrainMappingLayer2Offset == 0x10002, "el offset que mide el research (005f87b0 §3)");
static_assert(TerrainMappingAlphaOffset == 0x20002, "el offset que mide el research (005f87b0 §3)");
// El archivo entero: los 2 bytes de cabecera y las tres capas de un byte por celda.
inline constexpr std::size_t TerrainMappingSize = TerrainMappingAlphaOffset + TerrainCellCount;
static_assert(TerrainMappingSize == 0x30002, "el tamaño que mide el research (05, Terreno de un mundo)");

// Por cuánto se multiplica el byte de alfa para pasarlo a un float de 0 a 1. Es la constante del
// original (0x6b88f8), 1/255 redondeado a float. Multiplicar por ella no da siempre lo mismo que
// dividir por 255: con 51, una da 0x3E4CCCCE y la otra 0.2f (0x3E4CCCCD). El 255 da 1.0f justo, y
// eso importa: el renderer compara el alfa con 0.0f y con 1.0f (0x5fa830).
inline constexpr float TerrainAlphaScale = 1.0f / 255.0f;
static_assert(std::bit_cast<std::uint32_t>(TerrainAlphaScale) == 0x3B808081, "la constante de 0x6b88f8");

struct TerrainMapping
{
    // El tile de base de cada celda. El original le asigna la textura `tile + 0x23`, y el 5 es agua.
    TerrainLayer<std::uint8_t> layer1;
    // El tile que se mezcla encima de la capa 1; 0xFF es ninguno, aunque con alfa el original lo
    // dibuja igual, con una textura que no es un tile (Q-C19).
    TerrainLayer<std::uint8_t> layer2;
    // Cuánto se ve la capa 2, de 0.0f a 1.0f: el byte del archivo por TerrainAlphaScale. Hay celdas
    // con alfa y capa 2 en 0xFF en los archivos reales: una no implica la otra.
    TerrainLayer<float> alpha;
};

// Descifra los primeros TerrainMappingSize bytes de `file`, el EncTerrain.map de la carpeta
// World<world>.
//   - El mapeo ocupa 384 KB y va en el heap. En la pila no entra cómodo, y meterlo en un Expected
//     sería copiarlo: mover un std::array mueve uno por uno sus elementos, que acá son bytes y
//     floats.
//   - Si `file` es más corto, devuelve DataError::FileTooShort. El original no mira el tamaño y
//     leería después del buffer (Q-C24 en Client/docs/research/07-quirks-y-bugs.md).
//   - Si es más largo, el resto se ignora, como en el original.
//   - Si el byte 1 no es `world`, devuelve DataError::WrongWorld. El original carga las capas
//     igual, y es el que llama el que muestra "%s file corrupted." y cierra el juego.
Core::Expected<std::unique_ptr<TerrainMapping>, DataError> LoadTerrainMapping(std::span<const std::byte> file,
                                                                              std::uint8_t world);

} // namespace Client::Data::Terrain
