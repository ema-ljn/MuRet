#pragma once

#include <cstdint>
#include <vector>

// Una fuente TrueType mínima, armada en memoria, para los tests de FreeTypeRasterizer que corren
// sin MURET_CLIENT_FONT_FILE (CL5.3). Trae las tablas que FreeType pide para abrirla (cmap, glyf,
// head, hhea, hmtx, loca y maxp), sin OS/2 ni programas de hinting, un em de 1.000 unidades con el
// ascenso en 800 y el descenso en 200, y dos glifos de 600 de avance: el 0, un cuadrado, que es el
// de todo byte que el cmap no nombra, y el 1, el de FlatGlyphLetter, una raya sin área (sus cuatro
// puntos sobre la línea de base), como deja el hinting el guion bajo de algunas fuentes reales.
namespace Client::Tests
{
inline constexpr char FlatGlyphLetter = '_';

[[nodiscard]] std::vector<std::uint8_t> TinyTrueTypeFont();
} // namespace Client::Tests
