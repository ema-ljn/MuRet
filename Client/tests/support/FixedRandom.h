#pragma once

#include <cstdint>
#include <random>

// Random numbers that are the same with any compiler, for the fixed-seed tests. The standard fixes std::mt19937's
// numbers ([rand.predef]: the 10,000th from the default seed is 4123659995), but not
// std::uniform_int_distribution's, which change between libraries: with libstdc++ and libc++ a test would see other
// bytes.
namespace Client::Tests
{
// The date the first tests were written.
inline constexpr std::mt19937::result_type FixedSeed = 20261102;

// engine() % n: std::mt19937's output has 32 bits, so it fits a std::uint32_t. The remainder is not quite even, which
// is enough for a test.
inline std::uint32_t Below(std::mt19937& engine, std::uint32_t n)
{
    return static_cast<std::uint32_t>(engine() % n);
}
} // namespace Client::Tests
