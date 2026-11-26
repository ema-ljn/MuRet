#include "Render/Text/TextLayout.h"

#include "Core/PixelSize.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"

#include "support/FakeGlyphRasterizer.h"

#include <gtest/gtest.h>

#include <vector>

// Where the login texts go (0x6350b0) and how 0x467060 cuts and places them with 0x554000 (06 § Texto). The texts are
// synthetic and the fake rasterizer measures them: 6 columns per letter and 13 rows.
namespace
{
using Client::Core::PixelSize;
using Client::Render::Text::BottomAlignedAt;
using Client::Render::Text::BoxedQuad;
using Client::Render::Text::CenteredAt;
using Client::Render::Text::CenteredInBox;
using Client::Render::Text::CenterInBox;
using Client::Render::Text::DesignX;
using Client::Render::Text::DesignY;
using Client::Render::Text::EndingAt;
using Client::Render::Text::FontKind;
using Client::Render::Text::RightAlignedAt;
using Client::Render::Text::TextExtent;
using Client::Render::Text::TextPieces;
using Client::Render::Text::TextPiecesAtPixels;
using Client::Tests::FakeGlyphRasterizer;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
constexpr PixelSize Screen1280{1280, 1024};

// The three calls to 0x554180 of 0x6350b0: text 454 ends at x = 335, 455 starts there and 456 (the version) goes at
// x = 0; all three rest on y = 479. With a width of 60 and a height of 13: at 640 × 480 the box of 454 goes at 275 and
// y at 466; at 1024 × 768, 60 · 640 / 1024 = 37.5 and 13 · 480 / 768 = 8.125, truncated, give 298 and 471, which in
// pixels are 298 · 1.6 = 476.8 and 471 · 1.6000001 = 753.6, truncated again.
TEST(TextLayoutTest, PlacesTheLoginTextsLikeTheOriginal)
{
    const FakeGlyphRasterizer rasterizer;
    const TextExtent extent = rasterizer.Measure(FontKind::Normal, "abcdefghij");
    ASSERT_EQ(extent, (TextExtent{60, 13}));

    EXPECT_EQ(RightAlignedAt(335, extent, Screen640), 275);
    EXPECT_EQ(BottomAlignedAt(479, extent, Screen640), 466);
    const auto at640 =
        TextPieces(RightAlignedAt(335, extent, Screen640), BottomAlignedAt(479, extent, Screen640), extent, Screen640);
    ASSERT_EQ(at640.size(), 1u);
    EXPECT_EQ(at640[0].quad.x, 275.0f);
    EXPECT_EQ(at640[0].quad.y, 466.0f);

    EXPECT_EQ(RightAlignedAt(335, extent, Screen1024), 298);
    EXPECT_EQ(BottomAlignedAt(479, extent, Screen1024), 471);
    const int y = BottomAlignedAt(479, extent, Screen1024);
    const auto text454 = TextPieces(RightAlignedAt(335, extent, Screen1024), y, extent, Screen1024);
    const auto text455 = TextPieces(335, y, extent, Screen1024);
    const auto text456 = TextPieces(0, y, extent, Screen1024);
    ASSERT_EQ(text454.size(), 1u);
    ASSERT_EQ(text455.size(), 1u);
    ASSERT_EQ(text456.size(), 1u);
    EXPECT_EQ(text454[0].quad.x, 476.0f);
    EXPECT_EQ(text455[0].quad.x, 536.0f);
    EXPECT_EQ(text456[0].quad.x, 0.0f);
    EXPECT_EQ(text454[0].quad.y, 753.0f);
    EXPECT_EQ(text455[0].quad.y, 753.0f);
    EXPECT_EQ(text456[0].quad.y, 753.0f);
}

// Mode 4 of 0x467060, the HUD helper's name that ends at 625: in floating point, x − cx / rx, truncated towards 0. At
// 1024 × 768, with a cx of 19: 625 − 11.875 gives 613 (the Lorencia trace draws it at 613 · 1.6 = 980.8, truncated),
// while RightAlignedAt's integer math gives 614. On the negative side it truncates towards 0.
TEST(TextLayoutTest, TheHudTextsEndAtXInFloatingPoint)
{
    constexpr TextExtent Label{19, 13};

    EXPECT_EQ(EndingAt(625, Label, Screen1024), 613);
    EXPECT_EQ(RightAlignedAt(625, Label, Screen1024), 614);
    EXPECT_EQ(EndingAt(625, TextExtent{60, 13}, Screen640), 565);
    EXPECT_EQ(EndingAt(0, Label, Screen1024), -11);
    EXPECT_EQ(EndingAt(625, Label, PixelSize{0, 0}), 625);
}

// cx · 640 / W in unsigned 32 bits and truncated: 13 · 640 / 1024 = 8.125 gives 8, and with a cx of 6710887 the
// product passes 2³² and wraps around to 384, which divided by 640 gives 0. With a 64-bit int it would give
// 335 − 6710887.
TEST(TextLayoutTest, TheDivisionIsUnsignedAndInteger)
{
    EXPECT_EQ(RightAlignedAt(335, TextExtent{13, 0}, Screen1024), 327);
    EXPECT_EQ(RightAlignedAt(335, TextExtent{1, 0}, Screen1024), 335);
    EXPECT_EQ(RightAlignedAt(335, TextExtent{6710887, 0}, Screen640), 335);
    EXPECT_EQ(RightAlignedAt(335, TextExtent{6710886, 0}, Screen640), 335 - 6710886);

    EXPECT_EQ(BottomAlignedAt(479, TextExtent{0, 13}, Screen1024), 471);
    EXPECT_EQ(BottomAlignedAt(479, TextExtent{0, 8947849}, Screen640), 479);
}

// 0x623690: x − (cx · 640 / W) / 2, with unsigned integer division and the half truncated: 13 columns at 1024 give
// 8.125 → 8 → 4; 15 at 640, 15 → 7.
TEST(TextLayoutTest, TheCenteredTextTakesHalfOfItsWidthIn640)
{
    EXPECT_EQ(CenteredAt(320, TextExtent{13, 0}, Screen1024), 316);
    EXPECT_EQ(CenteredAt(320, TextExtent{15, 0}, Screen640), 313);
    EXPECT_EQ(CenteredAt(320, TextExtent{0, 0}, Screen640), 320);
    EXPECT_EQ(CenteredAt(320, TextExtent{6710887, 0}, Screen640), 320);
    EXPECT_EQ(CenteredAt(320, TextExtent{15, 0}, PixelSize{0, 0}), 320);
}

// Q-C60: the original subtracts cy · 640 / W in y too. At 4:3 it makes no difference; at 1280 × 1024, with a height
// of 14, it subtracts 7 instead of 6.
TEST(TextLayoutTest, TheLoginTextUsesTheHeightForY)
{
    const TextExtent extent{60, 14};
    const auto original = static_cast<int>(479u - (14u * 640u) / 1280u);

    EXPECT_EQ(original, 472);
    EXPECT_EQ(BottomAlignedAt(479, extent, Screen1280), 473);
    EXPECT_EQ(BottomAlignedAt(479, extent, Screen1024), 479 - static_cast<int>((14u * 640u) / 1024u));
}

// The quad of 0x554000: slot 0, in pixels and unscaled, with the 256 × 32 box clipped to what shows plus 0.01 of a
// texel (0x6b88d4).
TEST(TextLayoutTest, TheQuadIsInPixelsWithTheUvOfTheBox)
{
    const auto pieces = TextPieces(10, 20, TextExtent{60, 13}, Screen640);

    ASSERT_EQ(pieces.size(), 1u);
    const auto& piece = pieces[0];
    EXPECT_EQ(piece.firstColumn, 0);
    EXPECT_EQ(piece.width, 60);
    EXPECT_EQ(piece.rows, 13);
    EXPECT_EQ(piece.quad.slot.value, 0);
    EXPECT_EQ(piece.quad.x, 10.0f);
    EXPECT_EQ(piece.quad.y, 20.0f);
    EXPECT_EQ(piece.quad.width, 60.0f);
    EXPECT_EQ(piece.quad.height, 13.0f);
    EXPECT_EQ(piece.quad.u, 0.0f);
    EXPECT_EQ(piece.quad.v, 0.0f);
    EXPECT_EQ(piece.quad.du, 0.23441405594348907f);
    EXPECT_EQ(piece.quad.dv, 0.4065625071525574f);
    EXPECT_FALSE(piece.quad.scaleSize);
    EXPECT_FALSE(piece.quad.scalePosition);
    EXPECT_EQ(piece.quad.alpha, 0.0f);
}

// The caller of an EDIT already has the origin in pixels. Do not go back through 640 × 480, because at 1024 × 768
// origin 239 would come back as 238.
TEST(TextLayoutTest, PixelAnchoredPiecesKeepTheirOriginAndClipAcrossTiles)
{
    const auto pieces = TextPiecesAtPixels(239, 21, TextExtent{600, 13}, Screen1024);

    ASSERT_EQ(pieces.size(), 3u);
    EXPECT_EQ(pieces[0].quad.x, 239.0f);
    EXPECT_EQ(pieces[1].quad.x, 495.0f);
    EXPECT_EQ(pieces[2].quad.x, 751.0f);
    EXPECT_EQ(pieces[0].firstColumn, 0);
    EXPECT_EQ(pieces[1].firstColumn, 256);
    EXPECT_EQ(pieces[2].firstColumn, 512);
    EXPECT_EQ(pieces[0].width, 256);
    EXPECT_EQ(pieces[1].width, 256);
    EXPECT_EQ(pieces[2].width, 88);
    EXPECT_FALSE(pieces[0].quad.scalePosition);

    const auto clipped = TextPiecesAtPixels(900, 21, TextExtent{600, 13}, Screen1024);
    ASSERT_EQ(clipped.size(), 1u);
    EXPECT_EQ(clipped[0].quad.x, 900.0f);
    EXPECT_EQ(clipped[0].width, 124);
}

TEST(TextLayoutTest, TheRowsAreTheCellUpTo32)
{
    const auto tall = TextPieces(0, 0, TextExtent{10, 40}, Screen640);

    ASSERT_EQ(tall.size(), 1u);
    EXPECT_EQ(tall[0].rows, 32);
    EXPECT_EQ(tall[0].quad.height, 32.0f);
    EXPECT_EQ(tall[0].quad.dv, 1.0003124475479126f);
}

// One box per 256 columns, the last one with the remainder, each 256 pixels further right: x · 1.6 = 16.0000002 at
// 1024 × 768.
TEST(TextLayoutTest, AWideTextGoesInBoxesOf256)
{
    const auto pieces = TextPieces(10, 0, TextExtent{600, 13}, Screen1024);

    ASSERT_EQ(pieces.size(), 3u);
    EXPECT_EQ(pieces[0].firstColumn, 0);
    EXPECT_EQ(pieces[1].firstColumn, 256);
    EXPECT_EQ(pieces[2].firstColumn, 512);
    EXPECT_EQ(pieces[0].width, 256);
    EXPECT_EQ(pieces[1].width, 256);
    EXPECT_EQ(pieces[2].width, 88);
    EXPECT_EQ(pieces[0].quad.x, 16.0f);
    EXPECT_EQ(pieces[1].quad.x, 272.0f);
    EXPECT_EQ(pieces[2].quad.x, 528.0f);
    EXPECT_EQ(pieces[0].quad.du, 1.0000391006469727f);
    EXPECT_EQ(pieces[2].quad.du, 0.34378907084465027f);

    const auto exact = TextPieces(0, 0, TextExtent{512, 13}, Screen1024);
    ASSERT_EQ(exact.size(), 2u);
    EXPECT_EQ(exact[1].width, 256);
}

// A text of more than 256 columns that passes the right edge is cut at the edge, truncated: at 1024 × 768, x = 400 goes
// at 640.0000095, and 383 columns remain, not 384.
TEST(TextLayoutTest, AWideTextIsCutAtTheRightEdge)
{
    const auto pieces = TextPieces(400, 0, TextExtent{500, 13}, Screen1024);

    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0].width, 256);
    EXPECT_EQ(pieces[1].width, 127);
    EXPECT_EQ(pieces[0].quad.x, 640.0f);
    EXPECT_EQ(pieces[1].quad.x, 896.0f);

    EXPECT_TRUE(TextPieces(640, 0, TextExtent{300, 13}, Screen640).empty());
}

