#pragma once

#include "EngineApi.h"
#include "Render/Text/TextStyle.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// The text box: the 256 × 32 RGBA buffer that 0x554000 uploads to slot 0 with glTexImage2D (0x7d3a1cc), one dword per
// pixel, rows top to bottom. The original rasterizes with TextOutA on a 24-bit DIB (0x61bcd0: 1280 × 32, five boxes
// wide, transparent background), white on black without antialiasing, and 0x465a00 converts one box at a time: each
// DIB pixel whose first byte is 0xff gets the text color, any other the background's (06 § Texto).
namespace Client::Render::Text
{
class IGlyphRasterizer;

inline constexpr int TextBoxWidth = 256;
inline constexpr int TextBoxHeight = 32;
inline constexpr std::size_t TextBoxPixelCount = std::size_t{TextBoxWidth} * TextBoxHeight;
inline constexpr std::size_t TextBoxBytes = TextBoxPixelCount * 4;

// A box's ink, one byte per pixel, rows top to bottom: what TextOutA leaves in each DIB pixel's first byte. Without
// antialiasing it is 0xff where there is a letter and 0 elsewhere; an antialiasing rasterizer would leave grays, which
// 0x465a00 paints as background.
using InkMask = std::span<std::uint8_t, TextBoxPixelCount>;

// A box's RGBA, as 0x554000 uploads it.
using TextBoxPixels = std::span<std::uint8_t, TextBoxBytes>;

// A 0xAABBGGRR color into an RGBA pixel: red, green, blue and alpha, like the dword 0x465a00 writes read by
// glTexImage2D with GL_RGBA and GL_UNSIGNED_BYTE.
ENGINE_API void StoreColor(std::uint32_t color, std::span<std::uint8_t, 4> pixel);

// 0x467060 and 0x465a00 for one box: the text drawn from column `firstColumn` (box k of a wide text starts at 256 k; a
// negative one leaves −firstColumn background columns before the text, mode 2), and of it the top-left `width` columns
// and `rows` rows, in the text color where there is ink and the background's where not. A text starting with a line
// break is not drawn (0x4671e1): all background. The rest of the box stays 0: the original leaves it as it was, but
// 0x554000's quad does not show it.
ENGINE_API void PaintTextBox(const IGlyphRasterizer& rasterizer, const TextStyle& style, std::string_view text,
                             int firstColumn, int width, int rows, TextBoxPixels rgba);

// A bubble line's box (0x555b00 → 0x553e50): the columns (0x77f31b8, the bubble width) and the rows (the letter step).
// The ink in the columns before `prefixColumns` gets `prefixColor` (0x6c8e60): the guild and the rank pass their cx;
// the name and the lines, 0.
struct BoxShape
{
    int width = 0;
    int rows = 0;
    int prefixColumns = 0;
    std::uint32_t prefixColor = 0;

    friend bool operator==(const BoxShape&, const BoxShape&) = default;
};

// 0x553e50 on the DIB the caller cleared (`box.width` columns, up to 1280, by `box.rows` rows): the text at (0, 0) and,
// in the rows up to 32 and the box columns, the ink in the prefix or text color and the rest in the background color.
// 0x553e50 writes `box.width` columns per row into a 256 buffer: with more, each row overwrites the start of the next,
// which rewrites it later, and the last falls below what the quad shows. So the first 256 suffice (Q-C521).
ENGINE_API void PaintBoxedText(const IGlyphRasterizer& rasterizer, const TextStyle& style, std::string_view text,
                               const BoxShape& box, TextBoxPixels rgba);
} // namespace Client::Render::Text
