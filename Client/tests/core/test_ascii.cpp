#include "Core/Ascii.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace
{
using Client::Core::EqualsIgnoringAsciiCase;
using Client::Core::ToLowerAscii;
using namespace std::string_view_literals;

constexpr std::string_view Upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr std::string_view Lower = "abcdefghijklmnopqrstuvwxyz";
constexpr int LowercaseBit = 0x20;

// Bytes >= 0x80 too, which are negative with a signed char.
char Byte(int value)
{
    return static_cast<char>(static_cast<unsigned char>(value));
}

std::string OneByte(int value)
{
    return std::string(1, Byte(value));
}

TEST(AsciiTest, OnlyTheUppercaseLettersChange)
{
    for (int value = 0; value <= 0xFF; ++value)
    {
        const char byte = Byte(value);
        const std::size_t letter = Upper.find(byte);
        const char expected = letter == std::string_view::npos ? byte : Lower[letter];
        EXPECT_EQ(ToLowerAscii(byte), expected) << "byte " << value;
    }
}

TEST(AsciiTest, TwoEmptyTextsAreEqual)
{
    EXPECT_TRUE(EqualsIgnoringAsciiCase("", ""));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("", "a"));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("a", ""));
}

TEST(AsciiTest, DifferentSizesAreNeverEqual)
{
    EXPECT_FALSE(EqualsIgnoringAsciiCase("TerrainLight", "TerrainLigh"));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("terrainligh", "TERRAINLIGHT"));
}

TEST(AsciiTest, TheLettersIgnoreTheCase)
{
    EXPECT_TRUE(EqualsIgnoringAsciiCase("A", "a"));
    EXPECT_TRUE(EqualsIgnoringAsciiCase("z", "Z"));
    EXPECT_TRUE(EqualsIgnoringAsciiCase(Upper, Lower));
    EXPECT_TRUE(EqualsIgnoringAsciiCase("TerrainLight", "terrainlight"));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("TerrainLight", "TerrainNight"));
}

// The neighbours of A to Z ('@' before, '[' to '_' after) differ from '`' and '{' to DEL in the same bit as an
// uppercase from its lowercase, but they are not letters: they do not fold.
TEST(AsciiTest, TheNeighboursOfTheLettersDoNotFold)
{
    EXPECT_FALSE(EqualsIgnoringAsciiCase("@", "`"));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("[", "{"));
    for (const char neighbour : "@[\\]^_"sv)
    {
        const int value = static_cast<unsigned char>(neighbour);
        EXPECT_FALSE(EqualsIgnoringAsciiCase(OneByte(value), OneByte(value + LowercaseBit))) << neighbour;
    }
}

// 0xC1 (Latin-1's Á) is not 0xE1 (á).
TEST(AsciiTest, BytesFrom0x80AreComparedAsTheyAre)
{
    for (int value = 0x80; value <= 0xFF; ++value)
    {
        EXPECT_TRUE(EqualsIgnoringAsciiCase(OneByte(value), OneByte(value))) << "byte " << value;
        EXPECT_FALSE(EqualsIgnoringAsciiCase(OneByte(value), OneByte(value ^ LowercaseBit))) << "byte " << value;
    }
}

TEST(AsciiTest, AnEmbeddedNulIsOneMoreByte)
{
    EXPECT_TRUE(EqualsIgnoringAsciiCase("a\0B"sv, "A\0b"sv));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("a\0b"sv, "a\0c"sv));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("a\0"sv, "a"sv));
}
} // namespace
