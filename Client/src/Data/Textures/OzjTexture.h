// OZJ, una textura JPEG (Client/docs/research/05-formatos-de-datos.md, "Texturas"; en el
// original, el cargador de 0x63b870). El original pide "<nombre>.jpg" y abre Data\<nombre>.OZJ.
//
// Sin cifrar. Es un JPEG con 24 bytes delante: los 24 primeros del mismo JPEG, que repite su
// exportador (0x63b280). El original los saltea con fseek y le pasa el resto a libjpeg, sin
// mirarlos:
//
//   offset   qué es
//   0        24 bytes que no se leen
//   0x18     el JPEG, desde su marcador de inicio (FF D8)
//
// La imagen va en la esquina de una textura de lados potencia de 2, como la del OZT, pero sin dar
// vuelta las filas: la fila 0 de la textura es la primera del JPEG. Siete OZJ del laboratorio son
// en realidad TGA (Q-C14), y libjpeg los rechaza: "Not a JPEG file".

#pragma once

#include "Core/Expected.h"
#include "Data/DataError.h"
#include "Data/Textures/Texture.h"

#include <cstddef>
#include <span>

namespace Client::Data::Textures
{

// Los bytes del principio que el original saltea con fseek(0x18) antes de pasarle el archivo a
// libjpeg: los 24 que agrega el exportador (0x63b280).
inline constexpr std::size_t OzjPrefixSize = 24;
static_assert(OzjPrefixSize == 0x18, "el fseek que mide el research (0063b870-ozj-loader.md)");

// Lee la textura de `file`, un OZJ. Los píxeles quedan en RGB, con las filas en el orden del JPEG.
//   - Si `file` no pasa de OzjPrefixSize, no hay JPEG: devuelve DataError::FileTooShort.
//   - Si libjpeg corta antes de la última fila, o el ancho o el alto pasan de TextureMaxSide,
//     devuelve DataError::InvalidData. El original devuelve false y no sube la textura. Pasa con
//     los 7 TGA de Q-C14 y con el único OZJ del laboratorio de más de 512, Skill\null.OZJ
//     (800 × 600). Lo que viene después de la última fila no se lee: cuando el original lo lee, ya
//     subió la textura (Q-C32).
//   - Cada fila del JPEG se copia entera al principio de su fila de la textura, como en el
//     original: un JPEG en grises, de un byte por píxel, llena un tercio de lo que ocupa la imagen
//     en cada fila, y uno CMYK, de 4, entra si su ancho no pasa de 3/4 del de la textura (Q-C31).
//     Si una fila no entra en la de la textura, devuelve DataError::InvalidData.
Core::Expected<Texture, DataError> LoadOzjTexture(std::span<const std::byte> file);

} // namespace Client::Data::Textures
