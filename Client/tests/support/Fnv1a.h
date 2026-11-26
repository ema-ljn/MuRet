#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

// The 64-bit FNV-1a, a small hash to compare many bytes with a stored value: that of the pixels the 6b gives for each
// lab OZJ (Client/tests/vectors/jpeg/lab-6b.txt). It is not cryptographic and need not be: the point is to see whether
// a decoder gives other bytes.
namespace Client::Tests
{
// The 64-bit offset basis and prime, from the authors' page (Fowler, Noll and Vo):
// http://www.isthe.com/chongo/tech/comp/fnv/
inline constexpr std::uint64_t Fnv1aOffsetBasis = 0xcbf29ce484222325;
inline constexpr std::uint64_t Fnv1aPrime = 0x100000001b3;

// For each byte, first the xor and then the product, modulo 2^64 (a uint64_t wraps without undefined behavior).
constexpr std::uint64_t Fnv1a64(std::span<const std::byte> bytes)
{
    std::uint64_t hash = Fnv1aOffsetBasis;
    for (const std::byte value : bytes)
    {
        hash = (hash ^ std::to_integer<std::uint64_t>(value)) * Fnv1aPrime;
    }
    return hash;
}
} // namespace Client::Tests
