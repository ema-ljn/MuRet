#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>

// Comparing two buffers byte by byte in a test, and saying where they differ: with thousands of bytes, "not equal"
// does not help find the bug.
namespace Client::Tests
{
// If not, the message gives both sizes or the first different byte, with its value in each.
inline ::testing::AssertionResult SameBytes(std::span<const std::byte> actual, std::span<const std::byte> expected)
{
    if (actual.size() != expected.size())
    {
        return ::testing::AssertionFailure()
               << "tiene " << actual.size() << " bytes y se esperaban " << expected.size();
    }
    const auto [differs, expectedByte] = std::ranges::mismatch(actual, expected);
    if (differs == actual.end())
    {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "el byte " << std::distance(actual.begin(), differs) << " es "
                                         << std::to_integer<int>(*differs) << " y se esperaba "
                                         << std::to_integer<int>(*expectedByte);
}

// Like SameBytes, but the message says where they differ and not their values: the sizes, or the position of the first
// different byte. It is for the wire bytes, which carry the player's login (MU_CLIENT_WIRE_DIR): a failing test does
// not print a payload.
inline ::testing::AssertionResult SameBytesWithoutValues(std::span<const std::byte> actual,
                                                         std::span<const std::byte> expected)
{
    if (actual.size() != expected.size())
    {
        return ::testing::AssertionFailure()
               << "tiene " << actual.size() << " bytes y se esperaban " << expected.size();
    }
    const auto differs = std::ranges::mismatch(actual, expected).in1;
    if (differs == actual.end())
    {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << "difiere desde el byte " << std::distance(actual.begin(), differs);
}
} // namespace Client::Tests
