#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Terrain/TerrainGrid.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

// EncTerrain<N>.att, los atributos de cada celda de un mundo (zona segura, no caminable…;
// Client/docs/research/05-formatos-de-datos.md, "Terreno de un mundo"; en el original, el cargador
// de 0x5f8390). Se lee de Data\World<N>\EncTerrain<N>.att. En dos mundos, según el estado del
// juego, el nombre lleva otro número (0x61a6ea, 0x61a710): World31\EncTerrain312.att y
// World35\EncTerrain351.att o 352. Su cabecera dice 31 o 35, el N de la carpeta.
//
// Una cabecera de 4 bytes, 00 N FF FF, con N el número del mundo, y un atributo por celda, en el
// orden de CellIndex (TerrainGrid.h). Hay dos tamaños: un byte por celda o dos (un WORD
// little-endian, del que el original se queda con el byte de abajo). Todo el archivo, cabecera
// incluida, va cifrado con XOR3 y, encima, con K16: para descifrarlo, primero se saca K16 y
// después XOR3.
//
// El XOR3 es el de las tablas .bmd (Core/Xor3.h): el original tiene otra copia de la misma
// función para el .att (0x5f8650), con la misma clave.
//
// Ningún atributo llega a 0x80: el original lo comprueba y, si no, muestra "Data error" y cierra el
// juego. Qué es cada bit se lee en la CL7; el 0x04 parece ser "bloqueado": lo tiene casi toda el
// agua.
//
// El original también compara una celda fija por mundo, en los cinco primeros (World1 a World5),
// contra un valor esperado. Es una defensa contra archivos modificados y MuRet no la porta (Q-C26 en
// Client/docs/research/07-quirks-y-bugs.md).
namespace Client::Data::Terrain
{
// La cabecera: 00, el número del mundo, FF, FF.
inline constexpr std::size_t TerrainAttributesHeaderSize = 4;
// El byte de la cabecera con el número del mundo, el que devuelve el cargador del original.
inline constexpr std::size_t TerrainAttributesWorldOffset = 1;
// Los dos tamaños que acepta el original: la cabecera y un byte por celda, o dos.
inline constexpr std::size_t TerrainAttributesSizeWithByteCells = TerrainAttributesHeaderSize + TerrainCellCount;
inline constexpr std::size_t TerrainAttributesSizeWithWordCells = TerrainAttributesHeaderSize + 2 * TerrainCellCount;
static_assert(TerrainAttributesSizeWithByteCells == 0x10004, "el tamaño que mide el research (005f8390)");
static_assert(TerrainAttributesSizeWithWordCells == 0x20004, "el tamaño que mide el research (005f8390)");
// El primer atributo que el original rechaza (0x5f85fd compara cada celda con 0x80).
inline constexpr std::uint16_t TerrainAttributeLimit = 0x80;

struct TerrainAttributes
{
    // Un WORD por celda, como en la memoria del original (0x07D0D348): el juego le cambia bits
    // después (la CL7). Recién cargado, ninguno pasa de 0x7F.
    TerrainLayer<std::uint16_t> cells;
};

// Descifra `file`, el .att de la carpeta World<world>.
//   - Los atributos van en el heap, como el mapeo (TerrainMapping.h).
//   - Si el tamaño no es ninguno de los dos, devuelve DataError::UnexpectedSize. El original
//     devuelve -1 sin avisar, quien llama tampoco avisa con un -1, y el mundo queda con los
//     atributos del mapa anterior (Q-C25).
//   - Si la cabecera no es 00 N FF FF o un atributo llega a TerrainAttributeLimit, devuelve
//     DataError::InvalidData, el "Data error" del original.
//   - Si la cabecera es de otro mundo, devuelve DataError::WrongWorld. En el original es el que
//     llama el que muestra "%s file corrupted." y cierra el juego.
Core::Expected<std::unique_ptr<TerrainAttributes>, DataError> LoadTerrainAttributes(std::span<const std::byte> file,
                                                                                    std::uint8_t world);
} // namespace Client::Data::Terrain
