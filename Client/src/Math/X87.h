#pragma once

#include "World/Ftol.h"

#include <bit>
#include <cstdint>

// What the original's x87 code does with a value: store it as a float, load a float as a double, truncate it with
// 0x69fd3c, or read a float's bits.
namespace Client::Math
{
[[nodiscard]] constexpr float F32(double value)
{
    return static_cast<float>(value);
}

[[nodiscard]] constexpr double D(float value)
{
    return static_cast<double>(value);
}

[[nodiscard]] inline std::int32_t Truncate(double value)
{
    return World::FtolLow(value);
}

[[nodiscard]] constexpr std::uint32_t Bits(float value)
{
    return std::bit_cast<std::uint32_t>(value);
}
} // namespace Client::Math
