#include "Ui/GameText.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

// Client::Ui::PlaceArguments: a Text.bmd text with its "%s" and "%d" replaced by the arguments, without using it as
// a format (Q-C61, like CL5.4's PlaceVersion). The account title uses it (texts 460 and 461, with the group and the
// number; CL5.5). And Client::Ui::SplitIntoLines, 0x632c90's lines: the list's description, in two of 83 bytes.
namespace
{
using Client::Ui::PlaceArguments;
using Client::Ui::SplitIntoLines;
using Lines = std::vector<std::string>;

TEST(GameTextTest, PlacesEachMarkInOrder)
{
    constexpr std::array<std::string_view, 2> Arguments{"x", "7"};

    EXPECT_EQ(PlaceArguments("A %s b %d c", Arguments), "A x b 7 c");
}

// An extra argument goes nowhere.
TEST(GameTextTest, AMarkWithoutArgumentStays)
{
    constexpr std::array<std::string_view, 1> One{"x"};
    constexpr std::array<std::string_view, 3> Three{"x", "y", "z"};

    EXPECT_EQ(PlaceArguments("%s %d", One), "x %d");
    EXPECT_EQ(PlaceArguments("%s", Three), "x");
}

// The HUD's 201, "Exp : %u/%u".
TEST(GameTextTest, TheUnsignedMarkTakesAnArgument)
{
    constexpr std::array<std::string_view, 2> Two{"1", "2"};

    EXPECT_EQ(PlaceArguments("Exp : %u/%u", Two), "Exp : 1/2");
}

// The 380's countdown (0x647ef0), "...en% d segundos.": with the space flag, a number without a sign gets a space in
// front, and a negative one its sign. Made-up text.
TEST(GameTextTest, TheSpaceFlagPadsAnUnsignedNumber)
{
    constexpr std::array<std::string_view, 1> Five{"5"};
    constexpr std::array<std::string_view, 1> MinusOne{"-1"};

    EXPECT_EQ(PlaceArguments("faltan% d pasos", Five), "faltan 5 pasos");
    EXPECT_EQ(PlaceArguments("faltan% d pasos", MinusOne), "faltan-1 pasos");
}

// Anything other than "%s", "%d" or "%u" stays: the text is not a format, and a "%n" writes nothing.
TEST(GameTextTest, TheTextIsNotAFormat)
{
    constexpr std::array<std::string_view, 1> One{"y"};

    EXPECT_EQ(PlaceArguments("50% %n%s %x %", One), "50% %ny %x %");
}

// An argument containing a mark is not read again.
TEST(GameTextTest, AnArgumentIsNotReadAgain)
{
    constexpr std::array<std::string_view, 2> Arguments{"%s", "b"};

    EXPECT_EQ(PlaceArguments("%s %s", Arguments), "%s b");
}

// An empty one is an empty line (0x632c90 returns 1).
TEST(SplitIntoLinesTest, AShortTextIsOneLine)
{
    EXPECT_EQ(SplitIntoLines("abc", 2, 83), (Lines{"abc"}));
    EXPECT_EQ(SplitIntoLines("", 2, 83), (Lines{""}));
}

// Because of the '\0': with 83, 82 fit.
TEST(SplitIntoLinesTest, ALineHoldsOneByteLessThanItsSize)
{
    const std::string fits(82, 'a');
    EXPECT_EQ(SplitIntoLines(fits, 2, 83), (Lines{fits}));
    EXPECT_EQ(SplitIntoLines(fits + "b", 2, 83), (Lines{fits, "b"}));
}

// With a space less than 10 bytes before the character that does not fit, it cuts after the space, which stays at the
// end of the line, and goes on from there.
TEST(SplitIntoLinesTest, CutsAfterASpaceWithinTen)
{
    EXPECT_EQ(SplitIntoLines("aaaaaaaaaaaaaaa bbbbbbbbb", 2, 20), (Lines{"aaaaaaaaaaaaaaa ", "bbbbbbbbb"}));
}

// Right before the character that does not fit.
TEST(SplitIntoLinesTest, CutsInTheWordWhenTheSpaceIsTenOrMoreBack)
{
    EXPECT_EQ(SplitIntoLines("aaaaaaaaa bbbbbbbbbbbbbbbbb", 2, 20), (Lines{"aaaaaaaaa bbbbbbbbb", "bbbbbbbb"}));
}

// The back-off limit is the smaller of half the line and 10: with 8, 4.
TEST(SplitIntoLinesTest, TheLimitIsHalfTheLineWhenShorter)
{
    EXPECT_EQ(SplitIntoLines("ab cdefghij", 2, 8), (Lines{"ab cdef", "ghij"}));
    EXPECT_EQ(SplitIntoLines("abcd efghij", 2, 8), (Lines{"abcd ", "efghij"}));
}

// The last line is also cut at its space.
TEST(SplitIntoLinesTest, WhatDoesNotFitInTheLastLineIsDropped)
{
    EXPECT_EQ(SplitIntoLines("aaaabbbbcccc", 2, 5), (Lines{"aaaa", "bbbb"}));
    EXPECT_EQ(SplitIntoLines("aaaaaa bbbbb", 1, 10), (Lines{"aaaaaa "}));
}

// The space that did not fit in the line before.
TEST(SplitIntoLinesTest, ANewLineCanStartWithTheSpace)
{
    EXPECT_EQ(SplitIntoLines("abcde fgh", 2, 6), (Lines{"abcde", " fgh"}));
}

TEST(SplitIntoLinesTest, StopsAtTheFirstNul)
{
    constexpr std::string_view WithNul{"ab\0cd", 5};
    EXPECT_EQ(SplitIntoLines(WithNul, 2, 83), (Lines{"ab"}));
}
} // namespace
