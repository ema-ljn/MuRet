#include "Render/Text/TextBox.h"

#include "Render/Text/IGlyphRasterizer.h"

#include <algorithm>
#include <array>

namespace Client::Render::Text
{
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
    rasterizer.DrawInk(style.font, text, firstColumn, ink);

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
} // namespace Client::Render::Text
