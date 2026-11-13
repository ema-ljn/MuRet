#include "UI/SprintfFormat.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string_view>

namespace
{
using Client::UI::Sprintf;

TEST(SprintfFormat, PlacesIntegersAndTextsInOrder)
{
    EXPECT_EQ(Sprintf("Ataque: %d~%d", 12, 30), "Ataque: 12~30");
    EXPECT_EQ(Sprintf("%s %s", std::string_view("Uno"), std::string_view("Dos")), "Uno Dos");
    EXPECT_EQ(Sprintf("%u/%u", std::uint32_t{4000000000u}, std::uint32_t{7}), "4000000000/7");
}

TEST(SprintfFormat, PercentPercentIsOnePercent)
{
    EXPECT_EQ(Sprintf("Sube %d%%", 5), "Sube 5%");
}

TEST(SprintfFormat, SpaceFlagPutsASpaceBeforeAPositiveNumber)
{
    EXPECT_EQ(Sprintf("Tasa:% d", 40), "Tasa: 40");
    EXPECT_EQ(Sprintf("Tasa:% d", -40), "Tasa:-40");
    EXPECT_EQ(Sprintf("Sube +% d %%", 7), "Sube + 7 %");
    EXPECT_EQ(Sprintf("dura% dseg", 3), "dura 3seg");
    // El espacio no cambia un %s.
    EXPECT_EQ(Sprintf("de% s", std::string_view("X")), "deX");
}

TEST(SprintfFormat, ABrokenMarkWritesOnlyTheCharacterThatBrokeIt)
{
    // "25%)": el ')' corta la marca y sale solo; "10% " al final no escribe nada.
    EXPECT_EQ(Sprintf("(+ 25%)"), "(+ 25)");
    EXPECT_EQ(Sprintf("+ 10% "), "+ 10");
}

TEST(SprintfFormat, FlagsWidthAndPrecision)
{
    EXPECT_EQ(Sprintf("[%5d]", 42), "[   42]");
    EXPECT_EQ(Sprintf("[%-5d]", 42), "[42   ]");
    EXPECT_EQ(Sprintf("[%05d]", -42), "[-0042]");
    EXPECT_EQ(Sprintf("[%+d]", 3), "[+3]");
    EXPECT_EQ(Sprintf("[%.3d]", 7), "[007]");
    EXPECT_EQ(Sprintf("[%#x]", 255), "[0xff]");
    EXPECT_EQ(Sprintf("[%X]", 255), "[FF]");
    EXPECT_EQ(Sprintf("[%.2s]", std::string_view("abc")), "[ab]");
    EXPECT_EQ(Sprintf("[%c]", 65), "[A]");
    EXPECT_EQ(Sprintf("[%hd]", 70000), "[4464]");
}

TEST(SprintfFormat, DIsSignedAndUIsUnsigned)
{
    EXPECT_EQ(Sprintf("%d", std::uint32_t{0xffffffffu}), "-1");
    EXPECT_EQ(Sprintf("%u", -1), "4294967295");
}

TEST(SprintfFormat, AMarkWithoutItsArgumentStaysWritten)
{
    EXPECT_EQ(Sprintf("%d y %d", 1), "1 y %d");
    EXPECT_EQ(Sprintf("%s", 5), "%s");
    EXPECT_EQ(Sprintf("%f %d", 5), "%f 5");
}
} // namespace
