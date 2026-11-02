// Comparar dos buffers byte a byte en un test, y decir dónde difieren: con miles de bytes, "no son
// iguales" no ayuda a encontrar el bug.

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <span>

namespace Client::Tests
{

// Si `actual` tiene los mismos bytes que `expected`. Si no, el mensaje dice los dos tamaños o el
// primer byte distinto, con su valor en cada uno.
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

// Como SameBytes, pero el mensaje dice dónde difieren y no qué valen: los tamaños, o la posición del
// primer byte distinto. Es para los bytes del cable, que llevan el login del jugador
// (MURET_CLIENT_WIRE_DIR): un test que falla no imprime un payload.
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
