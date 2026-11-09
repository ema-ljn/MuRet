#include "Core/MillisecondTime.h"

#include <bit>
#include <cstdint>
#include <limits>

namespace Client::Core
{
float Milliseconds32ToFloatRn(std::uint32_t sample) noexcept
{
    static_assert(std::numeric_limits<float>::is_iec559 && std::numeric_limits<float>::radix == 2 &&
                  std::numeric_limits<float>::digits == 24 && std::numeric_limits<float>::max_exponent == 128);
    constexpr int SignificandBits = 24;
    const int discardedBits = static_cast<int>(std::bit_width(sample)) - SignificandBits;
    if (discardedBits <= 0)
    {
        return static_cast<float>(sample);
    }

    const std::uint32_t spacing = std::uint32_t{1} << discardedBits;
    const std::uint32_t remainder = sample & (spacing - 1);
    const std::uint32_t halfway = spacing / 2;
    std::uint64_t significand = sample >> discardedBits;
    if (remainder > halfway || (remainder == halfway && (significand & 1U) != 0))
    {
        ++significand;
    }
    // El carry puede alcanzar 2^32: retenerlo en 64 bits, sin envolver a cero. La conversión
    // final es exacta, así que no hereda el RC del host ni levanta inexact por la cuantización.
    const std::uint64_t rounded = significand << discardedBits;
    return static_cast<float>(rounded);
}
} // namespace Client::Core
