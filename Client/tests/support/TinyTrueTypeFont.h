#pragma once

#include <cstdint>
#include <vector>

// A minimal TrueType font, built in memory, for the FreeTypeRasterizer tests that run without MU_CLIENT_FONT_FILE. It
// has the tables FreeType asks for to open it (cmap, glyf, head, hhea, hmtx, loca and maxp), without OS/2 or hinting
// programs, an em of 1,000 units with the ascent at 800 and the descent at 200, and two glyphs of 600 advance: 0, a
// square, the one of every byte the cmap does not name, and 1, FlatGlyphLetter's, a line without area (its four
// points on the baseline), as hinting leaves the underscore of some real fonts.
namespace Client::Tests
{
inline constexpr char FlatGlyphLetter = '_';

[[nodiscard]] std::vector<std::uint8_t>
TinyTrueTypeFont(std::uint16_t extraFlatCodePoint = static_cast<std::uint16_t>(FlatGlyphLetter));
} // namespace Client::Tests
