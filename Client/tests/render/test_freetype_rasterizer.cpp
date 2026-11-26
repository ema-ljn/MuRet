#include "Render/Text/FreeTypeRasterizer.h"

#include "Core/PathText.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"
#include "Render/Text/TextStyle.h"

#include "support/GoldenDir.h"
#include "support/TemporaryFolder.h"
#include "support/TinyTrueTypeFont.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// WinMain's fonts with FreeType at one bit (question 7 of the CL5 plan). The tests with a font read
// MU_CLIENT_FONT_FILE (in CI, a font of the runner) and are skipped without the variable; they are compared with
// themselves, not with GDI (Q-C76). The ones that open a file that is not a font always run.
namespace
{
using Client::Render::Text::FontHeightFor;
using Client::Render::Text::FontKind;
using Client::Render::Text::FreeTypeRasterizer;
using Client::Render::Text::PaintTextBox;
using Client::Render::Text::TextBoxBytes;
using Client::Render::Text::TextBoxPixelCount;
using Client::Render::Text::TextBoxWidth;
using Client::Render::Text::TextColors;
using Client::Render::Text::TextStyle;

constexpr std::array AllFonts = {FontKind::Normal, FontKind::Bold, FontKind::BigBold, FontKind::OtherFace12};

std::vector<std::uint8_t> InkOf(const FreeTypeRasterizer& rasterizer, FontKind font, std::string_view text)
{
    std::vector<std::uint8_t> ink(TextBoxPixelCount, 0);
    rasterizer.DrawInk(font, text, 0, std::span<std::uint8_t, TextBoxPixelCount>(ink));
    return ink;
}

std::size_t InkCount(const std::vector<std::uint8_t>& ink)
{
    return static_cast<std::size_t>(std::ranges::count(ink, std::uint8_t{0xff}));
}

class FreeTypeRasterizerTest : public Client::Tests::FontFileTest
{
protected:
    std::unique_ptr<FreeTypeRasterizer> OpenFont(int screenWidth,
                                                 const std::optional<std::filesystem::path>& bold = std::nullopt)
    {
        auto opened = FreeTypeRasterizer::Open(m_fontFile, bold, screenWidth);
        if (!opened.HasValue())
        {
            ADD_FAILURE() << opened.Error();
            return nullptr;
        }
        return std::move(opened).Value();
    }
};

// No antialiasing: each ink pixel is 0 or 0xff, and in the box each pixel is the text or the background color. With
// FT_RENDER_MODE_NORMAL there would be grays.
TEST_F(FreeTypeRasterizerTest, RendersWithoutAntialiasing)
{
    const auto rasterizer = OpenFont(1024);
    ASSERT_NE(rasterizer, nullptr);

    for (const FontKind font : AllFonts)
    {
        const auto ink = InkOf(*rasterizer, font, "Agua sin sal, 0123");
        EXPECT_GT(InkCount(ink), 0u);
        EXPECT_TRUE(std::ranges::all_of(ink, [](std::uint8_t value) { return value == 0 || value == 0xff; }));
    }

    const TextStyle style{FontKind::Normal, TextColors{0xff0000ff, 0x80000000}};
    const auto extent = rasterizer->Measure(style.font, "Agua sin sal");
    std::vector<std::uint8_t> box(TextBoxBytes, 0);
    PaintTextBox(*rasterizer, style, "Agua sin sal", 0, extent.width, extent.height,
                 std::span<std::uint8_t, TextBoxBytes>(box));
    std::size_t text = 0;
    for (int row = 0; row < extent.height; ++row)
    {
        for (int column = 0; column < extent.width; ++column)
        {
            const auto offset = static_cast<std::size_t>((row * TextBoxWidth + column) * 4);
            const std::array<std::uint8_t, 4> pixel{box[offset], box[offset + 1], box[offset + 2], box[offset + 3]};
            const bool isText = pixel == std::array<std::uint8_t, 4>{0xff, 0, 0, 0xff};
            const bool isBackground = pixel == std::array<std::uint8_t, 4>{0, 0, 0, 0x80};
            EXPECT_TRUE(isText || isBackground) << column << ", " << row;
            text += isText ? 1 : 0;
        }
    }
    EXPECT_GT(text, 0u);
}

// Like GetTextExtentPointA, no kerning: a text's width is the sum of the advances.
TEST_F(FreeTypeRasterizerTest, MeasuresTheAdvance)
{
    const auto rasterizer = OpenFont(640);
    ASSERT_NE(rasterizer, nullptr);

    const int a = rasterizer->Measure(FontKind::Normal, "a").width;
    const int b = rasterizer->Measure(FontKind::Normal, "b").width;
    const int v = rasterizer->Measure(FontKind::Normal, "V").width;
    EXPECT_GT(a, 0);
    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "ab").width, a + b);
    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "aaa").width, 3 * a);
    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "AV").width, rasterizer->Measure(FontKind::Normal, "A").width + v);
    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "").width, 0);
}

