// OZT, una textura TGA de 32 bits (Client/docs/research/05-formatos-de-datos.md, "Texturas"; en el
// original, el cargador de 0x63bd00). El original pide "<nombre>.tga" y abre Data\<nombre>.OZT.
//
// Sin cifrar. Es un TGA con 4 bytes delante: los 4 primeros del mismo TGA, que repite su
// exportador (0x63b280). De la cabecera del TGA el original lee solo tres campos:
//
//   offset   qué es
//   0        4 bytes que no se leen
//   4        la cabecera del TGA, 18 bytes
//   0x10       el ancho, int16 con signo
//   0x12       el alto, int16 con signo
//   0x14       los bits por píxel, que tienen que ser 32
//   0x16     los píxeles: ancho × alto, 4 bytes cada uno en el orden azul, verde, rojo y alfa
//
// No mira el tipo de imagen, el largo del id ni el descriptor: lee siempre píxeles sin comprimir
// desde 0x16 y da vuelta siempre las filas. En el laboratorio, los 663 OZT de 32 bits son TGA sin
// comprimir, sin id y con el origen abajo, así que esa lectura es la correcta para todos ellos.
//
// No lo lee una biblioteca de TGA como stb_image porque lo que se lee del TGA son unas pocas
// líneas: el resto (el buffer de potencias de 2, el rechazo) habría que escribirlo igual, y la
// dependencia cuesta más que ellas (CODING_RULES §10). Además, stb_image sí mira el descriptor y
// el id, y carga el único OZT de 24 bits del laboratorio, Monster\wolfa02.OZT, que el original
// rechaza. En los 663 de 32 bits daría las mismas filas.

#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/Texture.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace Client::Data::Textures
{

// Los bytes del principio que no se leen: los 4 que agrega el exportador (0x63b280).
inline constexpr std::size_t OztPrefixSize = 4;
// Dónde está cada campo que lee el original en una cabecera de TGA (el ancho en el byte 12, el alto
// en el 14 y los bits por píxel en el 16), corrido por el prefijo.
inline constexpr std::size_t OztWidthOffset = OztPrefixSize + 12;
static_assert(OztWidthOffset == 0x10, "el offset que mide el research (0063bd00-ozt-loader.md)");
inline constexpr std::size_t OztHeightOffset = OztPrefixSize + 14;
static_assert(OztHeightOffset == 0x12, "el offset que mide el research (0063bd00-ozt-loader.md)");
inline constexpr std::size_t OztBitsPerPixelOffset = OztPrefixSize + 16;
static_assert(OztBitsPerPixelOffset == 0x14, "el offset que mide el research (0063bd00-ozt-loader.md)");
// La cabecera que hace falta para leer esos tres campos.
inline constexpr std::size_t OztHeaderSize = OztBitsPerPixelOffset + 1;
// Dónde empiezan los píxeles: después de la cabecera de 18 bytes del TGA, sin mirar el largo del id.
inline constexpr std::size_t OztPixelsOffset = OztPrefixSize + 18;
static_assert(OztPixelsOffset == 0x16, "el offset que mide el research (0063bd00-ozt-loader.md)");
// Los bits por píxel que acepta el original (cmp al,0x20).
inline constexpr std::uint8_t OztBitsPerPixel = 32;

// Lee la textura de `file`, un OZT. Los píxeles quedan en RGBA y con las filas dadas vuelta: la
// fila r del archivo es la alto - 1 - r de la textura, como en el original.
//   - Si `file` no llega a OztHeaderSize, devuelve DataError::FileTooShort.
//   - Si los bits por píxel no son 32, o el ancho o el alto pasan de TextureMaxSide, devuelve
//     DataError::InvalidData, como el original, que no carga la textura y no avisa. Pasa con
//     Monster\wolfa02.OZT, de 24 bits.
//   - Un ancho o un alto de cero o negativo pasan, como en el original (que compara con signo), y
//     la textura queda sin píxeles de imagen, de 1 de lado en ese sentido.
//   - Si faltan píxeles, devuelve DataError::FileTooShort. El original lee de más (Q-C24).
//   - Lo que sigue a los píxeles se ignora: en el laboratorio, el pie de 26 bytes de un TGA 2.0.
Core::Expected<Texture, DataError> LoadOztTexture(std::span<const std::byte> file);

} // namespace Client::Data::Textures
