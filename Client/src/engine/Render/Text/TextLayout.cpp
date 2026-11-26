#include "Render/Text/TextLayout.h"

#include "Core/PixelSize.h"
#include "Render/ScreenScale.h"
#include "Render/Text/FontSet.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace Client::Render::Text
{
namespace
{
// What 0x467060 measures instead of an empty text (0x6c7ae4, length 1).
constexpr std::string_view EmptyTextMeasure = "0";

// 0x554000's 0.01 (0x6b88d4), added to the width and the height before converting them to UV.
constexpr float TexelMargin = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(TexelMargin) == 0x3c23d70a, "el de 0x6b88d4");

// 0x6350b0's math: lea, shl and div, in unsigned 32 bits. The product wraps like the original's, and the subtraction
// goes back to int (sub at 0x6353de). Without a screen (a size of 0, which the original would divide by) nothing runs.
int Shrink(int position, int size, std::uint32_t designSize, int screenSize)
{
    if (screenSize <= 0)
    {
        return position;
    }
    const std::uint32_t scaled = static_cast<std::uint32_t>(size) * designSize;
    const std::uint32_t inDesign = scaled / static_cast<std::uint32_t>(screenSize);
    return static_cast<int>(static_cast<std::uint32_t>(position) - inDesign);
}

// _ftol (0x69fd3c): truncates toward 0.
int Truncate(double value)
{
    return static_cast<int>(value);
}

// fld of the size as float, fadd of 0.01 and fdiv by the box side on the 53-bit x87, and fstp to float. The sum fits
// exactly in a double.
float BoxUv(int size, int boxSize)
{
    return static_cast<float>((static_cast<double>(static_cast<float>(size)) + static_cast<double>(TexelMargin)) /
                              static_cast<double>(boxSize));
}
} // namespace

int RightAlignedAt(int x, TextExtent extent, Core::PixelSize screen)
{
    return Shrink(x, extent.width, 640, screen.width);
}

int EndingAt(int x, TextExtent extent, Core::PixelSize screen)
{
    if (screen.width <= 0)
    {
        return x;
    }
    return Truncate(static_cast<double>(x) -
                    static_cast<double>(extent.width) / static_cast<double>(ScaleX(1.0f, screen)));
}

int CenteredAt(int x, TextExtent extent, Core::PixelSize screen)
{
    if (screen.width <= 0)
    {
        return x;
    }
    const std::uint32_t scaled = static_cast<std::uint32_t>(extent.width) * 640U;
    const std::uint32_t half = scaled / static_cast<std::uint32_t>(screen.width) >> 1U;
    return static_cast<int>(static_cast<std::uint32_t>(x) - half);
}

int BottomAlignedAt(int y, TextExtent extent, Core::PixelSize screen)
{
    return Shrink(y, extent.height, 480, screen.height);
}

int DesignX(int pixelX, Core::PixelSize screen)
{
    // Without a screen, like Shrink: the ratio would be 0. With the x87 at 53 bits, the division in double gives the
    // same.
    if (screen.width <= 0)
    {
        return pixelX;
    }
    return Truncate(static_cast<double>(pixelX) / static_cast<double>(ScaleX(1.0f, screen)));
}

int DesignY(int pixelY, Core::PixelSize screen)
{
    if (screen.height <= 0)
    {
        return pixelY;
    }
    return Truncate(static_cast<double>(pixelY) / static_cast<double>(ScaleY(1.0f, screen)));
}

