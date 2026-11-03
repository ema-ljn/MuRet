#pragma once

#include <cstdint>

// Las cuatro fuentes de WinMain (02 § Resolución, fuentes y texto): cuatro CreateFontA sin
// suavizado (NONANTIALIASED_QUALITY) en 0x4ba737, 0x4ba777, 0x4ba7b9 y 0x4ba813, con un alto que
// crece de a un punto con el ancho de la pantalla. Lo que MuRet saca de acá son los altos y el
// peso; la cara la elige la config ([Video] FontFile y BoldFontFile, pregunta 8 del plan de la
// CL5), porque el original la pide por nombre al sistema.
namespace Client::Render::Text
{
enum class FontKind : std::uint8_t
{
    // DAT_05763554, peso 400 y alto n − 1, con la cara del texto 0 de Text.bmd: la del login.
    Normal,
    // DAT_05763558, peso 700 y alto n − 1: la de los avisos (0x554810).
    Bold,
    // DAT_0576355c, peso 700 y alto 2 (n − 1).
    BigBold,
    // DAT_05763560, peso 400 y alto 12 fijo, con otra cara (el texto 18 de Text.bmd). MuRet usa la
    // de [Video] FontFile, como las otras.
    OtherFace12,
};

// n, el paso de la letra (DAT_07758568): WinMain lo escribe en 0x4ba6af..0x4ba6d3 con 12 para un
// ancho de 640, 13 con 800, 14 con 1024 y 15 con 1280. Con otro ancho no lo escribe, y como está
// en la .bss queda en 0: con 1600 las letras piden un alto de -1 y -2 (Q-C59). MuRet da el paso
// del más grande de esos cuatro anchos que no pasa a `screenWidth`, y 12 por debajo de 800
// (pregunta 10 del plan de la CL5): con 1600 o con un modo del monitor más ancho, 15.
[[nodiscard]] int FontStepFor(int screenWidth);

// El alto de la celda que pide CreateFontA (positivo: la celda, no la letra): n − 1 la normal y la
// negrita, 2 (n − 1) la negrita grande y 12 la otra cara.
[[nodiscard]] int FontHeightFor(FontKind font, int screenWidth);

// Si CreateFontA la pide con peso 700 (FW_BOLD) y no 400 (FW_NORMAL).
[[nodiscard]] bool IsBold(FontKind font);
} // namespace Client::Render::Text
