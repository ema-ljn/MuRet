#pragma once

#include "Render/Text/TextStyle.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// La caja del texto: el buffer RGBA de 256 × 32 que 0x554000 sube al slot 0 con glTexImage2D
// (0x7d3a1cc), un dword por píxel y las filas de arriba hacia abajo. El original rasteriza el texto
// con TextOutA sobre un DIB de 24 bits (0x61bcd0: 1.280 × 32, el ancho de cinco cajas, con el fondo
// transparente), en blanco sobre negro y sin suavizado, y 0x465a00 pasa de a una caja: cada píxel
// del DIB cuyo primer byte es 0xff va con el color del texto, y cualquier otro, con el del fondo
// (06 § Texto).
namespace Client::Render::Text
{
class IGlyphRasterizer;

inline constexpr int TextBoxWidth = 256;
inline constexpr int TextBoxHeight = 32;
inline constexpr std::size_t TextBoxPixelCount = std::size_t{TextBoxWidth} * TextBoxHeight;
inline constexpr std::size_t TextBoxBytes = TextBoxPixelCount * 4;

// La tinta de una caja, un byte por píxel y las filas de arriba hacia abajo: lo que deja TextOutA
// en el primer byte de cada píxel del DIB. Sin suavizado es 0xff donde hay letra y 0 donde no; un
// rasterizador con suavizado dejaría grises, que 0x465a00 pinta como fondo.
using InkMask = std::span<std::uint8_t, TextBoxPixelCount>;

// El RGBA de una caja, como lo sube 0x554000.
using TextBoxPixels = std::span<std::uint8_t, TextBoxBytes>;

// Un color de 0xAABBGGRR en un píxel del RGBA: rojo, verde, azul y alfa, como el dword que escribe
// 0x465a00 leído por glTexImage2D con GL_RGBA y GL_UNSIGNED_BYTE.
void StoreColor(std::uint32_t color, std::span<std::uint8_t, 4> pixel);

// 0x467060 y 0x465a00 para una caja: el texto dibujado desde la columna `firstColumn` (la caja k de
// un texto ancho empieza en 256 k), y de él, las `width` columnas y las `rows` filas de arriba a la
// izquierda, con el color del texto donde hay tinta y el del fondo donde no. El resto de la caja
// queda en 0: el original lo deja como estaba, pero el quad de 0x554000 no lo muestra.
void PaintTextBox(const IGlyphRasterizer& rasterizer, const TextStyle& style, std::string_view text, int firstColumn,
                  int width, int rows, TextBoxPixels rgba);
} // namespace Client::Render::Text
