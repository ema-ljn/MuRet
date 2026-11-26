#include "Render/Text/TextBox.h"

#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"

#include "support/FakeGlyphRasterizer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 0x465a00, which copies the DIB's ink into the 256 × 32 RGBA box with the text and background colors (06 § Texto).
// The fake rasterizer puts the ink: a 3 × 4 block at the top left of each 6-column letter.
namespace
{
using Client::Render::Text::BoxShape;
using Client::Render::Text::FontKind;
using Client::Render::Text::PaintBoxedText;
using Client::Render::Text::PaintTextBox;
using Client::Render::Text::StoreColor;
using Client::Render::Text::TextBoxBytes;
using Client::Render::Text::TextBoxWidth;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;
using Client::Tests::FakeGlyphRasterizer;

constexpr TextStyle Style{FontKind::Normal, TextColors{0xff3322aa, 0x80000000}};

using Rgba = std::array<std::uint8_t, 4>;

Rgba PixelAt(const std::vector<std::uint8_t>& box, int column, int row)
{
    const auto offset = static_cast<std::size_t>((row * TextBoxWidth + column) * 4);
    return {box[offset], box[offset + 1], box[offset + 2], box[offset + 3]};
}

std::vector<std::uint8_t> Paint(std::string_view text, int firstColumn, int width, int rows)
{
    const FakeGlyphRasterizer rasterizer;
    std::vector<std::uint8_t> box(TextBoxBytes, 0x5a);
    PaintTextBox(rasterizer, Style, text, firstColumn, width, rows, std::span<std::uint8_t, TextBoxBytes>(box));
    return box;
}

// 0xAABBGGRR: the low byte is red, as glTexImage2D reads the dword with GL_RGBA.
TEST(TextBoxTest, AColorIsRedGreenBlueAlpha)
{
    std::array<std::uint8_t, 4> pixel{};

    StoreColor(0xff3322aa, pixel);

    EXPECT_EQ(pixel, (Rgba{0xaa, 0x22, 0x33, 0xff}));
}

TEST(TextBoxTest, InkIsTextAndTheRestIsBackground)
{
    const auto box = Paint("ab", 0, 12, 13);

    constexpr Rgba Text{0xaa, 0x22, 0x33, 0xff};
    constexpr Rgba Background{0, 0, 0, 0x80};
    EXPECT_EQ(PixelAt(box, 0, 0), Text);
    EXPECT_EQ(PixelAt(box, 2, 3), Text);
    EXPECT_EQ(PixelAt(box, 3, 0), Background);
    EXPECT_EQ(PixelAt(box, 0, 4), Background);
    EXPECT_EQ(PixelAt(box, 6, 0), Text);
    EXPECT_EQ(PixelAt(box, 11, 12), Background);
}

// Outside the visible columns and rows the box stays at 0, even if the caller passes it with something else.
TEST(TextBoxTest, OutsideWhatIsSeenTheBoxIsEmpty)
{
    const auto box = Paint("abcd", 0, 12, 13);

    EXPECT_EQ(PixelAt(box, 12, 0), (Rgba{0, 0, 0, 0}));
    EXPECT_EQ(PixelAt(box, 18, 0), (Rgba{0, 0, 0, 0}));
    EXPECT_EQ(PixelAt(box, 0, 13), (Rgba{0, 0, 0, 0}));
    EXPECT_EQ(PixelAt(box, 255, 31), (Rgba{0, 0, 0, 0}));
}

// Box k of a wide text starts at column 256 k: letter 43 starts at 258, which is column 2 of the second box, and the
// ink of 42 (from 252 to 254) stays in the first.
TEST(TextBoxTest, ABoxStartsAtItsFirstColumn)
{
    const std::string text(50, 'x');
    const auto box = Paint(text, 256, 44, 13);

    constexpr Rgba Text{0xaa, 0x22, 0x33, 0xff};
    constexpr Rgba Background{0, 0, 0, 0x80};
    EXPECT_EQ(PixelAt(box, 0, 0), Background);
    EXPECT_EQ(PixelAt(box, 1, 0), Background);
    EXPECT_EQ(PixelAt(box, 2, 0), Text);
    EXPECT_EQ(PixelAt(box, 4, 0), Text);
    EXPECT_EQ(PixelAt(box, 5, 0), Background);
}

// 0x467060 does not pass TextOutA a text that starts with a newline (cmp byte, 0x0a at 0x4671e1): the visible box is
// all background, without letters.
TEST(TextBoxTest, ATextThatStartsWithANewLineIsOnlyItsBackground)
{
    const auto box = Paint("\nab", 0, 18, 13);

    constexpr Rgba Background{0, 0, 0, 0x80};
    for (int row = 0; row < 13; ++row)
    {
        for (int column = 0; column < 18; ++column)
        {
            EXPECT_EQ(PixelAt(box, column, row), Background) << column << ", " << row;
        }
    }
    EXPECT_EQ(PixelAt(box, 18, 0), (Rgba{0, 0, 0, 0}));
    // A newline in the middle changes nothing: it is drawn like any byte.
    EXPECT_EQ(PixelAt(Paint("a\nb", 0, 18, 13), 0, 0), (Rgba{0xaa, 0x22, 0x33, 0xff}));
}

// Only 0xff is a letter, as in 0x465a00 (cmp byte, 0xff): a gray from an antialiasing rasterizer is background.
TEST(TextBoxTest, OnlyFullInkIsText)
{
    class GrayRasterizer final : public Client::Render::Text::IGlyphRasterizer
    {
    public:
        [[nodiscard]] Client::Render::Text::TextExtent Measure(FontKind, std::string_view) const override
        {
            return {2, 1};
        }
        void DrawInk(FontKind, std::string_view, int, Client::Render::Text::InkMask ink) const override
        {
            ink[0] = 0xfe;
            ink[1] = 0xff;
        }
    };
    const GrayRasterizer rasterizer;
    std::vector<std::uint8_t> box(TextBoxBytes, 0);

    PaintTextBox(rasterizer, Style, "ab", 0, 2, 1, std::span<std::uint8_t, TextBoxBytes>(box));

    EXPECT_EQ(PixelAt(box, 0, 0), (Rgba{0, 0, 0, 0x80}));
    EXPECT_EQ(PixelAt(box, 1, 0), (Rgba{0xaa, 0x22, 0x33, 0xff}));
}

// 0x553e50: the ink before the leading columns goes in their color and the rest in the text color; the rest of the box,
// the bubble width over the step's rows, with the background, and 0 outside.
TEST(TextBoxTest, ABubbleBoxHasItsPrefixColumns)
{
    const FakeGlyphRasterizer rasterizer;
    std::vector<std::uint8_t> box(TextBoxBytes, 0x5a);

    PaintBoxedText(rasterizer, Style, "ab",
                   BoxShape{.width = 20, .rows = 12, .prefixColumns = 6, .prefixColor = 0xff00ffc8},
                   std::span<std::uint8_t, TextBoxBytes>(box));

    constexpr Rgba Prefix{0xc8, 0xff, 0x00, 0xff};
    constexpr Rgba Text{0xaa, 0x22, 0x33, 0xff};
    constexpr Rgba Background{0, 0, 0, 0x80};
    EXPECT_EQ(PixelAt(box, 0, 0), Prefix);
    EXPECT_EQ(PixelAt(box, 2, 3), Prefix);
    EXPECT_EQ(PixelAt(box, 6, 0), Text);
    EXPECT_EQ(PixelAt(box, 3, 0), Background);
    EXPECT_EQ(PixelAt(box, 19, 11), Background);
    EXPECT_EQ(PixelAt(box, 20, 0), (Rgba{0, 0, 0, 0}));
    EXPECT_EQ(PixelAt(box, 0, 12), (Rgba{0, 0, 0, 0}));
}

// No text, the background alone; with more than 256 columns (Q-C521), the box's 256, and the rows up to 32.
TEST(TextBoxTest, AnEmptyOrWideBubbleBoxIsItsBackground)
{
    const FakeGlyphRasterizer rasterizer;
    std::vector<std::uint8_t> box(TextBoxBytes, 0x5a);

    PaintBoxedText(rasterizer, Style, "", BoxShape{.width = 300, .rows = 40, .prefixColumns = 0, .prefixColor = 0},
                   std::span<std::uint8_t, TextBoxBytes>(box));

    constexpr Rgba Background{0, 0, 0, 0x80};
    EXPECT_EQ(PixelAt(box, 0, 0), Background);
    EXPECT_EQ(PixelAt(box, 255, 31), Background);
    EXPECT_EQ(rasterizer.drawCalls, 0);
}
} // namespace
