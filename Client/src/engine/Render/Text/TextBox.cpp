#include "Render/Text/TextBox.h"

#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextStyle.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace Client::Render::Text
{
namespace
{
// 0x467060 no le pasa a TextOutA un texto que empieza con este byte (0x4671e1).
constexpr char NewLine = '\n';
} // namespace

void StoreColor(std::uint32_t color, std::span<std::uint8_t, 4> pixel)
{
    pixel[0] = static_cast<std::uint8_t>(color & 0xff);
    pixel[1] = static_cast<std::uint8_t>((color >> 8) & 0xff);
    pixel[2] = static_cast<std::uint8_t>((color >> 16) & 0xff);
    pixel[3] = static_cast<std::uint8_t>((color >> 24) & 0xff);
}

void PaintTextBox(const IGlyphRasterizer& rasterizer, const TextStyle& style, std::string_view text, int firstColumn,
                  int width, int rows, TextBoxPixels rgba)
{
    std::ranges::fill(rgba, std::uint8_t{0});
    std::array<std::uint8_t, TextBoxPixelCount> ink{};
    if (!text.starts_with(NewLine))
    {
        rasterizer.DrawInk(style.font, text, firstColumn, ink);
    }

    const int columns = std::clamp(width, 0, TextBoxWidth);
    const int visibleRows = std::clamp(rows, 0, TextBoxHeight);
    for (int row = 0; row < visibleRows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const auto index = static_cast<std::size_t>(row * TextBoxWidth + column);
            // 0x465a00 (y 0x4668e0, el del texto con caché): cmp byte, 0xff.
            const std::uint32_t color = ink[index] == 0xff ? style.colors.text : style.colors.background;
            StoreColor(color, rgba.subspan(index * 4).first<4>());
        }
    }
}

void PaintBoxedText(const IGlyphRasterizer& rasterizer, const TextStyle& style, std::string_view text,
                    const BoxShape& box, TextBoxPixels rgba)
{
    std::ranges::fill(rgba, std::uint8_t{0});
    std::array<std::uint8_t, TextBoxPixelCount> ink{};
    // 0x553e64..0x553f2e: solo un texto no vacío llega a TextOutA.
    if (!text.empty())
    {
        rasterizer.DrawInk(style.font, text, 0, ink);
    }
    // 0x553f51..0x553f68: las filas, hasta el alto de la caja (32,0f de 0x7d3a1bc, con _ftol).
    const int rows = std::clamp(box.rows, 0, TextBoxHeight);
    const int columns = std::clamp(box.width, 0, TextBoxWidth);
    for (int row = 0; row < rows; ++row)
    {
        for (int column = 0; column < columns; ++column)
        {
            const auto index = static_cast<std::size_t>(row * TextBoxWidth + column);
            // 0x553f9c..0x553fc9: cmp byte, 0; el fondo, en las columnas antes del tope (el ancho: 0x555b00 pasa 0).
            std::uint32_t color = style.colors.background;
            if (ink[index] != 0)
                color = column < box.prefixColumns ? box.prefixColor : style.colors.text;
            StoreColor(color, rgba.subspan(index * 4).first<4>());
        }
    }
}
} // namespace Client::Render::Text
