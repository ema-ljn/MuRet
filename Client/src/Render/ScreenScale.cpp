#include "Render/ScreenScale.h"

#include <bit>
#include <cstdint>

namespace Client::Render
{
namespace
{
// Las inversas de 0x620d50 y 0x620d80, guardadas en float en 0x6ba070 y 0x6ba06c. Ninguna es
// exacta: 1/640 en float es 0,0015625000233, y 1/480, 0,0020833334420.
constexpr float InverseOf640 = 1.0f / 640;
constexpr float InverseOf480 = 1.0f / 480;
static_assert(std::bit_cast<std::uint32_t>(InverseOf640) == 0x3acccccd, "la de 0x6ba070");
static_assert(std::bit_cast<std::uint32_t>(InverseOf480) == 0x3b088889, "la de 0x6ba06c");

// FILD del tamaño, FMUL por la coordenada y FMUL por la inversa, en la x87 con 53 bits; el que
// llama guarda el resultado como float (FSTP). En double es la misma cuenta: los dos productos
// redondean igual a 53 bits.
float Scale(float value, int size, float inverse)
{
    return static_cast<float>(static_cast<double>(size) * static_cast<double>(value) * static_cast<double>(inverse));
}
} // namespace

float ScaleX(float x, App::PixelSize screen)
{
    return Scale(x, screen.width, InverseOf640);
}

float ScaleY(float y, App::PixelSize screen)
{
    return Scale(y, screen.height, InverseOf480);
}
} // namespace Client::Render
