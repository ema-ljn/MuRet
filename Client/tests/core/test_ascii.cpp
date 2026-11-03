#include "Core/Ascii.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>
#include <string_view>

// Client::Core::ToLowerAscii y EqualsIgnoringAsciiCase: la comparación sin mayúsculas del cliente,
// byte a byte y solo de la A a la Z (REF-C01 de la auditoría del 2026-11-03).
namespace
{
using Client::Core::EqualsIgnoringAsciiCase;
using Client::Core::ToLowerAscii;
using namespace std::string_view_literals;

constexpr std::string_view Upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
constexpr std::string_view Lower = "abcdefghijklmnopqrstuvwxyz";
constexpr int LowercaseBit = 0x20;

// El byte `value` como char, también los >= 0x80, que con un char con signo son negativos.
char Byte(int value)
{
    return static_cast<char>(static_cast<unsigned char>(value));
}

std::string OneByte(int value)
{
    return std::string(1, Byte(value));
}

// De los 256 bytes, solo los 26 de la A a la Z cambian, cada uno a su minúscula.
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

// Dos vacíos son iguales; un vacío y uno que no lo es, no.
TEST(AsciiTest, TwoEmptyTextsAreEqual)
{
    EXPECT_TRUE(EqualsIgnoringAsciiCase("", ""));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("", "a"));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("a", ""));
}

// Con otro tamaño no son iguales, aunque uno empiece como el otro.
TEST(AsciiTest, DifferentSizesAreNeverEqual)
{
    EXPECT_FALSE(EqualsIgnoringAsciiCase("TerrainLight", "TerrainLigh"));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("terrainligh", "TERRAINLIGHT"));
}

// Las letras, de punta a punta del abecedario, se comparan sin mayúsculas; el resto de cada texto,
// igual que siempre.
TEST(AsciiTest, TheLettersIgnoreTheCase)
{
    EXPECT_TRUE(EqualsIgnoringAsciiCase("A", "a"));
    EXPECT_TRUE(EqualsIgnoringAsciiCase("z", "Z"));
    EXPECT_TRUE(EqualsIgnoringAsciiCase(Upper, Lower));
    EXPECT_TRUE(EqualsIgnoringAsciiCase("TerrainLight", "terrainlight"));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("TerrainLight", "TerrainNight"));
}

// Los vecinos de la A a la Z ('@' antes y de '[' a '_' después) difieren de '`' y de '{' a DEL en
// el mismo bit que una mayúscula de su minúscula, pero no son letras: no se juntan.
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

// Los bytes >= 0x80 (UTF-8, o la página de códigos de Windows) se comparan tal cual: 0xC1 (la Á de
// Latin-1) no es 0xE1 (la á).
TEST(AsciiTest, BytesFrom0x80AreComparedAsTheyAre)
{
    for (int value = 0x80; value <= 0xFF; ++value)
    {
        EXPECT_TRUE(EqualsIgnoringAsciiCase(OneByte(value), OneByte(value))) << "byte " << value;
        EXPECT_FALSE(EqualsIgnoringAsciiCase(OneByte(value), OneByte(value ^ LowercaseBit))) << "byte " << value;
    }
}

// Un '\0' adentro es un byte más y no el final: se compara también lo que sigue.
TEST(AsciiTest, AnEmbeddedNulIsOneMoreByte)
{
    EXPECT_TRUE(EqualsIgnoringAsciiCase("a\0B"sv, "A\0b"sv));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("a\0b"sv, "a\0c"sv));
    EXPECT_FALSE(EqualsIgnoringAsciiCase("a\0"sv, "a"sv));
}
} // namespace