// The height is that of the cell CreateFontA requests, for each font and each width, also without text.
TEST_F(FreeTypeRasterizerTest, TheCellHeightIsTheFontHeight)
{
    for (const int width : {640, 800, 1024, 1280})
    {
        const auto rasterizer = OpenFont(width);
        ASSERT_NE(rasterizer, nullptr);
        for (const FontKind font : AllFonts)
        {
            EXPECT_EQ(rasterizer->Measure(font, "Ag").height, FontHeightFor(font, width)) << width;
            EXPECT_EQ(rasterizer->Measure(font, "").height, FontHeightFor(font, width)) << width;
        }
    }
}

// The height is the cell's (Windows ascent plus descent), not the em's, which is what FT_Set_Pixel_Sizes would request:
// the em ends up smaller. In DejaVu Sans it is 2048 / 2384 of the cell, and in Arial, 2048 / 2288.
TEST_F(FreeTypeRasterizerTest, TheEmIsSmallerThanTheCell)
{
    for (const int width : {640, 1280})
    {
        const auto rasterizer = OpenFont(width);
        ASSERT_NE(rasterizer, nullptr);
        for (const FontKind font : AllFonts)
        {
            const int height = FontHeightFor(font, width);
            EXPECT_LT(rasterizer->PixelsPerEm(font), height) << width;
            EXPECT_GE(rasterizer->PixelsPerEm(font) * 10, height * 7) << width;
        }
    }
}

// Capitals fall inside the cell, with the cell at the top (TA_TOP): the ink rows go from 0 to the cell height.
TEST_F(FreeTypeRasterizerTest, TheLettersAreInsideTheCell)
{
    const auto rasterizer = OpenFont(1280);
    ASSERT_NE(rasterizer, nullptr);

    for (const FontKind font : AllFonts)
    {
        const auto ink = InkOf(*rasterizer, font, "EHMT");
        const int height = FontHeightFor(font, 1280);
        int lastRow = -1;
        for (std::size_t i = 0; i < ink.size(); ++i)
        {
            if (ink[i] == 0xff)
            {
                lastRow = std::max(lastRow, static_cast<int>(i) / TextBoxWidth);
            }
        }
        EXPECT_GE(lastRow, height / 2);
        EXPECT_LT(lastRow, height);
    }
}

// FT_PIXEL_MODE_MONO: the left pixel of each byte is its high bit, and a row starts `pitch` bytes after the previous
// one. The "L" shows it: the top row is the stem, on the left side of the letter, and the bottom one, the foot, reaches
// further right. With the bits read backwards, or another row stride, the letter comes out mirrored or shifted.
TEST_F(FreeTypeRasterizerTest, TheLeftPixelOfAByteIsItsHighBit)
{
    for (const int width : {640, 1024, 1280})
    {
        const auto rasterizer = OpenFont(width);
        ASSERT_NE(rasterizer, nullptr);
        const auto ink = InkOf(*rasterizer, FontKind::Normal, "L");

        // The first and the last inked column of each row.
        std::vector<std::pair<int, int>> rows;
        int left = TextBoxWidth;
        for (int row = 0; row < 32; ++row)
        {
            int first = -1;
            int last = -1;
            for (int column = 0; column < TextBoxWidth; ++column)
            {
                if (ink[static_cast<std::size_t>(row * TextBoxWidth + column)] == 0xff)
                {
                    first = first < 0 ? column : first;
                    last = column;
                }
            }
            if (first >= 0)
            {
                rows.emplace_back(first, last);
                left = std::min(left, first);
            }
        }
        ASSERT_GE(rows.size(), 3u) << width;
        const auto [topFirst, topLast] = rows.front();
        const auto [bottomFirst, bottomLast] = rows.back();
        EXPECT_EQ(topFirst, left) << width;
        EXPECT_EQ(bottomFirst, left) << width;
        EXPECT_GT(bottomLast, topLast) << width;
    }
}