// One of up to 256 is not cut: 0x554000 moves it against the edge. And a negative x (a text 454 wider than 335 points)
// goes to 0.
TEST(TextLayoutTest, AShortTextIsMovedInsideTheScreen)
{
    const auto atTheEdge = TextPieces(600, 0, TextExtent{100, 13}, Screen640);
    ASSERT_EQ(atTheEdge.size(), 1u);
    EXPECT_EQ(atTheEdge[0].width, 100);
    EXPECT_EQ(atTheEdge[0].quad.x, 540.0f);

    const auto negative = TextPieces(-10, 0, TextExtent{50, 13}, Screen640);
    ASSERT_EQ(negative.size(), 1u);
    EXPECT_EQ(negative[0].quad.x, 0.0f);
}

TEST(TextLayoutTest, AnEmptyTextHasNoBoxes)
{
    EXPECT_TRUE(TextPieces(10, 10, TextExtent{0, 13}, Screen640).empty());
}

// The new UI's windows place their texts in pixels and convert them to 640 × 480 for 0x5542f0: fild of the pixel, fdiv
// by rx or ry in float and _ftol (0x641aa1..0x641ad6, CL5.5). At 640 × 480 both are 1. At 1024 × 768, rx is 1.6 in
// float, a bit more than 1.6: 240 gives 149.99999 and becomes 149. At 800 × 600, ry is 1.2500001: 240 gives 191.
TEST(TextLayoutTest, DesignXAndYTruncateThePixelOverTheRatio)
{
    EXPECT_EQ(DesignX(155, Screen640), 155);
    EXPECT_EQ(DesignY(156, Screen640), 156);
    EXPECT_EQ(DesignX(240, Screen1024), 149);
    EXPECT_EQ(DesignX(30, Screen1024), 18);
    EXPECT_EQ(DesignY(240, PixelSize{800, 600}), 191);
    EXPECT_EQ(DesignX(240, PixelSize{800, 600}), 192);
    EXPECT_EQ(DesignY(212, Screen1280), 99);
}

