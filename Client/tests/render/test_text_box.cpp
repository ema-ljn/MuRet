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

// Client::Render::Text::PaintTextBox y StoreColor: 0x465a00, que pasa la tinta del DIB a la caja
// RGBA de 256 × 32 con el color del texto y el del fondo (06 § Texto). La tinta la pone el
// rasterizador falso: un bloque de 3 × 4 arriba a la izquierda de cada letra de 6 columnas.
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

// 0xAABBGGRR: el byte de abajo es el rojo, como lee glTexImage2D el dword con GL_RGBA.
TEST(TextBoxTest, AColorIsRedGreenBlueAlpha)
{
    std::array<std::uint8_t, 4> pixel{};

    StoreColor(0xff3322aa, pixel);

    EXPECT_EQ(pixel, (Rgba{0xaa, 0x22, 0x33, 0xff}));
}

// Donde hay tinta, el color del texto; en el resto de lo que se ve, el del fondo.
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

// Fuera de las columnas y las filas que se ven, la caja queda en 0, aunque el que llama la dé con
// otra cosa.
TEST(TextBoxTest, OutsideWhatIsSeenTheBoxIsEmpty)
{
    const auto box = Paint("abcd", 0, 12, 13);

    EXPECT_EQ(PixelAt(box, 12, 0), (Rgba{0, 0, 0, 0}));
    EXPECT_EQ(PixelAt(box, 18, 0), (Rgba{0, 0, 0, 0}));
    EXPECT_EQ(PixelAt(box, 0, 13), (Rgba{0, 0, 0, 0}));
    EXPECT_EQ(PixelAt(box, 255, 31), (Rgba{0, 0, 0, 0}));
}

// La caja k de un texto ancho empieza en la columna 256 k: la letra 43 empieza en 258, que es la
// columna 2 de la segunda caja, y la tinta de la 42 (de 252 a 254) queda en la primera.
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

// 0x467060 no le pasa a TextOutA un texto que empieza con un salto de línea (cmp byte, 0x0a en
// 0x4671e1): la caja que se ve queda toda con el fondo, sin letras.
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
    // Un salto de línea en el medio no cambia nada: se dibuja como cualquier byte.
    EXPECT_EQ(PixelAt(Paint("a\nb", 0, 18, 13), 0, 0), (Rgba{0xaa, 0x22, 0x33, 0xff}));
}

// Solo 0xff es letra, como en 0x465a00 (cmp byte, 0xff): un gris de un rasterizador con suavizado
// es fondo.
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

// 0x553e50: la tinta antes de las columnas de adelante va con su color y la otra con el del texto; el resto de la caja,
// el ancho del globo por las filas del paso, con el fondo, y afuera 0.
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

// Sin texto, el fondo solo; con más de 256 columnas (Q-C521), las 256 de la caja, y las filas hasta 32.
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
