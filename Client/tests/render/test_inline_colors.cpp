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

// Client::Render::Text::ParseInlineColors e InlineColor: los cambios de color 0x02 de 0x4667a0 y la
// tabla de 0x4665d0. Los textos son los cinco del paso 2 del plan de la CL5 (CL5.3): sin 0x02, uno
// al principio, dos seguidos, uno al final y diez.
namespace
{
using Client::Render::Text::ColorRun;
using Client::Render::Text::InlineColor;
using Client::Render::Text::InlineColorContext;
using Client::Render::Text::MaxInlineColors;
using Client::Render::Text::ParseInlineColors;
using Client::Render::Text::TextColors;

// Los colores del login (0x6350b0): texto blanco y fondo negro con alfa 0x80.
constexpr TextColors Scene{0xffffffff, 0x80000000};
constexpr InlineColorContext Context{Scene};

constexpr TextColors Red{0xff0000ff, 0};
constexpr TextColors White{0xffffffff, 0};

// Un 0x02 con su código. Así una letra de la a a la f que sigue no entra en el \x del código.
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

// El tramo empieza en el texto limpio: la posición del 0x02 menos dos por cada par de antes. Dos
// seguidos empiezan en el mismo lugar, y el segundo es el que vale.
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

// 0x4665d0: 0x01 y 0xf4 rojo, de 0xf2 a 0xf8 los de su tabla sin fondo, y cualquier otro blanco sin
// fondo (también el 0, el 0x02 y los de arriba de 0xf8).
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

// 0xf0 deja los dos colores de la escena (DAT_006c8e5c y DAT_006c8e64) y 0xf1 los cruza: el texto
// con el color del fondo y el fondo con el del texto.
TEST(InlineColorsTest, F0AndF1SwapTheSceneColors)
{
    const InlineColorContext context{TextColors{0x11223344, 0x55667788}};

    EXPECT_EQ(InlineColor(0xf0, context), (TextColors{0x11223344, 0x55667788}));
    EXPECT_EQ(InlineColor(0xf1, context), (TextColors{0x55667788, 0x11223344}));
}

// Con DAT_006c7a88 en 0, 0xf8 lleva fondo: negro con alfa 0x96.
TEST(InlineColorsTest, F8HasABackgroundWhenTheFlagIsOff)
{
    const InlineColorContext withBackground{Scene, false};

    EXPECT_EQ(InlineColor(0xf8, withBackground), (TextColors{0xffc8dce6, 0x96000000}));
    EXPECT_EQ(InlineColor(0xf4, withBackground), Red);
}

// Nueve tramos y no más: el décimo par sale del texto igual, sin tramo, así que la "j" sigue con el
// color del noveno.
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

// Un 0x02 al final toma como código el 0 que termina el texto: un tramo blanco sin fondo que
// empieza donde termina el texto limpio, y no pinta nada.
TEST(InlineColorsTest, AMarkerAtTheEndHasNoCode)
{
    const auto parsed = ParseInlineColors("ab\x02", Context);

    EXPECT_EQ(parsed.text, "ab");
    EXPECT_EQ(parsed.runs, (std::vector<ColorRun>{{2, 0x00, White}}));
}

// El código es el byte que sigue, aunque sea otro 0x02: "\x02\x02" es un par, no dos marcas.
TEST(InlineColorsTest, TheByteAfterTheMarkerIsAlwaysTheCode)
{
    const auto parsed = ParseInlineColors("a" + Pair(0x02) + "b", Context);

    EXPECT_EQ(parsed.text, "ab");
    EXPECT_EQ(parsed.runs, (std::vector<ColorRun>{{1, 0x02, White}}));
}

// Como strlen: lo que va después del primer 0 no es del texto.
TEST(InlineColorsTest, StopsAtTheFirstNul)
{
    const auto parsed = ParseInlineColors(std::string("ab\0"sv) + Pair(0xf4) + "c", Context);

    EXPECT_EQ(parsed.text, "ab");
    EXPECT_TRUE(parsed.runs.empty());
}

// 0x4667a0 copia el texto a un buffer de 256 bytes de su pila y lo reescribe desde ahí en el del
// que llama: de un texto más largo, lo que pasa del 256 sale de la pila (Q-C77). MuRet arma el
// texto limpio aparte, sin tope, y no toca el del que llama.
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
