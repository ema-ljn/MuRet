#include "Render/ScreenScale.h"

#include "Core/PixelSize.h"

#include <bit>
#include <cstdint>

namespace Client::Render
{
namespace
{
// The inverses of 0x620d50 and 0x620d80, stored as float at 0x6ba070 and 0x6ba06c. Neither is exact: 1/640 as float
// is 0.0015625000233, and 1/480, 0.0020833334420.
constexpr float InverseOf640 = 1.0f / 640;
constexpr float InverseOf480 = 1.0f / 480;
static_assert(std::bit_cast<std::uint32_t>(InverseOf640) == 0x3acccccd, "la de 0x6ba070");
static_assert(std::bit_cast<std::uint32_t>(InverseOf480) == 0x3b088889, "la de 0x6ba06c");

// FILD of the size, FMUL by the coordinate and FMUL by the inverse, on the x87 at 53 bits; the caller stores the
// result as float (FSTP). In double it is the same math: both products round alike at 53 bits.
float Scale(float value, int size, float inverse)
{
    return static_cast<float>(static_cast<double>(size) * static_cast<double>(value) * static_cast<double>(inverse));
}
} // namespace

float ScaleX(float x, Core::PixelSize screen)
{
    return Scale(x, screen.width, InverseOf640);
}

float ScaleY(float y, Core::PixelSize screen)
{
    return Scale(y, screen.height, InverseOf480);
}
} // namespace Client::Render
