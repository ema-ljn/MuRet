#include "Render/Text/InlineColors.h"

#include "Render/Text/TextStyle.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace std::string_view_literals;

// The 0x02 color changes of 0x4667a0 and the table of 0x4665d0. The texts are the five of step 2 of the CL5 plan
// (CL5.3): no 0x02, one at the start, two in a row, one at the end and ten.
namespace
{
using Client::Render::Text::ColorRun;
using Client::Render::Text::InlineColor;
using Client::Render::Text::InlineColorContext;
using Client::Render::Text::MaxInlineColors;
using Client::Render::Text::ParseInlineColors;
using Client::Render::Text::TextColors;

// The login colors (0x6350b0): white text and black background with alpha 0x80.
constexpr TextColors Scene{0xffffffff, 0x80000000};
constexpr InlineColorContext Context{Scene};

constexpr TextColors Red{0xff0000ff, 0};
constexpr TextColors White{0xffffffff, 0};

// A 0x02 with its code, so that a following letter from a to f does not enter the code's \x.
std::string Pair(std::uint8_t code)
{
    return std::string{Client::Render::Text::InlineColorMarker, static_cast<char>(code)};
}

TEST(InlineColorsTest, ATextWithoutMarkersStaysAsItIs)
{
    const auto parsed = ParseInlineColors("abc", Context);

    EXPECT_EQ(parsed.text, "abc");
    EXPECT_TRUE(parsed.runs.empty());
}

TEST(InlineColorsTest, StripsTheMarkerAndItsCode)
{
    EXPECT_EQ(ParseInlineColors(Pair(0xf4) + "abc", Context).text, "abc");
    EXPECT_EQ(ParseInlineColors("ab" + Pair(0xf4) + "cd", Context).text, "abcd");
    EXPECT_EQ(ParseInlineColors("ab" + Pair(0xf0) + Pair(0xf1) + "cd", Context).text, "abcd");
}

// The span starts in the clean text: the position of the 0x02 minus two for each earlier pair. Two in a row start at
// the same place, and the second one counts.
TEST(InlineColorsTest, RecordsWhereEachColorStarts)
{
    const auto atTheStart = ParseInlineColors(Pair(0xf4) + "abc", Context);
    EXPECT_EQ(atTheStart.runs, (std::vector<ColorRun>{{0, 0xf4, Red}}));

    const auto twoInARow = ParseInlineColors("ab" + Pair(0xf0) + Pair(0xf1) + "cd", Context);
    EXPECT_EQ(twoInARow.runs,
              (std::vector<ColorRun>{{2, 0xf0, Scene}, {2, 0xf1, TextColors{Scene.background, Scene.text}}}));

    const auto apart = ParseInlineColors("a" + Pair(0xf2) + "bc" + Pair(0xf3) + "d", Context);
    ASSERT_EQ(apart.runs.size(), 2u);
    EXPECT_EQ(apart.runs[0].start, 1u);
    EXPECT_EQ(apart.runs[1].start, 3u);
    EXPECT_EQ(apart.text, "abcd");
}

// 0x4665d0: 0x01 and 0xf4 red, 0xf2 to 0xf8 those of its table without background, and any other white without
// background (also 0, 0x02 and those above 0xf8).
TEST(InlineColorsTest, TheCodeIsAColorOfTheTable)
{
    EXPECT_EQ(InlineColor(0x01, Context), Red);
    EXPECT_EQ(InlineColor(0xf4, Context), Red);
    EXPECT_EQ(InlineColor(0xf2, Context), (TextColors{0xffffc8dc, 0}));
    EXPECT_EQ(InlineColor(0xf3, Context), (TextColors{0xff00ffc8, 0}));
    EXPECT_EQ(InlineColor(0xf5, Context), (TextColors{0xff503214, 0}));
    EXPECT_EQ(InlineColor(0xf6, Context), (TextColors{0xff00643c, 0}));
    EXPECT_EQ(InlineColor(0xf7, Context), (TextColors{0xff000064, 0}));
    EXPECT_EQ(InlineColor(0xf8, Context), (TextColors{0xffc8dce6, 0}));

    constexpr std::array<std::uint8_t, 9> Others = {0x00, 0x02, 0x03, 0x41, 0x80, 0xef, 0xf9, 0xfe, 0xff};
    for (const std::uint8_t other : Others)
    {
        EXPECT_EQ(InlineColor(other, Context), White) << static_cast<int>(other);
    }
}

// 0xf0 keeps both scene colors (DAT_006c8e5c and DAT_006c8e64) and 0xf1 swaps them: the text with the background
// color and the background with the text's.
TEST(InlineColorsTest, F0AndF1SwapTheSceneColors)
{
    const InlineColorContext context{TextColors{0x11223344, 0x55667788}};

    EXPECT_EQ(InlineColor(0xf0, context), (TextColors{0x11223344, 0x55667788}));
    EXPECT_EQ(InlineColor(0xf1, context), (TextColors{0x55667788, 0x11223344}));
}

// With DAT_006c7a88 at 0, 0xf8 has a background: black with alpha 0x96.
TEST(InlineColorsTest, F8HasABackgroundWhenTheFlagIsOff)
{
    const InlineColorContext withBackground{Scene, false};

    EXPECT_EQ(InlineColor(0xf8, withBackground), (TextColors{0xffc8dce6, 0x96000000}));
    EXPECT_EQ(InlineColor(0xf4, withBackground), Red);
}

// Nine spans and no more: the tenth pair leaves the text all the same, without a span, so the "j" keeps the ninth's
// color.
TEST(InlineColorsTest, KeepsAtMostNineColors)
{
    constexpr std::array<std::uint8_t, 10> Codes = {0x01, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf0, 0xf1};
    std::string text;
    for (std::size_t i = 0; i < Codes.size(); ++i)
    {
        text += Pair(Codes[i]);
        text += static_cast<char>('a' + i);
    }

    const auto parsed = ParseInlineColors(text, Context);

    EXPECT_EQ(parsed.text, "abcdefghij");
    ASSERT_EQ(parsed.runs.size(), MaxInlineColors);
    for (std::size_t i = 0; i < parsed.runs.size(); ++i)
    {
        EXPECT_EQ(parsed.runs[i].start, i);
    }
    EXPECT_EQ(parsed.runs.back().code, 0xf0);
    EXPECT_EQ(parsed.runs.back().colors, Scene);
}

// A 0x02 at the end takes the 0 that ends the text as its code: a white span without background that starts where the
// clean text ends, and paints nothing.
TEST(InlineColorsTest, AMarkerAtTheEndHasNoCode)
{
    const auto parsed = ParseInlineColors("ab\x02", Context);

    EXPECT_EQ(parsed.text, "ab");
    EXPECT_EQ(parsed.runs, (std::vector<ColorRun>{{2, 0x00, White}}));
}

// The code is the next byte, even if it is another 0x02: "\x02\x02" is a pair, not two markers.
TEST(InlineColorsTest, TheByteAfterTheMarkerIsAlwaysTheCode)
{
    const auto parsed = ParseInlineColors("a" + Pair(0x02) + "b", Context);

    EXPECT_EQ(parsed.text, "ab");
    EXPECT_EQ(parsed.runs, (std::vector<ColorRun>{{1, 0x02, White}}));
}

// Like strlen: what follows the first 0 is not part of the text.
TEST(InlineColorsTest, StopsAtTheFirstNul)
{
    const auto parsed = ParseInlineColors(std::string("ab\0"sv) + Pair(0xf4) + "c", Context);

    EXPECT_EQ(parsed.text, "ab");
    EXPECT_TRUE(parsed.runs.empty());
}

// 0x4667a0 copies the text to a 256-byte buffer on its stack and rewrites it from there into the caller's: for a longer
// text, what goes past 256 comes off the stack (Q-C77). MuRet builds the clean text separately, without a cap, and
// does not touch the caller's.
TEST(InlineColorsTest, ALongTextKeepsItsTail)
{
    std::string tail;
    for (int i = 0; i < 300; ++i)
    {
        tail.push_back(static_cast<char>('a' + (i % 26)));
    }
    const std::string text = Pair(0xf4) + tail;

    const auto parsed = ParseInlineColors(text, Context);

    EXPECT_EQ(parsed.text, tail);
    EXPECT_EQ(parsed.runs, (std::vector<ColorRun>{{0, 0xf4, Red}}));
    EXPECT_EQ(text, Pair(0xf4) + tail);
}
} // namespace