// _ftol truncates towards 0: a negative pixel does not go down to the integer below.
TEST(TextLayoutTest, DesignXTruncatesTowardZero)
{
    EXPECT_EQ(DesignX(-5, Screen1024), -3);
}

// Without a screen (a side at 0, which the original would divide by), the pixel stays as it is, as in RightAlignedAt:
// dividing by a ratio of 0 and converting the infinity to int would be UB.
TEST(TextLayoutTest, DesignXAndYWithoutAScreenAreThePixel)
{
    EXPECT_EQ(DesignX(240, PixelSize{0, 0}), 240);
    EXPECT_EQ(DesignY(240, PixelSize{640, 0}), 240);
    EXPECT_EQ(DesignX(0, PixelSize{0, 480}), 0);
}
// 0x554000 for a bubble line: x against the edges in pixels, and y only with 0x77f3b66, against 0 and against
// H − H · 47 / 640 (445 at 480). With more than 256 columns, du exceeds 1 (Q-C521).
TEST(TextLayoutTest, ABubbleBoxIsKeptInsideTheScreen)
{
    const auto quad = BoxedQuad(10, 20, 60, 13, true, Screen640);
    EXPECT_EQ(quad.slot.value, 0);
    EXPECT_EQ(quad.x, 10.0f);
    EXPECT_EQ(quad.y, 20.0f);
    EXPECT_EQ(quad.width, 60.0f);
    EXPECT_EQ(quad.height, 13.0f);
    EXPECT_EQ(quad.du, 0.23441405594348907f);
    EXPECT_EQ(quad.dv, 0.4065625071525574f);
    EXPECT_FALSE(quad.scaleSize);
    EXPECT_FALSE(quad.scalePosition);

    EXPECT_EQ(BoxedQuad(-5, 0, 60, 13, true, Screen640).x, 0.0f);
    EXPECT_EQ(BoxedQuad(600, 0, 60, 13, true, Screen640).x, 580.0f);
    EXPECT_EQ(BoxedQuad(0, -7, 60, 13, true, Screen640).y, 0.0f);
    EXPECT_EQ(BoxedQuad(0, 440, 60, 13, true, Screen640).y, 432.0f);
    EXPECT_EQ(BoxedQuad(0, -7, 60, 13, false, Screen640).y, -7.0f);
    EXPECT_EQ(BoxedQuad(0, 440, 60, 13, false, Screen640).y, 440.0f);
    EXPECT_GT(BoxedQuad(0, 0, 300, 13, true, Screen640).du, 1.0f);
}

