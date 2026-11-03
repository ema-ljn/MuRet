#pragma once

#include "App/WindowState.h"
#include "Render/OriginalQuad.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <vector>

// Dónde va un texto (06 § Texto). El que llama da la x y la y de 0x554180 en 640 × 480; 0x467060
// las pasa a píxeles, corta el texto en cajas de 256 columnas y dibuja cada una con 0x554000, que
// arma el quad de 0x621b60 en píxeles. *Estático*, de 0x6350b0, 0x467060 y 0x554000.
namespace Client::Render::Text
{
// 0x6350b0, el texto 454 del login: termina en `x`, de 640 × 480. Pasa el ancho a 640 × 480 con
// cx · 640 / W en enteros de 32 bits sin signo, como el original (lea, shl y div en 0x6353d1): la
// cuenta se trunca, y con un cx de 6.710.887 o más el producto da la vuelta.
[[nodiscard]] int RightAlignedAt(int x, TextExtent extent, App::PixelSize screen);

// Los tres textos del login se apoyan en `y`: y − cy · 480 / H, con la misma división. El original
// resta cy · 640 / W (0x6353b9), que en una pantalla que no es 4:3 da otra cosa: en 1280 × 1024 el
// texto queda un punto más arriba (Q-C60). MuRet usa el alto.
[[nodiscard]] int BottomAlignedAt(int y, TextExtent extent, App::PixelSize screen);

// Una caja de un texto y el quad que la muestra.
struct TextPiece
{
    // La columna del texto donde empieza la caja: 256 k para la caja k.
    int firstColumn = 0;
    // Las columnas y las filas de la caja que se ven, desde arriba a la izquierda.
    int width = 0;
    int rows = 0;
    // El de 0x554000: en píxeles, sin escalar nada, con du = (w + 0,01) / 256 y dv = (filas + 0,01)
    // / 32. El slot es el 0, el de FontInput.tga en el original; el que dibuja pone el suyo.
    OriginalQuad quad;
};

// 0x467060 con el modo 1 y sin tope, el del login (0x554180(x, y, texto, 0, 0, 0)), para un texto
// que mide `extent`:
//   - las filas son las de la celda, hasta 32;
//   - si el texto pasa de 256 columnas y del borde derecho, se corta en el borde, truncado: x · rx
//     + ancho contra W, con rx = W / 640 en float (DAT_05762ea8);
//   - una caja por cada 256 columnas, la última con lo que sobra;
//   - la caja k va en x · rx + 256 k y en y · ry, truncadas (_ftol), y 0x554000 la corre: una x
//     negativa va a 0, y una caja que pasa del borde derecho va contra él.
// Un texto de ancho 0 no da ninguna caja; el original no dibuja un texto vacío.
[[nodiscard]] std::vector<TextPiece> TextPieces(int x, int y, TextExtent extent, App::PixelSize screen);
} // namespace Client::Render::Text
