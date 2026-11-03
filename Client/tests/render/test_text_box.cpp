#include "Render/Text/TextBox.h"

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
using Client::Render::Text::FontKind;
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
} // namespace