// Mode 2 of 0x467060: the text at column (box − cx) / 2, with C division, and the box at cx + twice that. The
// inventory title box at 640 × 480 (120) with 9 letters of 6 (54) leaves 33 columns on each side; at 1600 × 1200 (300)
// with 55 columns, 245 / 2 gives 122 and the box ends up at 299, one less than requested.
TEST(TextLayoutTest, TheModeTwoCentersTheTextInItsBox)
{
    EXPECT_EQ(CenterInBox(120, 54), (CenteredInBox{.textColumn = 33, .width = 120}));
    EXPECT_EQ(CenterInBox(300, 55), (CenteredInBox{.textColumn = 122, .width = 299}));
    EXPECT_EQ(CenterInBox(192, 192), (CenteredInBox{.textColumn = 0, .width = 192}));
}

// With a text wider than the box the column is negative and truncates towards 0 (cdq and sub before the sar): −5 / 2
// gives −2, and the box ends up one wider than requested. Without a box, the middle column of an odd cx, or none; with
// a negative box, the box gives fewer than 0 columns.
TEST(TextLayoutTest, AWiderTextGoesLeftOfItsBox)
{
    EXPECT_EQ(CenterInBox(10, 15), (CenteredInBox{.textColumn = -2, .width = 11}));
    EXPECT_EQ(CenterInBox(10, 16), (CenteredInBox{.textColumn = -3, .width = 10}));
    EXPECT_EQ(CenterInBox(0, 5), (CenteredInBox{.textColumn = -2, .width = 1}));
    EXPECT_EQ(CenterInBox(0, 4), (CenteredInBox{.textColumn = -2, .width = 0}));
    EXPECT_EQ(CenterInBox(-10, 5), (CenteredInBox{.textColumn = -7, .width = -9}));
}

// With the measured text: an empty one measures the "0" of 0x6c7ae4, and empty and without a box it gives no columns
// (0x467077..0x467082), even though the "0" is odd: with 7 columns, CenterInBox(0, 7) would give one.
TEST(TextLayoutTest, TheModeTwoMeasuresAnEmptyTextAsZero)
{
    const FakeGlyphRasterizer sixWide;
    const FakeGlyphRasterizer sevenWide(7, 13);

    EXPECT_EQ(CenterInBox(sixWide, FontKind::Bold, "ab", 40), (CenteredInBox{.textColumn = 14, .width = 40}));
    EXPECT_EQ(CenterInBox(sixWide, FontKind::Bold, "", 20), (CenteredInBox{.textColumn = 7, .width = 20}));
    EXPECT_EQ(CenterInBox(sevenWide, FontKind::Bold, "", 20), (CenteredInBox{.textColumn = 6, .width = 19}));
    EXPECT_EQ(CenterInBox(sevenWide, FontKind::Bold, "", 0), (CenteredInBox{}));
    EXPECT_EQ(CenterInBox(0, 7), (CenteredInBox{.textColumn = -3, .width = 1}));
}
} // namespace