// A box of a wide text draws from its column: the second box of a 300-column text is the first one's ink shifted 256
// columns.
TEST_F(FreeTypeRasterizerTest, DrawsFromTheFirstColumn)
{
    const auto rasterizer = OpenFont(640);
    ASSERT_NE(rasterizer, nullptr);
    const std::string text(64, 'M');
    ASSERT_GT(rasterizer->Measure(FontKind::Normal, text).width, 260);

    std::vector<std::uint8_t> whole(TextBoxPixelCount, 0);
    std::vector<std::uint8_t> shifted(TextBoxPixelCount, 0);
    rasterizer->DrawInk(FontKind::Normal, text, 0, std::span<std::uint8_t, TextBoxPixelCount>(whole));
    rasterizer->DrawInk(FontKind::Normal, text, 200, std::span<std::uint8_t, TextBoxPixelCount>(shifted));

    for (int row = 0; row < 32; ++row)
    {
        for (int column = 0; column < 56; ++column)
        {
            EXPECT_EQ(shifted[static_cast<std::size_t>(row * TextBoxWidth + column)],
                      whole[static_cast<std::size_t>(row * TextBoxWidth + column + 200)])
                << column << ", " << row;
        }
    }
}

TEST_F(FreeTypeRasterizerTest, WithoutABoldFileTheBoldIsOnePixelWiderPerLetter)
{
    const auto rasterizer = OpenFont(640);
    ASSERT_NE(rasterizer, nullptr);

    EXPECT_EQ(rasterizer->Measure(FontKind::Bold, "abc").width, rasterizer->Measure(FontKind::Normal, "abc").width + 3);
    EXPECT_GT(InkCount(InkOf(*rasterizer, FontKind::Bold, "abc")),
              InkCount(InkOf(*rasterizer, FontKind::Normal, "abc")));
}

// With BoldFontFile the bold is that face, not widened: with the same font in both keys, the bold measures and draws
// like the normal one.
TEST_F(FreeTypeRasterizerTest, WithABoldFileThereIsNoSyntheticBold)
{
    const auto rasterizer = OpenFont(640, m_fontFile);
    ASSERT_NE(rasterizer, nullptr);

    EXPECT_EQ(rasterizer->Measure(FontKind::Bold, "abc"), rasterizer->Measure(FontKind::Normal, "abc"));
    EXPECT_EQ(InkOf(*rasterizer, FontKind::Bold, "abc"), InkOf(*rasterizer, FontKind::Normal, "abc"));
}

// Byte E9 belongs to Latin-1 and CP1252; both represent it as e with an acute accent.
TEST_F(FreeTypeRasterizerTest, ReadsWesternAccentedByte)
{
    const auto rasterizer = OpenFont(1024);
    ASSERT_NE(rasterizer, nullptr);

    EXPECT_EQ(rasterizer->Measure(FontKind::Normal, "\xE9"), rasterizer->Measure(FontKind::Normal, "e"));
    EXPECT_NE(InkOf(*rasterizer, FontKind::Normal, "\xE9"), InkOf(*rasterizer, FontKind::Normal, "e"));
    EXPECT_GT(InkCount(InkOf(*rasterizer, FontKind::Normal, "\xE9")),
              InkCount(InkOf(*rasterizer, FontKind::Normal, "e")));
}

