#include "UI/GameText.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

// Client::UI::PlaceArguments: un texto de Text.bmd con sus "%s" y "%d" cambiados por los
// argumentos, sin usarlo como formato (Q-C61, como PlaceVersion de la CL5.4). Lo usa el título de
// la cuenta (los textos 460 y 461, con el grupo y el número; CL5.5). Y Client::UI::SplitIntoLines,
// los renglones de 0x632c90: la descripción de la lista, en dos de 83 bytes.
namespace
{
using Client::UI::PlaceArguments;
using Client::UI::SplitIntoLines;
using Lines = std::vector<std::string>;

TEST(GameTextTest, PlacesEachMarkInOrder)
{
    constexpr std::array<std::string_view, 2> Arguments{"x", "7"};

    EXPECT_EQ(PlaceArguments("A %s b %d c", Arguments), "A x b 7 c");
}

// Una marca sin argumento queda como está, y un argumento de más no va a ningún lado.
TEST(GameTextTest, AMarkWithoutArgumentStays)
{
    constexpr std::array<std::string_view, 1> One{"x"};
    constexpr std::array<std::string_view, 3> Three{"x", "y", "z"};

    EXPECT_EQ(PlaceArguments("%s %d", One), "x %d");
    EXPECT_EQ(PlaceArguments("%s", Three), "x");
}

// Lo que no es "%s" ni "%d" queda: el texto no es un formato, y un "%n" no escribe nada.
TEST(GameTextTest, TheTextIsNotAFormat)
{
    constexpr std::array<std::string_view, 1> One{"y"};

    EXPECT_EQ(PlaceArguments("50% %n%s %x %", One), "50% %ny %x %");
}

// Un argumento con una marca no se vuelve a leer.
TEST(GameTextTest, AnArgumentIsNotReadAgain)
{
    constexpr std::array<std::string_view, 2> Arguments{"%s", "b"};

    EXPECT_EQ(PlaceArguments("%s %s", Arguments), "%s b");
}

// Un texto que entra es un renglón; uno vacío, un renglón vacío (0x632c90 devuelve 1).
TEST(SplitIntoLinesTest, AShortTextIsOneLine)
{
    EXPECT_EQ(SplitIntoLines("abc", 2, 83), (Lines{"abc"}));
    EXPECT_EQ(SplitIntoLines("", 2, 83), (Lines{""}));
}

// Un renglón tiene un byte menos que su lugar, por el '\0': con 83, entran 82.
TEST(SplitIntoLinesTest, ALineHoldsOneByteLessThanItsSize)
{
    const std::string fits(82, 'a');
    EXPECT_EQ(SplitIntoLines(fits, 2, 83), (Lines{fits}));
    EXPECT_EQ(SplitIntoLines(fits + "b", 2, 83), (Lines{fits, "b"}));
}

// Con un espacio a menos de 10 bytes del carácter que no entra, corta después del espacio, que
// queda al final del renglón, y sigue desde ahí.
TEST(SplitIntoLinesTest, CutsAfterASpaceWithinTen)
{
    EXPECT_EQ(SplitIntoLines("aaaaaaaaaaaaaaa bbbbbbbbb", 2, 20), (Lines{"aaaaaaaaaaaaaaa ", "bbbbbbbbb"}));
}

// Con el espacio a 10 o más, corta en la palabra, justo antes del carácter que no entra.
TEST(SplitIntoLinesTest, CutsInTheWordWhenTheSpaceIsTenOrMoreBack)
{
    EXPECT_EQ(SplitIntoLines("aaaaaaaaa bbbbbbbbbbbbbbbbb", 2, 20), (Lines{"aaaaaaaaa bbbbbbbbb", "bbbbbbbb"}));
}

// El tope para volver es el menor de la mitad del renglón y 10: con 8, 4.
TEST(SplitIntoLinesTest, TheLimitIsHalfTheLineWhenShorter)
{
    EXPECT_EQ(SplitIntoLines("ab cdefghij", 2, 8), (Lines{"ab cdef", "ghij"}));
    EXPECT_EQ(SplitIntoLines("abcd efghij", 2, 8), (Lines{"abcd ", "efghij"}));
}

// Con el último renglón lleno, lo que sigue no va; y el último también se corta en su espacio.
TEST(SplitIntoLinesTest, WhatDoesNotFitInTheLastLineIsDropped)
{
    EXPECT_EQ(SplitIntoLines("aaaabbbbcccc", 2, 5), (Lines{"aaaa", "bbbb"}));
    EXPECT_EQ(SplitIntoLines("aaaaaa bbbbb", 1, 10), (Lines{"aaaaaa "}));
}

// Un renglón nuevo puede empezar con el espacio que no entró en el de antes.
TEST(SplitIntoLinesTest, ANewLineCanStartWithTheSpace)
{
    EXPECT_EQ(SplitIntoLines("abcde fgh", 2, 6), (Lines{"abcde", " fgh"}));
}

// Es un texto de C: termina en el primer '\0'.
TEST(SplitIntoLinesTest, StopsAtTheFirstNul)
{
    constexpr std::string_view WithNul{"ab\0cd", 5};
    EXPECT_EQ(SplitIntoLines(WithNul, 2, 83), (Lines{"ab"}));
}
} // namespace
