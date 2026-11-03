#include "OriginalQuad.h"

#include <bit>
#include <cstdint>

namespace Labs
{
namespace
{
// Las inversas que usan 0x620d50 y 0x620d80, guardadas en float en 0x6ba070 (0x3acccccd) y
// 0x6ba06c (0x3b088889). Ninguna es exacta: 1/640 en float es 0,0015625000233, y 1/480,
// 0,0020833334420.
constexpr float InverseOf640 = 1.0f / 640;
constexpr float InverseOf480 = 1.0f / 480;
static_assert(std::bit_cast<std::uint32_t>(InverseOf640) == 0x3acccccd, "la de 0x6ba070");
static_assert(std::bit_cast<std::uint32_t>(InverseOf480) == 0x3b088889, "la de 0x6ba06c");

// FILD del tamaño, FMUL por la coordenada y FMUL por la inversa, en el x87; el que llama guarda el
// resultado como float (FSTP).
float Scale(float value, int size, float inverse)
{
    return static_cast<float>(static_cast<double>(size) * static_cast<double>(value) * static_cast<double>(inverse));
}
} // namespace

float ScaleX(float x, Screen screen)
{
    return Scale(x, screen.width, InverseOf640);
}

float ScaleY(float y, Screen screen)
{
    return Scale(y, screen.height, InverseOf480);
}

std::array<QuadVertex, 4> FanVertices(const OriginalQuad& quad, Screen screen)
{
    const float x = quad.scalePosition ? ScaleX(quad.x, screen) : quad.x;
    const float y = quad.scalePosition ? ScaleY(quad.y, screen) : quad.y;
    const float width = quad.scaleSize ? ScaleX(quad.width, screen) : quad.width;
    const float height = quad.scaleSize ? ScaleY(quad.height, screen) : quad.height;

    // 0x621bc9..0x621bf3: FILD H y FSUB y dejan H − y en la pila; FST la guarda como float (la de
    // arriba), y FLD ST0 y FSUB h restan del valor de la pila, sin redondear (la de abajo).
    const double topInTheStack = static_cast<double>(screen.height) - static_cast<double>(y);
    const auto top = static_cast<float>(topInTheStack);
    const auto bottom = static_cast<float>(topInTheStack - static_cast<double>(height));
    const float right = x + width;
    const float bottomV = quad.v + quad.dv;
    const float rightU = quad.u + quad.du;

    return {QuadVertex{x, top, quad.u, quad.v}, QuadVertex{x, bottom, quad.u, bottomV},
            QuadVertex{right, bottom, rightU, bottomV}, QuadVertex{right, top, rightU, quad.v}};
}
} // namespace Labs
