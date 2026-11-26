#pragma once

#include "Core/PixelSize.h"
#include "EngineApi.h"
#include "Render/OriginalQuad.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"

#include <string_view>
#include <vector>

// Where a text goes (06 § Texto). The caller gives 0x554180's x and y in 640 × 480; 0x467060 converts them to pixels,
// cuts the text into 256-column boxes and draws each with 0x554000, which builds 0x621b60's quad in pixels. *Static*,
// from 0x6350b0, 0x467060 and 0x554000.
namespace Client::Render::Text
{
// 0x6350b0, login text 454: ends at `x`, in 640 × 480. Converts the width to 640 × 480 with cx · 640 / W in unsigned
// 32-bit integers, like the original (lea, shl and div at 0x6353d1): the math truncates, and with a cx of 6710887 or
// more the product wraps.
[[nodiscard]] ENGINE_API int RightAlignedAt(int x, TextExtent extent, Core::PixelSize screen);

// 0x467060 in mode 4 (0x467146..0x46715e), for HUD texts ending at `x` (the helper, durability notices): fild of x,
// fild of cx, fdiv by rx (W times the inverse of 640, as float: DAT_05762ea8), the subtraction on the x87 and _ftol,
// which truncates toward 0. Not RightAlignedAt's math: this one is floating point. Without a screen, x as is.
[[nodiscard]] ENGINE_API int EndingAt(int x, TextExtent extent, Core::PixelSize screen);

// 0x623690: the text centered at `x`, of 640 × 480: x − (cx · 640 / W) / 2, with the same unsigned 32-bit division and
// the halving shr (selection texts 436 and 437).
[[nodiscard]] ENGINE_API int CenteredAt(int x, TextExtent extent, Core::PixelSize screen);

// The three login texts sit on `y`: y − cy · 480 / H, with the same division. The original subtracts cy · 640 / W
// (0x6353b9), which differs on a non-4:3 screen: at 1280 × 1024 the text sits one point higher (Q-C60). MuRet uses the
// height.
[[nodiscard]] ENGINE_API int BottomAlignedAt(int y, TextExtent extent, Core::PixelSize screen);

// From a screen pixel to 640 × 480, as the new UI's windows pass their texts to 0x5542f0 (0x641aa1..0x641ad6): fild of
// the pixel, fdiv by rx or ry (W or H times the inverse of 640 or 480, as float: DAT_05762ea8 and DAT_05762eac) and
// _ftol, which truncates toward 0. The round trip through TextPieces can lose a pixel: at 1024 × 768, 240 gives 149 and
// comes back as 238. Without a screen (a side at 0), the pixel as is, like RightAlignedAt.
[[nodiscard]] ENGINE_API int DesignX(int pixelX, Core::PixelSize screen);
[[nodiscard]] ENGINE_API int DesignY(int pixelY, Core::PixelSize screen);

struct TextPiece
{
    // The text column where the box starts: 256 k for box k.
    int firstColumn = 0;
    // The box's visible columns and rows, from the top left.
    int width = 0;
    int rows = 0;
    // 0x554000's: in pixels, unscaled, with du = (w + 0.01) / 256 and dv = (rows + 0.01) / 32. The slot is 0, the
    // original's FontInput.tga; the drawer sets its own.
    OriginalQuad quad;
};

// 0x467060 in mode 1 without a cap, the login's (0x554180(x, y, text, 0, 0, 0)), for a text measuring `extent`:
//   - the rows are the cell's, up to 32;
//   - if the text exceeds 256 columns and the right edge, it is cut at the edge, truncated: x · rx + width against W,
//     with rx = W / 640 as float (DAT_05762ea8);
//   - one box per 256 columns, the last with the remainder;
//   - box k goes at x · rx + 256 k and y · ry, truncated (_ftol), and 0x554000 shifts it: a negative x goes to 0, and a
//     box past the right edge goes against it.
// A 0-wide text gives no box; the original does not draw an empty text.
[[nodiscard]] ENGINE_API std::vector<TextPiece> TextPieces(int x, int y, TextExtent extent, Core::PixelSize screen);

// Like TextPieces, but `pixelX` and `pixelY` are already pixels and do not go through 640 × 480.
[[nodiscard]] ENGINE_API std::vector<TextPiece> TextPiecesAtPixels(int pixelX, int pixelY, TextExtent extent,
                                                                   Core::PixelSize screen);

// 0x467060 in mode 2 (0x46711b..0x467132), that of 0x554180(x, y, text, box, 1, ·) and 0x5542f0(·, box, 2, ·): the text
// centered in a `boxWidth`-column box, for a `textWidth` (cx) text. Drawn with Render::Frame::ITextDrawer::DrawColumns,
// with −textColumn as first column and `width` columns.
struct CenteredInBox
{
    // The DIB column where TextOutA puts the text (0x46720a): (box − cx) / 2 with C division (cdq, sub and sar,
    // truncating toward 0). With a text wider than the box it is negative, and TextOutA cuts the left.
    int textColumn = 0;
    // The box columns (lea at 0x46712c): cx + 2 · textColumn, which is the box, or one less if box − cx is odd and
    // positive, or one more if odd and negative. 0x4671b4 clears the DIB across this whole width (the background), and
    // the rest (the cut at W past 256, the boxes and their quads) is TextPieces' with this width.
    int width = 0;

    friend bool operator==(const CenteredInBox&, const CenteredInBox&) = default;
};

[[nodiscard]] ENGINE_API CenteredInBox CenterInBox(int boxWidth, int textWidth);

// CenterInBox with GetTextExtentPointA's cx for `text` in `font`: an empty one measures 0x6c7ae4's "0"
// (0x4670ba..0x4670d0), and empty without a box gives no columns (0x467077..0x467082: returns without drawing).
[[nodiscard]] ENGINE_API CenteredInBox CenterInBox(const ITextMeasure& measure, FontKind font, std::string_view text,
                                                   int boxWidth);

// 0x554000(x, y, width, rows, …, 0, 0x280) for a bubble line (0x555b00), in pixels: a negative x goes to 0 and a box
// past W goes against the edge; with 0x77f3b66 on (`clampY`), a negative y goes to 0 and a box past H − H · 47 / 640
// (with IDIV) moves up to there. The quad shows `width` columns of the 256 box (with GL_CLAMP, those past it repeat the
// last, Q-C521) and `rows` rows.
[[nodiscard]] ENGINE_API OriginalQuad BoxedQuad(int x, int y, int width, int rows, bool clampY, Core::PixelSize screen);
} // namespace Client::Render::Text
