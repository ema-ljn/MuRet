#pragma once

#include <bit>
#include <cstdint>
#include <random>

// Floats and doubles built from the bits of std::mt19937_64, whose output the standard fixes. No arithmetic on
// them, which a compiler could do another way.
namespace Client::Tests
{
struct ExponentRange
{
    int lowest;
    int highest;
};

// A random sign and mantissa, and a binary exponent in `range`.
inline float FloatFromBits(std::mt19937_64& engine, ExponentRange range)
{
    const std::uint64_t bits = engine();
    const auto span = static_cast<std::uint64_t>(range.highest - range.lowest + 1);
    const auto exponent =
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(range.lowest + 127) + (bits >> 32) % span);
    return std::bit_cast<float>((static_cast<std::uint32_t>(bits) & 0x807fffffu) | (exponent << 23));
}

inline double DoubleFromBits(std::mt19937_64& engine, ExponentRange range)
{
    const std::uint64_t bits = engine();
    const auto span = static_cast<std::uint64_t>(range.highest - range.lowest + 1);
    const std::uint64_t exponent = static_cast<std::uint64_t>(range.lowest + 1023) + ((bits >> 52) & 0x7ffu) % span;
    return std::bit_cast<double>((bits & 0x800fffffffffffffu) | (exponent << 52));
}

inline float WithoutSign(float value)
{
    return std::bit_cast<float>(std::bit_cast<std::uint32_t>(value) & 0x7fffffffu);
}

inline double WithoutSign(double value)
{
    return std::bit_cast<double>(std::bit_cast<std::uint64_t>(value) & 0x7fffffffffffffffu);
}
} // namespace Client::Tests
