#include "Render/Text/TextLayout.h"

#include "App/WindowState.h"
#include "Render/ScreenScale.h"
#include "Render/Text/IGlyphRasterizer.h"
#include "Render/Text/TextBox.h"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Client::Render::Text
{
namespace
{
// El 0,01 de 0x554000 (0x6b88d4) que se suma al ancho y al alto antes de pasarlos a UV.
constexpr float TexelMargin = 0.01f;
static_assert(std::bit_cast<std::uint32_t>(TexelMargin) == 0x3c23d70a, "el de 0x6b88d4");

// Las cuentas de 0x6350b0: lea, shl y div, en 32 bits sin signo. El producto da la vuelta como el
// del original, y la resta vuelve a int (sub en 0x6353de). Sin pantalla (un tamaño de 0, que el
// original dividiría) no corre nada.
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

// _ftol (0x69fd3c): trunca hacia el 0.
int Truncate(double value)
{
    return static_cast<int>(value);
}

// fld del tamaño en float, fadd del 0,01 y fdiv por el lado de la caja en la x87 de 53 bits, y
// fstp en float. La suma entra entera en un double.
float BoxUv(int size, int boxSize)
{
    return static_cast<float>((static_cast<double>(static_cast<float>(size)) + static_cast<double>(TexelMargin)) /
                              static_cast<double>(boxSize));
}
} // namespace

int RightAlignedAt(int x, TextExtent extent, App::PixelSize screen)
{
    return Shrink(x, extent.width, 640, screen.width);
}

int EndingAt(int x, TextExtent extent, App::PixelSize screen)
{
    if (screen.width <= 0)
    {
        return x;
    }
    return Truncate(static_cast<double>(x) -
                    static_cast<double>(extent.width) / static_cast<double>(ScaleX(1.0f, screen)));
}

int CenteredAt(int x, TextExtent extent, App::PixelSize screen)
{
    if (screen.width <= 0)
    {
        return x;
    }
    const std::uint32_t scaled = static_cast<std::uint32_t>(extent.width) * 640U;
    const std::uint32_t half = scaled / static_cast<std::uint32_t>(screen.width) >> 1U;
    return static_cast<int>(static_cast<std::uint32_t>(x) - half);
}

int BottomAlignedAt(int y, TextExtent extent, App::PixelSize screen)
{
    return Shrink(y, extent.height, 480, screen.height);
}

int DesignX(int pixelX, App::PixelSize screen)
{
    // Sin pantalla, como Shrink: la proporción sería 0. Con la x87 en 53 bits, la división en
    // double da lo mismo.
    if (screen.width <= 0)
    {
        return pixelX;
    }
    return Truncate(static_cast<double>(pixelX) / static_cast<double>(ScaleX(1.0f, screen)));
}

int DesignY(int pixelY, App::PixelSize screen)
{
    if (screen.height <= 0)
    {
        return pixelY;
    }
    return Truncate(static_cast<double>(pixelY) / static_cast<double>(ScaleY(1.0f, screen)));
}

std::vector<TextPiece> TextPieces(int x, int y, TextExtent extent, App::PixelSize screen)
{
    // DAT_05762ea8 y DAT_05762eac: W y H por las inversas de 640 y 480, en float
    // (0x4b9441..0x4b9467), lo mismo que escala 0x620d50 con un punto.
    const double ratioX = ScaleX(1.0f, screen);
    const double ratioY = ScaleY(1.0f, screen);
    const int rows = std::min(extent.height, TextBoxHeight);

    // 0x46721f..0x467253: fild x, fmul rx, y contra W el ancho más eso; el corte es W menos eso.
    int width = extent.width;
    const double textLeft = static_cast<double>(x) * ratioX;
    if (width > TextBoxWidth && static_cast<double>(width) + textLeft > static_cast<double>(screen.width))
    {
        width = Truncate(static_cast<double>(screen.width) - textLeft);
    }

    // 0x467259..0x467287: el resto y la cuenta de cajas con la división de C, que trunca.
    const int remainder = width % TextBoxWidth;
    const int count = width / TextBoxWidth + (remainder > 0 ? 1 : 0);

    // 0x467291..0x4672fc: la x y la y pasan por float antes de multiplicarse.
    const double xInPixels = static_cast<double>(static_cast<float>(x)) * ratioX;
    const int yInPixels = Truncate(static_cast<double>(static_cast<float>(y)) * ratioY);

    std::vector<TextPiece> pieces;
    pieces.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int k = 0; k < count; ++k)
    {
        const int firstColumn = k * TextBoxWidth;
        const int boxWidth = k == count - 1 && remainder > 0 ? remainder : TextBoxWidth;

        // 0x554000 sin escalar: una x negativa va a 0, y una caja que pasa de W va contra el borde.
        int boxLeft = Truncate(xInPixels + static_cast<double>(firstColumn));
        if (boxLeft < 0)
        {
            boxLeft = 0;
        }
        if (boxLeft + boxWidth > screen.width)
        {
            boxLeft = screen.width - boxWidth;
        }

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
} // namespace Client::Render::Text