TEST(FreeTypeRasterizerOpenTest, DecodesEuroByteBeforeLoadingTheGlyph)
{
    const Client::Tests::TemporaryFolder folder("freetype");
    const std::filesystem::path font = folder.Path() / "euro.ttf";
    {
        const std::vector<std::uint8_t> bytes = Client::Tests::TinyTrueTypeFont(0x20ac);
        std::ofstream file(font, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    const auto opened = FreeTypeRasterizer::Open(font, std::nullopt, 640);

    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    const FreeTypeRasterizer& rasterizer = *opened.Value();
    const std::string euroByte(1, static_cast<char>(0x80));
    const std::string undefinedByte(1, static_cast<char>(0x81));
    const std::vector<std::uint8_t> euroInk = InkOf(rasterizer, FontKind::Normal, euroByte);
    const std::vector<std::uint8_t> underscoreInk = InkOf(rasterizer, FontKind::Normal, "_");
    const std::vector<std::uint8_t> undefinedInk = InkOf(rasterizer, FontKind::Normal, undefinedByte);
    // Compares against glyphs of the same cmap: mono hinting can add pixels to thin outlines.
    EXPECT_EQ(euroInk, underscoreInk);
    EXPECT_NE(euroInk, undefinedInk);
}

// The v35 interpreter, the one of Windows 98, and not the v40 FreeType uses by default.
TEST_F(FreeTypeRasterizerTest, UsesTheInterpreterOfWindows98)
{
    const auto rasterizer = OpenFont(640);
    ASSERT_NE(rasterizer, nullptr);

    EXPECT_EQ(rasterizer->InterpreterVersion(), 35u);
}

TEST_F(FreeTypeRasterizerTest, AMissingBoldFileSaysItsPath)
{
    const Client::Tests::TemporaryFolder folder("freetype");
    const std::filesystem::path missing = folder.Path() / "negrita.ttf";

    const auto opened = FreeTypeRasterizer::Open(m_fontFile, missing, 640);

    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find(Client::Core::PathText(missing)), std::string::npos) << opened.Error();
}

// The error names the file, whether it is missing or not a font.
TEST(FreeTypeRasterizerOpenTest, AMissingFileSaysItsPath)
{
    const Client::Tests::TemporaryFolder folder("freetype");
    const std::filesystem::path missing = folder.Path() / "no-esta.ttf";

    const auto opened = FreeTypeRasterizer::Open(missing, std::nullopt, 640);

    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find(Client::Core::PathText(missing)), std::string::npos) << opened.Error();
}

TEST(FreeTypeRasterizerOpenTest, AFileThatIsNotAFontSaysItsPath)
{
    const Client::Tests::TemporaryFolder folder("freetype");
    const std::filesystem::path notAFont = folder.Path() / "letra.ttf";
    {
        std::ofstream file(notAFont, std::ios::binary);
        file << "esto no es una fuente";
    }

    const auto opened = FreeTypeRasterizer::Open(notAFont, std::nullopt, 640);

    ASSERT_FALSE(opened.HasValue());
    EXPECT_NE(opened.Error().find(Client::Core::PathText(notAFont)), std::string::npos) << opened.Error();
}

// A letter without area, like the underscore that hinting leaves as a line in some fonts: FT_Outline_EmboldenXY cannot
// widen it (FT_ORIENTATION_NONE) and returns an error, which must not make Open fail. The simulated bold leaves it as
// is, with its advance pixel. Uses the minimal font of support/, without OS/2 (the cell comes from hhea): runs without
// the variable.
TEST(FreeTypeRasterizerOpenTest, ALetterWithoutAreaIsBoldToo)
{
    const Client::Tests::TemporaryFolder folder("freetype");
    const std::filesystem::path font = folder.Path() / "minima.ttf";
    {
        const std::vector<std::uint8_t> bytes = Client::Tests::TinyTrueTypeFont();
        std::ofstream file(font, std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    const auto opened = FreeTypeRasterizer::Open(font, std::nullopt, 640);

    ASSERT_TRUE(opened.HasValue()) << opened.Error();
    const FreeTypeRasterizer& rasterizer = *opened.Value();
    const std::string flat(1, Client::Tests::FlatGlyphLetter);
    EXPECT_EQ(rasterizer.Measure(FontKind::Bold, flat).width, rasterizer.Measure(FontKind::Normal, flat).width + 1);
    EXPECT_EQ(rasterizer.Measure(FontKind::Bold, "a").width, rasterizer.Measure(FontKind::Normal, "a").width + 1);
    EXPECT_EQ(rasterizer.Measure(FontKind::Normal, flat).height, FontHeightFor(FontKind::Normal, 640));
    EXPECT_GT(InkCount(InkOf(rasterizer, FontKind::Bold, "a")), InkCount(InkOf(rasterizer, FontKind::Normal, "a")));
}
} // namespace
