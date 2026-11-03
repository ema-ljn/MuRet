#include "Render/Text/TextLayout.h"

#include "App/WindowState.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"

#include "support/FakeGlyphRasterizer.h"

#include <gtest/gtest.h>

#include <vector>

// Client::Render::Text::RightAlignedAt, BottomAlignedAt y TextPieces: dónde van los textos del
// login (0x6350b0) y cómo los corta y los ubica 0x467060 con 0x554000 (06 § Texto). Los textos son
// sintéticos y los mide el rasterizador falso: 6 columnas por letra y 13 filas.
namespace
{
using Client::App::PixelSize;
using Client::Render::Text::BottomAlignedAt;
using Client::Render::Text::DesignX;
using Client::Render::Text::DesignY;
using Client::Render::Text::FontKind;
using Client::Render::Text::RightAlignedAt;
using Client::Render::Text::TextExtent;
using Client::Render::Text::TextPieces;
using Client::Tests::FakeGlyphRasterizer;

constexpr PixelSize Screen640{640, 480};
constexpr PixelSize Screen1024{1024, 768};
constexpr PixelSize Screen1280{1280, 1024};

// Las tres llamadas a 0x554180 de 0x6350b0: el texto 454 termina en x = 335, el 455 empieza ahí y
// el 456 (la versión) va en x = 0; los tres se apoyan en y = 479. Con un ancho de 60 y un alto de
// 13: en 640 × 480 la caja del 454 va en 275 y la y en 466; en 1024 × 768, 60 · 640 / 1024 = 37,5 y
// 13 · 480 / 768 = 8,125, truncados, dan 298 y 471, que en píxeles son 298 · 1,6 = 476,8 y 471 ·
// 1,6000001 = 753,6, truncados otra vez.
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

// cx · 640 / W en 32 bits sin signo y truncado: 13 · 640 / 1024 = 8,125 da 8, y con un cx de
// 6.710.887 el producto pasa de 2³² y da la vuelta a 384, que dividido por 640 da 0. Con int de 64
// bits daría 335 − 6.710.887.
TEST(TextLayoutTest, TheDivisionIsUnsignedAndInteger)
{
    EXPECT_EQ(RightAlignedAt(335, TextExtent{13, 0}, Screen1024), 327);
    EXPECT_EQ(RightAlignedAt(335, TextExtent{1, 0}, Screen1024), 335);
    EXPECT_EQ(RightAlignedAt(335, TextExtent{6710887, 0}, Screen640), 335);
    EXPECT_EQ(RightAlignedAt(335, TextExtent{6710886, 0}, Screen640), 335 - 6710886);

    EXPECT_EQ(BottomAlignedAt(479, TextExtent{0, 13}, Screen1024), 471);
    EXPECT_EQ(BottomAlignedAt(479, TextExtent{0, 8947849}, Screen640), 479);
}

// Q-C60: el original resta cy · 640 / W también en la y. En 4:3 da lo mismo; en 1280 × 1024, con un
// alto de 14, resta 7 en vez de 6.
TEST(TextLayoutTest, TheLoginTextUsesTheHeightForY)
{
    const TextExtent extent{60, 14};
    const auto original = static_cast<int>(479u - (14u * 640u) / 1280u);

    EXPECT_EQ(original, 472);
    EXPECT_EQ(BottomAlignedAt(479, extent, Screen1280), 473);
    EXPECT_EQ(BottomAlignedAt(479, extent, Screen1024), 479 - static_cast<int>((14u * 640u) / 1024u));
}

// El quad de 0x554000: el slot 0, en píxeles y sin escalar, con la caja de 256 × 32 recortada a lo
// que se ve más 0,01 de téxel (0x6b88d4).
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

// Las filas son las de la celda, hasta las 32 de la caja.
TEST(TextLayoutTest, TheRowsAreTheCellUpTo32)
{
    const auto tall = TextPieces(0, 0, TextExtent{10, 40}, Screen640);

    ASSERT_EQ(tall.size(), 1u);
    EXPECT_EQ(tall[0].rows, 32);
    EXPECT_EQ(tall[0].quad.height, 32.0f);
    EXPECT_EQ(tall[0].quad.dv, 1.0003124475479126f);
}

// Una caja por cada 256 columnas, la última con lo que sobra, cada una 256 píxeles más a la
// derecha: x · 1,6 = 16,0000002 en 1024 × 768.
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

// Un texto de más de 256 columnas que pasa del borde derecho se corta en el borde, truncado: en
// 1024 × 768, x = 400 va en 640,0000095, y quedan 383 columnas y no 384.
TEST(TextLayoutTest, AWideTextIsCutAtTheRightEdge)
{
    const auto pieces = TextPieces(400, 0, TextExtent{500, 13}, Screen1024);

    ASSERT_EQ(pieces.size(), 2u);
    EXPECT_EQ(pieces[0].width, 256);
    EXPECT_EQ(pieces[1].width, 127);
    EXPECT_EQ(pieces[0].quad.x, 640.0f);
    EXPECT_EQ(pieces[1].quad.x, 896.0f);

    // Del todo afuera no queda nada.
    EXPECT_TRUE(TextPieces(640, 0, TextExtent{300, 13}, Screen640).empty());
}

// Uno de hasta 256 no se corta: 0x554000 lo corre contra el borde. Y una x negativa (un texto 454
// más ancho que 335 puntos) va a 0.
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

// Las ventanas de la UI nueva ubican sus textos en píxeles y los pasan a 640 × 480 para 0x5542f0:
// fild del píxel, fdiv por rx o ry en float y _ftol (0x641aa1..0x641ad6, CL5.5). En 640 × 480 los
// dos valen 1. En 1024 × 768, rx es 1,6 en float, un poco más que 1,6: 240 da 149,99999 y queda
// en 149. En 800 × 600, ry es 1,2500001: 240 da 191.
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

// _ftol trunca hacia el 0: un píxel negativo no baja al entero de abajo.
TEST(TextLayoutTest, DesignXTruncatesTowardZero)
{
    EXPECT_EQ(DesignX(-5, Screen1024), -3);
}

// Sin pantalla (un lado en 0, que el original dividiría), el píxel queda como está, como en
// RightAlignedAt: dividir por una proporción de 0 y pasar el infinito a int sería UB.
TEST(TextLayoutTest, DesignXAndYWithoutAScreenAreThePixel)
{
    EXPECT_EQ(DesignX(240, PixelSize{0, 0}), 240);
    EXPECT_EQ(DesignY(240, PixelSize{640, 0}), 240);
    EXPECT_EQ(DesignX(0, PixelSize{0, 480}), 0);
}
} // namespace