namespace
{
std::vector<TextPiece> TextPiecesAtPixelOrigin(double textLeft, double xInPixels, int yInPixels, TextExtent extent,
                                               Core::PixelSize screen)
{
    const int rows = std::min(extent.height, TextBoxHeight);
    int width = extent.width;
    if (width > TextBoxWidth && static_cast<double>(width) + textLeft > static_cast<double>(screen.width))
        width = Truncate(static_cast<double>(screen.width) - textLeft);

    const int remainder = width % TextBoxWidth;
    const int count = width / TextBoxWidth + (remainder > 0 ? 1 : 0);
    std::vector<TextPiece> pieces;
    pieces.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int k = 0; k < count; ++k)
    {
        const int firstColumn = k * TextBoxWidth;
        const int boxWidth = k == count - 1 && remainder > 0 ? remainder : TextBoxWidth;
        int boxLeft = Truncate(xInPixels + static_cast<double>(firstColumn));
        if (boxLeft < 0)
            boxLeft = 0;
        if (boxLeft + boxWidth > screen.width)
            boxLeft = screen.width - boxWidth;

        TextPiece piece;
        piece.firstColumn = firstColumn;
        piece.width = boxWidth;
        piece.rows = rows;
        piece.quad.slot = TextureSlot{0};
        piece.quad.x = static_cast<float>(boxLeft);
        piece.quad.y = static_cast<float>(yInPixels);
        piece.quad.width = static_cast<float>(boxWidth);
        piece.quad.height = static_cast<float>(rows);
        piece.quad.du = BoxUv(boxWidth, TextBoxWidth);
        piece.quad.dv = BoxUv(rows, TextBoxHeight);
        pieces.push_back(piece);
    }
    return pieces;
}
} // namespace

std::vector<TextPiece> TextPieces(int x, int y, TextExtent extent, Core::PixelSize screen)
{
    // DAT_05762ea8 and DAT_05762eac: W and H times the inverses of 640 and 480, as float (0x4b9441..0x4b9467), the same
    // as 0x620d50 scaling one point.
    const double ratioX = ScaleX(1.0f, screen);
    const double ratioY = ScaleY(1.0f, screen);
    const double textLeft = static_cast<double>(x) * ratioX;
    const double xInPixels = static_cast<double>(static_cast<float>(x)) * ratioX;
    const int yInPixels = Truncate(static_cast<double>(static_cast<float>(y)) * ratioY);
    return TextPiecesAtPixelOrigin(textLeft, xInPixels, yInPixels, extent, screen);
}

std::vector<TextPiece> TextPiecesAtPixels(int pixelX, int pixelY, TextExtent extent, Core::PixelSize screen)
{
    return TextPiecesAtPixelOrigin(static_cast<double>(pixelX), static_cast<double>(pixelX), pixelY, extent, screen);
}

CenteredInBox CenterInBox(int boxWidth, int textWidth)
{
    // 0x467120..0x46712f: mov, sub, cdq, sub, sar and lea. C++'s C division also truncates toward 0.
    const int textColumn = (boxWidth - textWidth) / 2;
    return CenteredInBox{.textColumn = textColumn, .width = textWidth + 2 * textColumn};
}

CenteredInBox CenterInBox(const ITextMeasure& measure, FontKind font, std::string_view text, int boxWidth)
{
    if (text.empty() && boxWidth == 0)
        return {};
    return CenterInBox(boxWidth, measure.Measure(font, text.empty() ? EmptyTextMeasure : text).width);
}

OriginalQuad BoxedQuad(int x, int y, int width, int rows, bool clampY, Core::PixelSize screen)
{
    // 0x554060..0x5540b4: in mode 0, against W in pixels.
    if (x < 0)
        x = 0;
    if (screen.width < x + width)
        x = screen.width - width;
    // 0x5540b6..0x55412a: the bottom cap leaves H · 47 / 640 (0x2f and 0x280) for the bar.
    if (clampY)
    {
        constexpr int BarRows = 0x2f;
        constexpr int BarDesignWidth = 0x280;
        if (y < 0)
            y = 0;
        const int bottom = screen.height - screen.height * BarRows / BarDesignWidth;
        if (bottom < y + rows)
            y = bottom - rows;
    }
    OriginalQuad quad;
    quad.slot = TextureSlot{0};
    quad.x = static_cast<float>(x);
    quad.y = static_cast<float>(y);
    quad.width = static_cast<float>(width);
    quad.height = static_cast<float>(rows);
    quad.du = BoxUv(width, TextBoxWidth);
    quad.dv = BoxUv(rows, TextBoxHeight);
    return quad;
}
} // namespace Client::Render::Text
